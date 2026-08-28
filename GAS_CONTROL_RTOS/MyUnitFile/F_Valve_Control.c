#include "F_Valve_Control.h"

#include <stddef.h>

/*
 * 函数名：F_ValveControl_OtherSupplyIsOn。
 * 说明：检查除指定气瓶外是否还有其他供气阀处于软件开启状态。
 * 输入：system 为只读系统状态指针；requested_index 为本次请求的气瓶索引。
 * 输出：存在其他已开启供气阀时返回 true，否则返回 false。
 */
static bool F_ValveControl_OtherSupplyIsOn(const Gas_System *system, uint8_t requested_index)
{
    uint8_t i;

    for (i = 0U; i < GAS_CYLINDER_COUNT; ++i)
    {
        if ((i != requested_index) && system->cylinder[i].supply_cmd)
        {
            return true;
        }
    }
    return false;
}

/*
 * 函数名：F_ValveControl_ManualOpenAllowed。
 * 说明：统一判断指定气瓶是否允许开启排气阀或测试阀，两个人工阀门之间不互相限制。
 * 输入：system 为系统状态只读指针；index 为从 0 开始的气瓶索引。
 * 输出：系统处于自动模式、气瓶状态允许且进气阀关闭时返回 true，否则返回 false。
 */
static bool F_ValveControl_ManualOpenAllowed(const Gas_System *system, uint8_t index)
{
    gas_cylinder_state_t state;

    if ((system == NULL) || (index >= GAS_CYLINDER_COUNT) ||
        (system->mode != GAS_MODE_AUTO) || system->cylinder[index].supply_cmd)
    {
        return false;
    }

    state = system->cylinder[index].state;
    return ((state == GAS_CYL_INIT) ||
            (state == GAS_CYL_WAIT_TEST) ||
            (state == GAS_CYL_READY) ||
            (state == GAS_CYL_LOW_REPLACE));
}

/*
 * 函数名：F_ValveControl_AllOff。
 * 说明：关闭全部板级阀门输出，并同步清除六瓶供气阀、排气阀和测试阀的软件命令状态。
 * 输入：platform 为硬件上下文；system 为可选的系统状态输出指针，可传 NULL 仅执行硬件全关。
 * 输出：无返回值；当 system 非空时清除其中的全部阀门命令。
 */
void F_ValveControl_AllOff(H_Gas_Platform_Context *platform, Gas_System *system)
{
    uint8_t i;

    H_GasPlatform_AllValvesOff(platform);
    if (system != NULL)
    {
        for (i = 0U; i < GAS_CYLINDER_COUNT; ++i)
        {
            system->cylinder[i].supply_cmd = false;
            system->cylinder[i].exhaust_cmd = false;
            system->cylinder[i].test_cmd = false;
            system->cylinder[i].exhaust_deadline_ms = 0U;
            system->cylinder[i].test_deadline_ms = 0U;
        }
        system->total_test_cmd = false;
        // 硬件全关与软件命令、人工计时同步清零，避免恢复运行后出现幽灵开阀状态。
    }
}

/*
 * 函数名：F_ValveControl_Init。
 * 说明：以全部阀门关闭的安全状态初始化阀门服务。
 * 输入：platform 为硬件上下文；system 为系统状态输入输出指针。
 * 输出：无返回值；通过 system 输出清零后的阀门命令状态。
 */
void F_ValveControl_Init(H_Gas_Platform_Context *platform, Gas_System *system)
{
    F_ValveControl_AllOff(platform, system);
}

/*
 * 函数名：F_ValveControl_Task。
 * 说明：周期推进 12 V 吸合计时，并在本次开阀指定时间到达后切换到约 5 V 保持状态。
 * 输入：platform 为硬件上下文；system 为系统状态；now_ms 为当前毫秒计数。
 * 输出：无；硬件切换失败时通过 system 设置平台异常报警。
 */
void F_ValveControl_Task(H_Gas_Platform_Context *platform,
                       Gas_System *system,
                       uint32_t now_ms)
{
    if ((platform == NULL) || (system == NULL))
    {
        return;
    }

    if (!H_GasPlatform_ValveTask(platform, now_ms))
    {
        system->alarm_bits |= GAS_ALARM_PLATFORM_NOT_READY;
    }
}

/*
 * 函数名：F_ValveControl_SetSupply。
 * 说明：在气瓶状态、供排气互锁和单路供气约束下设置指定供气阀。
 * 输入：platform 为硬件上下文；system 为系统状态；config 为运行参数；index 为气瓶索引；on 为目标开关状态。
 * 输出：阀门命令成功执行时返回 true；参数、互锁或硬件条件不满足时返回 false。
 */
bool F_ValveControl_SetSupply(H_Gas_Platform_Context *platform,
                              Gas_System *system,
                              const Gas_Config *config,
                              uint8_t index,
                              bool on)
{
    if ((platform == NULL) || (system == NULL) || (config == NULL) ||
        (index >= GAS_CYLINDER_COUNT))
    {
        return false;
    }

    if (on && ((system->cylinder[index].state != GAS_CYL_READY) &&
               (system->cylinder[index].state != GAS_CYL_ACTIVE) &&
               (system->cylinder[index].state != GAS_CYL_LOW_WARNING)))
    {
        system->alarm_bits |= GAS_ALARM_VALVE_INTERLOCK;
        return false;
    }
    // 关阀请求始终允许通过状态检查，紧急关断不能被业务状态限制。

    if (on && (system->cylinder[index].state == GAS_CYL_READY) &&
        !system->cylinder[index].qualification_passed)
    {
        system->alarm_bits |= GAS_ALARM_VALVE_INTERLOCK;
        return false;
    }

    if (on && (system->cylinder[index].exhaust_cmd ||
               system->cylinder[index].test_cmd ||
               F_ValveControl_OtherSupplyIsOn(system, index)))
    {
        system->alarm_bits |= GAS_ALARM_VALVE_INTERLOCK;
        return false;
    }
    // 进气阀与同瓶两个人工阀门互斥，同时从软件层保证全系统最多一路供气。

    if (!H_GasPlatform_WriteSupplyValve(platform, index, on, config->valve_pull_in_time_ms))
    {
        system->alarm_bits |= GAS_ALARM_PLATFORM_NOT_READY;
        return false;
    }

    system->cylinder[index].supply_cmd = on;
    // 只有硬件层接受输出命令后才更新软件状态，保证两层状态一致。
    return true;
}

/*
 * 函数名：F_ValveControl_SetExhaust。
 * 说明：仅在自动模式的初始化、待测试、待用或低压待换状态下设置排气阀，并保持与供气阀互锁；允许测试阀同时开启。
 * 输入：platform 为硬件上下文；system 为系统状态；config 为运行参数；index 为气瓶索引；on 为目标开关状态。
 * 输出：阀门命令成功执行时返回 true；参数、互锁或硬件条件不满足时返回 false。
 */
bool F_ValveControl_SetExhaust(H_Gas_Platform_Context *platform,
                               Gas_System *system,
                               const Gas_Config *config,
                               uint8_t index,
                               bool on)
{
    if ((platform == NULL) || (system == NULL) || (config == NULL) ||
        (index >= GAS_CYLINDER_COUNT))
    {
        return false;
    }

    if (on && !F_ValveControl_ManualOpenAllowed(system, index))
    {
        system->alarm_bits |= GAS_ALARM_VALVE_INTERLOCK;
        return false;
    }

    // 排气阀和测试阀可以同时工作，但任一人工阀门开启期间始终禁止同瓶进气。

    if (!H_GasPlatform_WriteExhaustValve(platform, index, on, config->valve_pull_in_time_ms))
    {
        system->alarm_bits |= GAS_ALARM_PLATFORM_NOT_READY;
        return false;
    }

    system->cylinder[index].exhaust_cmd = on;
    return true;
}

/*
 * 函数名：F_ValveControl_SetTest。
 * 说明：仅在自动模式的初始化、待测试、待用或低压待换状态下设置测试阀，并保持与供气阀互锁；允许排气阀同时开启。
 * 输入：platform 为硬件上下文；system 为系统状态；config 为运行参数；index 为气瓶索引；on 为目标状态。
 * 输出：阀门命令成功执行时返回 true；参数、互锁或硬件条件不满足时返回 false。
 */
bool F_ValveControl_SetTest(H_Gas_Platform_Context *platform,
                            Gas_System *system,
                            const Gas_Config *config,
                            uint8_t index,
                            bool on)
{
    if ((platform == NULL) || (system == NULL) || (config == NULL) ||
        (index >= GAS_CYLINDER_COUNT))
    {
        return false;
    }

    if (on && !F_ValveControl_ManualOpenAllowed(system, index))
    {
        system->alarm_bits |= GAS_ALARM_VALVE_INTERLOCK;
        return false;
    }

    if (on && !system->total_test_cmd)
    {
        system->alarm_bits |= GAS_ALARM_VALVE_INTERLOCK;
        return false;
    }
    // 分路测试阀只能在总测试阀命令已经建立后打开，禁止绕过VAL_CAL形成封闭测试支路。

    // 测试阀和排气阀共享开启权限，但各自使用独立的自动关闭截止时间。

    if (!H_GasPlatform_WriteTestValve(platform, index, on, config->valve_pull_in_time_ms))
    {
        system->alarm_bits |= GAS_ALARM_PLATFORM_NOT_READY;
        return false;
    }

    system->cylinder[index].test_cmd = on;
    return true;
}

/*
 * 函数名：F_ValveControl_SetTotalTest。
 * 说明：设置VAL_CAL总测试阀，并在硬件成功后同步系统命令镜像。
 * 输入：platform为硬件上下文；system为系统状态；config为运行参数；on为目标状态。
 * 输出：命令已经处于目标状态或成功写入时返回true，参数或硬件异常时返回false。
 */
bool F_ValveControl_SetTotalTest(H_Gas_Platform_Context *platform,
                                 Gas_System *system,
                                 const Gas_Config *config,
                                 bool on)
{
    if ((platform == NULL) || (system == NULL) || (config == NULL))
    {
        return false;
    }
    if (system->total_test_cmd == on)
    {
        return true;
    }
    if (!H_GasPlatform_WriteTotalTestValve(platform,
                                            on,
                                            config->valve_pull_in_time_ms))
    {
        system->alarm_bits |= GAS_ALARM_PLATFORM_NOT_READY;
        return false;
    }

    system->total_test_cmd = on;
    return true;
}

/*
 * 函数名：F_ValveControl_TotalTestCanOpen。
 * 说明：检查1号阀组共享强吸合电源的最短间隔是否已经结束。
 * 输入：platform为只读硬件上下文；now_ms为当前毫秒时间。
 * 输出：没有间隔限制或已经到达允许时间时返回true，否则返回false。
 */
bool F_ValveControl_TotalTestCanOpen(const H_Gas_Platform_Context *platform,
                                     uint32_t now_ms)
{
    if (platform == NULL)
    {
        return false;
    }
    return (!platform->boost_interval_active[0] ||
            ((int32_t) (now_ms - platform->boost_available_ms[0]) >= 0));
}
