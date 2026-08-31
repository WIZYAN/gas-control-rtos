/*
 * Version: v1.11
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现气源系统状态机、自动换瓶、人工控制和安全互锁业务。
 */

#include "A_Gas_Control.h"

#include <stddef.h>
#include <string.h>

#define A_GAS_CONTROL_COMM_RECORD_ADDRESS (0x0030U) // 36字节参数记录之后的外部通讯模式记录起始地址。
#define A_GAS_CONTROL_COMM_LEGACY_ADDRESS (0x0020U) // V2工程使用的旧模式地址，仅用于启动迁移。
#define A_GAS_CONTROL_COMM_RECORD_SIZE    (8U)      // 模式记录固定长度，包含标识、版本、模式和CRC16。
#define A_GAS_CONTROL_COMM_RECORD_VERSION (1U)      // 外部通讯模式记录格式版本。
#define A_GAS_CONTROL_RECOVERY_SAMPLES    (3U)      // 低压瓶恢复到待测试前需要取得的独立合格压力样本数。

static bool A_GasControl_SaveCommMode(A_Storage_Context *storage,
                                      gas_external_comm_mode_t mode);
static bool A_GasControl_AllValveCommandsAreOff(const Gas_System *system);

/*
 * 函数名：A_GasControl_AnyTestValveOn。
 * 说明：检查六路分支测试阀是否至少有一路已经实际执行开启命令。
 * 输入：system为只读气源系统状态。
 * 输出：至少一路测试阀开启时返回true，否则返回false。
 */
static bool A_GasControl_AnyTestValveOn(const Gas_System *system)
{
    uint8_t index; // 当前作用域变量，用于保存遍历索引。

    if (system == NULL)
    {
        return false;
    }
    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        if (system->cylinder[index].test_cmd)
        {
            return true;
        }
    }
    return false;
}

/*
 * 函数名：A_GasControl_ForceAllValvesOff。
 * 说明：关闭十九路阀门并清除尚未执行的分支测试阀开启请求。
 * 输入：context为气源控制应用上下文输入输出指针。
 * 输出：无；参数有效时同步清零硬件、业务命令和总测试阀联动状态。
 */
static void A_GasControl_ForceAllValvesOff(A_Gas_Control_Context *context)
{
    if (context == NULL)
    {
        return;
    }

    F_ValveControl_AllOff(&context->runtime_service.platform, &context->system);
    (void) memset(&context->total_test, 0, sizeof(context->total_test));
}

/*
 * 函数名：A_GasControl_CommRecordCrc16。
 * 说明：计算外部通讯模式EEPROM记录前六字节的Modbus多项式CRC16。
 * 输入：data为只读记录数据；length为参与计算的字节数。
 * 输出：返回16位CRC校验值。
 */
static uint16_t A_GasControl_CommRecordCrc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU; // 当前作用域变量，用于保存CRC校验值。
    size_t index; // 当前作用域变量，用于保存遍历索引。
    uint8_t bit; // 当前作用域变量，用于保存位掩码。

    for (index = 0U; index < length; ++index)
    {
        crc = (uint16_t) (crc ^ (uint16_t) data[index]);
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = ((crc & 1U) != 0U) ? (uint16_t) ((crc >> 1U) ^ 0xA001U)
                                     : (uint16_t) (crc >> 1U);
        }
    }
    return crc;
}

/*
 * 函数名：A_GasControl_ReadCommModeAtAddress。
 * 说明：从指定EEPROM地址读取并校验一条外部通讯模式记录。
 * 输入：storage为已经初始化的存储实例；address为记录地址；mode为通讯模式输出指针。
 * 输出：标识、版本、模式及CRC全部有效时返回true，否则返回false。
 */
static bool A_GasControl_ReadCommModeAtAddress(A_Storage_Context *storage,
                                               uint16_t address,
                                               gas_external_comm_mode_t *mode)
{
    uint8_t record[A_GAS_CONTROL_COMM_RECORD_SIZE]; // 当前作用域变量，用于保存日志或配置记录缓冲区。
    uint16_t stored_crc; // 当前作用域变量，用于保存CRC校验值。
    uint16_t calculated_crc; // 当前作用域变量，用于保存CRC校验值。

    if ((storage == NULL) || (mode == NULL) ||
        !A_Storage_Read(storage,
                        address,
                        record,
                        sizeof(record)))
    {
        return false;
    }
    stored_crc = (uint16_t) (((uint16_t) record[7] << 8U) | record[6]);
    calculated_crc = A_GasControl_CommRecordCrc16(record, 6U);
    if ((record[0] != (uint8_t) 'G') || (record[1] != (uint8_t) 'C') ||
        (record[2] != (uint8_t) 'O') || (record[3] != (uint8_t) 'M') ||
        (record[4] != A_GAS_CONTROL_COMM_RECORD_VERSION) ||
        (record[5] > (uint8_t) GAS_EXTERNAL_COMM_RS485) ||
        (stored_crc != calculated_crc))
    {
        return false;
    }
    *mode = (gas_external_comm_mode_t) record[5];
    return true;
}

/*
 * 函数名：A_GasControl_LoadCommMode。
 * 说明：优先读取0x0030的新记录；不存在时兼容读取0x0020旧记录并立即迁移。
 * 输入：storage为已经初始化的存储实例；mode为通讯模式输出指针。
 * 输出：任一地址存在完整有效记录时返回true，否则返回false。
 */
static bool A_GasControl_LoadCommMode(A_Storage_Context *storage,
                                      gas_external_comm_mode_t *mode)
{
    if (A_GasControl_ReadCommModeAtAddress(storage,
                                           A_GAS_CONTROL_COMM_RECORD_ADDRESS,
                                           mode))
    {
        return true;
    }
    if (!A_GasControl_ReadCommModeAtAddress(storage,
                                            A_GAS_CONTROL_COMM_LEGACY_ADDRESS,
                                            mode))
    {
        return false;
    }

    (void) A_GasControl_SaveCommMode(storage, *mode);
    // 必须先把旧模式迁到0x0030，随后V2参数升级为当前记录时才可安全覆盖0x0020～0x0023。
    return true;
}

/*
 * 函数名：A_GasControl_SaveCommMode。
 * 说明：把当前外部通讯模式编码为8字节带CRC记录并写入EEPROM后读回校验。
 * 输入：storage为已经初始化的存储实例；mode为待保存通讯模式。
 * 输出：写入和读回校验均成功时返回true，否则返回false。
 */
static bool A_GasControl_SaveCommMode(A_Storage_Context *storage,
                                      gas_external_comm_mode_t mode)
{
    uint8_t record[A_GAS_CONTROL_COMM_RECORD_SIZE]; // 当前作用域变量，用于保存日志或配置记录缓冲区。
    uint8_t verify[A_GAS_CONTROL_COMM_RECORD_SIZE]; // 当前作用域变量，用于保存读回校验缓冲区。
    uint16_t crc; // 当前作用域变量，用于保存CRC校验值。

    if ((storage == NULL) || (mode > GAS_EXTERNAL_COMM_RS485))
    {
        return false;
    }
    record[0] = (uint8_t) 'G';
    record[1] = (uint8_t) 'C';
    record[2] = (uint8_t) 'O';
    record[3] = (uint8_t) 'M';
    record[4] = A_GAS_CONTROL_COMM_RECORD_VERSION;
    record[5] = (uint8_t) mode;
    crc = A_GasControl_CommRecordCrc16(record, 6U);
    record[6] = (uint8_t) crc;
    record[7] = (uint8_t) (crc >> 8U);
    if (!A_Storage_Write(storage,
                         A_GAS_CONTROL_COMM_RECORD_ADDRESS,
                         record,
                         sizeof(record)) ||
        !A_Storage_Read(storage,
                        A_GAS_CONTROL_COMM_RECORD_ADDRESS,
                        verify,
                        sizeof(verify)))
    {
        return false;
    }
    return (memcmp(record, verify, sizeof(record)) == 0);
}

/*
 * 函数名：A_GasControl_TimeReached。
 * 说明：使用有符号毫秒差判断当前时间是否已经到达截止时间，并兼容计数器回绕。
 * 输入：now_ms 为当前时间；deadline_ms 为截止时间。
 * 输出：已经到达或超过截止时间时返回 true，否则返回 false。
 */
static bool A_GasControl_TimeReached(uint32_t now_ms, uint32_t deadline_ms)
{
    return ((int32_t) (now_ms - deadline_ms) >= 0);
}

/*
 * 函数名：A_GasControl_PressureIsFresh。
 * 说明：检查指定气瓶是否具有未超过运行参数时限的有效压力数据。
 * 输入：cylinder 为只读气瓶数据；config 为只读运行参数；now_ms 为当前时间。
 * 输出：压力有效且未过期时返回 true，否则返回 false。
 */
static bool A_GasControl_PressureIsFresh(const Gas_Cylinder *cylinder,
                                         const Gas_Config *config,
                                         uint32_t now_ms)
{
    return ((cylinder != NULL) && (config != NULL) &&
            (cylinder->pressure_quality == GAS_PRESSURE_VALID) &&
            !A_GasControl_TimeReached(now_ms,
                                      cylinder->pressure_timestamp_ms + config->pressure_fresh_ms));
}

/*
 * 函数名：A_GasControl_CloseCylinderValves。
 * 说明：关闭指定气瓶的进气阀、排气阀和测试阀并清除人工阀门计时。
 * 输入：context 为气源控制应用上下文；index 为从 0 开始的气瓶索引。
 * 输出：三路关闭命令均成功时返回 true，否则返回 false。
 */
static bool A_GasControl_CloseCylinderValves(A_Gas_Control_Context *context, uint8_t index)
{
    Gas_Cylinder *cylinder; // 当前作用域变量，用于保存气瓶对象或编号指针。
    bool supply_ok; // supply_ok 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示操作失败，true表示操作成功。
    bool exhaust_ok; // exhaust_ok 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示操作失败，true表示操作成功。
    bool test_ok; // test_ok 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示操作失败，true表示操作成功。

    if ((context == NULL) || (index >= GAS_CYLINDER_COUNT))
    {
        return false;
    }

    cylinder = &context->system.cylinder[index];
    supply_ok = F_ValveControl_SetSupply(&context->runtime_service.platform,
                                         &context->system,
                                         &context->config,
                                         index,
                                         false);
    exhaust_ok = F_ValveControl_SetExhaust(&context->runtime_service.platform,
                                           &context->system,
                                           &context->config,
                                           index,
                                           false);
    test_ok = A_GasControl_SetTestValve(context, index, false);
    cylinder->exhaust_deadline_ms = 0U;
    cylinder->test_deadline_ms = 0U;
    return (supply_ok && exhaust_ok && test_ok);
}

/*
 * 函数名：F_GasControl_EnterLowReplace。
 * 说明：统一把指定气瓶转入低压待换，清除原测试合格结论并复位压力恢复确认计数。
 * 输入：context为气源控制应用上下文；index为从0开始的气瓶索引。
 * 输出：无；参数有效时更新气瓶状态并请求串口屏把对应测试通过按钮恢复为未通过。
 */
static void F_GasControl_EnterLowReplace(A_Gas_Control_Context *context, uint8_t index)
{
    Gas_Cylinder *cylinder; // 当前作用域变量，用于保存气瓶对象或编号指针。
    bool state_changed; // state_changed 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未发生变化，true表示已经发生变化。
    bool qualification_changed; // qualification_changed 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未发生变化，true表示已经发生变化。

    if ((context == NULL) || (index >= GAS_CYLINDER_COUNT))
    {
        return;
    }

    cylinder = &context->system.cylinder[index];
    state_changed = (cylinder->state != GAS_CYL_LOW_REPLACE);
    qualification_changed = cylinder->qualification_passed;
    cylinder->state = GAS_CYL_LOW_REPLACE;
    cylinder->qualification_passed = false;
    cylinder->recovery_sample_count = 0U;
    cylinder->recovery_sample_timestamp_ms = cylinder->pressure_timestamp_ms;
    if (state_changed || qualification_changed)
    {
        A_Hmi_RequestQualificationSync(&context->hmi, index);
    }
    // 只有首次进入或原标志确实为通过时才请求优先回写，持续低压不会重复占用串口带宽。
}

/*
 * 函数名：F_GasControl_RecoveryPressureConfirmed。
 * 说明：按压力样本时间戳累计独立合格样本，确认低压待换气瓶的压力已经稳定恢复。
 * 输入：cylinder为气瓶状态输入输出指针。
 * 输出：累计达到固定的三个独立合格样本时返回true，否则返回false。
 */
static bool F_GasControl_RecoveryPressureConfirmed(Gas_Cylinder *cylinder)
{
    if (cylinder == NULL)
    {
        return false;
    }

    if ((cylinder->recovery_sample_count == 0U) ||
        (cylinder->recovery_sample_timestamp_ms != cylinder->pressure_timestamp_ms))
    {
        cylinder->recovery_sample_timestamp_ms = cylinder->pressure_timestamp_ms;
        if (cylinder->recovery_sample_count < A_GAS_CONTROL_RECOVERY_SAMPLES)
        {
            cylinder->recovery_sample_count++;
        }
        // 只在传感器产生新时间戳时累计，5 ms控制任务不得重复计算同一份压力数据。
    }
    return (cylinder->recovery_sample_count >= A_GAS_CONTROL_RECOVERY_SAMPLES);
}

/*
 * 函数名：A_GasControl_FindNextReady。
 * 说明：从指定位置之后按 1→2→3→4→5→6→1 顺序查找可投入工作的待用瓶。
 * 输入：system 为只读系统状态；config 为运行参数；start_index 为查找起点；now_ms 为当前时间。
 * 输出：找到时返回 0～5 的气瓶索引，否则返回 GAS_CYLINDER_COUNT。
 */
static uint8_t A_GasControl_FindNextReady(const Gas_System *system,
                                          const Gas_Config *config,
                                          uint8_t start_index,
                                          uint32_t now_ms)
{
    uint8_t offset; // 当前作用域变量，用于保存数据偏移量。

    for (offset = 1U; offset <= GAS_CYLINDER_COUNT; ++offset)
    {
        uint8_t index = (uint8_t) ((start_index + offset) % GAS_CYLINDER_COUNT); // 当前作用域变量，用于保存遍历索引。
        const Gas_Cylinder *cylinder = &system->cylinder[index]; // 当前作用域变量，用于保存气瓶对象或编号指针。

        if ((cylinder->state == GAS_CYL_READY) &&
            cylinder->qualification_passed &&
            !cylinder->supply_cmd && !cylinder->exhaust_cmd && !cylinder->test_cmd &&
            A_GasControl_PressureIsFresh(cylinder, config, now_ms) &&
            (cylinder->pressure_mpa >= config->ready_min_pressure_mpa))
        {
            return index;
        }
    }
    return GAS_CYLINDER_COUNT;
}

/*
 * 函数名：A_GasControl_UpdateCylinderStates。
 * 说明：根据有效压力、测试合格标志、当前工作瓶和停用标志维护七种气瓶业务状态。
 * 输入：context 为气源控制应用上下文；now_ms 为当前时间。
 * 输出：无；更新六只气瓶的初始化、待测试、待用、使用、低压待换和低压警告状态。
 */
static void A_GasControl_UpdateCylinderStates(A_Gas_Control_Context *context, uint32_t now_ms)
{
    Gas_System *system = &context->system; // 当前作用域变量，用于保存气源系统对象指针。
    uint8_t index; // 当前作用域变量，用于保存遍历索引。

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        Gas_Cylinder *cylinder = &system->cylinder[index]; // 当前作用域变量，用于保存气瓶对象或编号指针。
        bool switching_new = ((system->switch_state == GAS_SWITCH_VERIFY_NEW) &&
                              (index == system->switch_new_index));

        if (cylinder->state == GAS_CYL_DISABLED)
        {
            if (cylinder->supply_cmd || cylinder->exhaust_cmd || cylinder->test_cmd)
            {
                (void) A_GasControl_CloseCylinderValves(context, index);
            }
            continue; // 停用优先级最高，状态判断不得重新打开阀门或自动改变为其他状态。
        }

        if (cylinder->supply_cmd && ((index == system->active_index) || switching_new))
        {
            if (A_GasControl_PressureIsFresh(cylinder, &context->config, now_ms))
            {
                cylinder->state = (cylinder->pressure_mpa < context->config.low_warning_pressure_mpa) ?
                                  GAS_CYL_LOW_WARNING : GAS_CYL_ACTIVE;
                system->alarm_bits &= ~(uint32_t) GAS_ALARM_ACTIVE_SENSOR;
            }
            else
            {
                cylinder->state = GAS_CYL_LOW_WARNING;
                system->alarm_bits |= GAS_ALARM_ACTIVE_SENSOR;
            }
            continue; // 工作瓶只在使用和低压警告之间切换，不套用备用瓶的1.5 MPa判断。
        }

        if (cylinder->supply_cmd)
        {
            (void) A_GasControl_CloseCylinderValves(context, index);
            system->alarm_bits |= GAS_ALARM_VALVE_INTERLOCK;
            // 非工作瓶出现进气命令属于异常，立即关断并留下互锁报警供诊断。
        }

        if (A_GasControl_PressureIsFresh(cylinder, &context->config, now_ms))
        {
            if (cylinder->pressure_mpa < context->config.ready_min_pressure_mpa)
            {
                F_GasControl_EnterLowReplace(context, index);
            }
            else if (cylinder->state == GAS_CYL_LOW_REPLACE)
            {
                if (F_GasControl_RecoveryPressureConfirmed(cylinder))
                {
                    cylinder->state = GAS_CYL_WAIT_TEST;
                    cylinder->recovery_sample_count = 0U;
                    // 低压瓶只有连续取得三个独立合格样本后才表示新瓶压力恢复，随后等待人工测试。
                }
            }
            else
            {
                if (cylinder->qualification_passed)
                {
                    cylinder->state = GAS_CYL_READY;
                }
                else
                {
                    cylinder->state = GAS_CYL_WAIT_TEST;
                }
                cylinder->recovery_sample_count = 0U;
                // 压力合格但没有人工测试结论时统一显示待测试，不再混用初始化或低压待换状态。
            }
        }
        else
        {
            cylinder->state = GAS_CYL_INIT;
        }
    }
}

/*
 * 函数名：A_GasControl_SelectInitialBottle。
 * 说明：在自动模式且当前无工作瓶时按顺序选择一只待用瓶并打开进气阀。
 * 输入：context 为气源控制应用上下文；now_ms 为当前时间。
 * 输出：成功选中并开启一只气瓶时返回 true，否则返回 false。
 */
static bool A_GasControl_SelectInitialBottle(A_Gas_Control_Context *context, uint32_t now_ms)
{
    Gas_System *system = &context->system; // 当前作用域变量，用于保存气源系统对象指针。
    uint8_t start_index; // 当前作用域变量，用于保存遍历索引。
    uint8_t next_index; // 当前作用域变量，用于保存遍历索引。

    if ((system->mode != GAS_MODE_AUTO) || !system->platform_ready ||
        (system->active_index < GAS_CYLINDER_COUNT) ||
        (system->switch_state != GAS_SWITCH_IDLE))
    {
        return false;
    }

    start_index = (system->switch_old_index < GAS_CYLINDER_COUNT) ?
                  system->switch_old_index : (uint8_t) (GAS_CYLINDER_COUNT - 1U);
    next_index = A_GasControl_FindNextReady(system, &context->config, start_index, now_ms);
    // 无历史工作瓶时从6号之后开始，使首次选择自然落到1号瓶。
    if (next_index >= GAS_CYLINDER_COUNT)
    {
        system->alarm_bits |= GAS_ALARM_NO_BACKUP;
        return false;
    }

    if (!A_GasControl_CloseCylinderValves(context, next_index) ||
        !F_ValveControl_SetSupply(&context->runtime_service.platform,
                                  system,
                                  &context->config,
                                  next_index,
                                  true))
    {
        return false;
    }

    system->active_index = next_index;
    system->switch_old_index = GAS_NO_ACTIVE_CYLINDER;
    system->cylinder[next_index].state = GAS_CYL_ACTIVE;
    system->alarm_bits &= ~(uint32_t) GAS_ALARM_NO_BACKUP;
    // 只有阀门实际打开成功后才提交active_index，避免软件状态领先于硬件输出。
    return true;
}

/*
 * 函数名：A_GasControl_ManualValveTask。
 * 说明：按当前运行参数到时关闭人工排气阀和测试阀，并保证停用瓶三阀全关。
 * 输入：context 为气源控制应用上下文；now_ms 为当前时间。
 * 输出：无；更新人工阀门命令和异常报警。
 */
static void A_GasControl_ManualValveTask(A_Gas_Control_Context *context, uint32_t now_ms)
{
    uint8_t index; // 当前作用域变量，用于保存遍历索引。

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        Gas_Cylinder *cylinder = &context->system.cylinder[index]; // 当前作用域变量，用于保存气瓶对象或编号指针。

        if (cylinder->state == GAS_CYL_DISABLED)
        {
            if (cylinder->supply_cmd || cylinder->exhaust_cmd || cylinder->test_cmd)
            {
                (void) A_GasControl_CloseCylinderValves(context, index);
            }
            continue;
        }

        if (cylinder->exhaust_cmd &&
            A_GasControl_TimeReached(now_ms, cylinder->exhaust_deadline_ms))
        {
            if (!F_ValveControl_SetExhaust(&context->runtime_service.platform,
                                           &context->system,
                                           &context->config,
                                           index,
                                           false))
            {
                context->system.alarm_bits |= GAS_ALARM_MANUAL_VALVE_ABORTED;
            }
            cylinder->exhaust_deadline_ms = 0U;
            // 无论关阀接口是否成功都清除截止时间，失败通过报警处理，避免每周期重复操作。
        }

        if (cylinder->test_cmd &&
            A_GasControl_TimeReached(now_ms, cylinder->test_deadline_ms))
        {
            if (!A_GasControl_SetTestValve(context, index, false))
            {
                context->system.alarm_bits |= GAS_ALARM_MANUAL_VALVE_ABORTED;
            }
            // 一分钟上限由MCU强制执行，不依赖串口屏再次发送关闭事件。
        }
    }
}

/*
 * 函数名：A_GasControl_TotalTestTask。
 * 说明：在总测试阀预先吸合并满足共享VALP1+间隔后，分步执行等待中的测试阀开启请求。
 * 输入：context为气源控制应用上下文输入输出指针；now_ms为当前毫秒时间。
 * 输出：无；成功时开启分路并启动独立超时，失败时撤销请求并按需关闭总测试阀。
 */
static void A_GasControl_TotalTestTask(A_Gas_Control_Context *context, uint32_t now_ms)
{
    uint8_t index; // 当前作用域变量，用于保存遍历索引。
    uint8_t bit; // 当前作用域变量，用于保存位掩码。

    if (context == NULL)
    {
        return;
    }

    if ((context->total_test.pending_open_mask != 0U) &&
        !context->system.total_test_cmd &&
        F_ValveControl_TotalTestCanOpen(&context->runtime_service.platform, now_ms))
    {
        if (!F_ValveControl_SetTotalTest(&context->runtime_service.platform,
                                         &context->system,
                                         &context->config,
                                         true))
        {
            context->total_test.pending_open_mask = 0U;
            (void) memset(context->total_test.open_not_before_ms,
                          0,
                          sizeof(context->total_test.open_not_before_ms));
            context->system.alarm_bits |= GAS_ALARM_MANUAL_VALVE_ABORTED;
        }
        else
        {
            for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
            {
                bit = (uint8_t) (1U << index);
                if ((context->total_test.pending_open_mask & bit) != 0U)
                {
                    context->total_test.open_not_before_ms[index] =
                        now_ms + GAS_VALVE_BOOST_MIN_INTERVAL_MS;
                }
            }
        }
        // 共享电源可用后先吸合总阀，所有等待分路统一再等待500 ms才允许打开。
    }
    if ((context->total_test.pending_open_mask != 0U) &&
        !context->system.total_test_cmd)
    {
        return;
    }

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        bit = (uint8_t) (1U << index);
        if (((context->total_test.pending_open_mask & bit) == 0U) ||
            !A_GasControl_TimeReached(now_ms,
                                      context->total_test.open_not_before_ms[index]))
        {
            continue;
        }

        context->total_test.pending_open_mask &= (uint8_t) ~bit;
        context->total_test.open_not_before_ms[index] = 0U;
        if (F_ValveControl_SetTest(&context->runtime_service.platform,
                                   &context->system,
                                   &context->config,
                                   index,
                                   true))
        {
            context->system.cylinder[index].test_deadline_ms =
                now_ms + context->config.test_valve_max_time_ms;
        }
        else
        {
            context->system.cylinder[index].test_deadline_ms = 0U;
            context->system.alarm_bits |= GAS_ALARM_MANUAL_VALVE_ABORTED;
        }
        A_Hmi_RequestTestSync(&context->hmi, index);
        // 分路真正执行后再回写既有测试按钮，不增加任何新HMI控件。
    }

    if (!A_GasControl_AnyTestValveOn(&context->system) &&
        (context->total_test.pending_open_mask == 0U) &&
        context->system.total_test_cmd &&
        !F_ValveControl_SetTotalTest(&context->runtime_service.platform,
                                     &context->system,
                                     &context->config,
                                     false))
    {
        context->system.alarm_bits |= GAS_ALARM_MANUAL_VALVE_ABORTED;
    }
    // 总测试阀只在至少一路实际开启或等待开启期间保持，最后一路结束后立即关闭。
}

/*
 * 函数名：A_GasControl_SwitchTask。
 * 说明：执行工作瓶低压确认、顺序查找、先关旧瓶和后开新瓶的非阻塞自动切换。
 * 输入：context 为气源控制应用上下文；now_ms 为当前时间。
 * 输出：无；更新切换状态、气瓶状态、进气阀和报警位。
 */
static void A_GasControl_SwitchTask(A_Gas_Control_Context *context, uint32_t now_ms)
{
    Gas_System *system = &context->system; // 当前作用域变量，用于保存气源系统对象指针。
    Gas_Cylinder *active; // 当前作用域变量，用于保存当前活动对象指针。
    uint8_t next_index; // 当前作用域变量，用于保存遍历索引。

    if ((system->mode != GAS_MODE_AUTO) || !system->platform_ready)
    {
        return;
    }
    if (system->active_index >= GAS_CYLINDER_COUNT)
    {
        (void) A_GasControl_SelectInitialBottle(context, now_ms);
        return;
    }

    active = &system->cylinder[system->active_index];
    switch (system->switch_state)
    {
        case GAS_SWITCH_IDLE: // 正常监测工作瓶，仅在低压首次满足时启动确认流程。
            if (active->state == GAS_CYL_DISABLED)
            {
                (void) A_GasControl_CloseCylinderValves(context, system->active_index);
                system->switch_old_index = system->active_index;
                system->active_index = GAS_NO_ACTIVE_CYLINDER;
                (void) A_GasControl_SelectInitialBottle(context, now_ms);
                break;
            }

            if (!A_GasControl_PressureIsFresh(active, &context->config, now_ms))
            {
                active->state = GAS_CYL_LOW_WARNING;
                system->alarm_bits |= GAS_ALARM_ACTIVE_SENSOR;
                break;
            }

            active->state = (active->pressure_mpa < context->config.low_warning_pressure_mpa) ?
                            GAS_CYL_LOW_WARNING : GAS_CYL_ACTIVE;
            if (active->pressure_mpa <= context->config.switch_pressure_mpa)
            {
                system->low_sample_count = 1U;
                system->low_last_sample_ms = active->pressure_timestamp_ms;
                system->low_start_ms = now_ms;
                system->switch_state = GAS_SWITCH_LOW_CONFIRM;
                // 同时记录第一个低压样本及起始时间，样本数和持续时间必须同时满足。
            }
            break;

        case GAS_SWITCH_LOW_CONFIRM: // 使用回差和独立采样时间戳过滤瞬时波动及重复计数。
            if (!A_GasControl_PressureIsFresh(active, &context->config, now_ms))
            {
                system->switch_state = GAS_SWITCH_IDLE;
                system->low_sample_count = 0U;
                system->alarm_bits |= GAS_ALARM_ACTIVE_SENSOR;
                break;
            }
            if (active->pressure_mpa >= context->config.switch_release_mpa)
            {
                system->switch_state = GAS_SWITCH_IDLE;
                system->low_sample_count = 0U;
                break;
            }
            if ((active->pressure_mpa <= context->config.switch_pressure_mpa) &&
                (active->pressure_timestamp_ms != system->low_last_sample_ms))
            {
                if (system->low_sample_count < UINT8_MAX)
                {
                    system->low_sample_count++;
                }
                system->low_last_sample_ms = active->pressure_timestamp_ms;
                // 只累计传感器产生的新样本，主循环重复读取同一数值不会增加计数。
            }
            if ((system->low_sample_count >= context->config.low_confirm_samples) &&
                A_GasControl_TimeReached(now_ms,
                                         system->low_start_ms + context->config.low_confirm_time_ms))
            {
                system->switch_state = GAS_SWITCH_FIND_NEXT;
            }
            break;

        case GAS_SWITCH_FIND_NEXT: // 按固定环形顺序选择压力、测试结果和阀门状态均合格的备用瓶。
            next_index = A_GasControl_FindNextReady(system,
                                                    &context->config,
                                                    system->active_index,
                                                    now_ms);
            if (next_index >= GAS_CYLINDER_COUNT)
            {
                system->switch_state = GAS_SWITCH_NO_BACKUP;
            }
            else
            {
                system->switch_old_index = system->active_index;
                system->switch_new_index = next_index;
                system->switch_state = GAS_SWITCH_CLOSE_OLD;
            }
            break;

        case GAS_SWITCH_CLOSE_OLD: // 先关闭旧瓶，切换过程中允许短暂断气但禁止两瓶同时供气。
            if (F_ValveControl_SetSupply(&context->runtime_service.platform,
                                         system,
                                         &context->config,
                                         system->switch_old_index,
                                         false))
            {
                system->switch_enter_ms = now_ms;
                system->switch_state = GAS_SWITCH_DEAD_TIME;
            }
            break;

        case GAS_SWITCH_DEAD_TIME: // 等待旧阀机械释放完成后才允许进入新瓶开阀阶段。
            if (A_GasControl_TimeReached(now_ms,
                                         system->switch_enter_ms + context->config.valve_close_wait_ms))
            {
                system->switch_state = GAS_SWITCH_OPEN_NEW;
            }
            break;

        case GAS_SWITCH_OPEN_NEW: // 开阀前再次强制关闭目标瓶三阀，确保供气与排气、测试互斥。
            if (!A_GasControl_CloseCylinderValves(context, system->switch_new_index) ||
                !F_ValveControl_SetSupply(&context->runtime_service.platform,
                                          system,
                                          &context->config,
                                          system->switch_new_index,
                                          true))
            {
                A_GasControl_ForceAllValvesOff(context);
                system->active_index = GAS_NO_ACTIVE_CYLINDER;
                system->mode = GAS_MODE_STOPPED;
                system->switch_state = GAS_SWITCH_IDLE;
                system->alarm_bits |= GAS_ALARM_PLATFORM_NOT_READY;
                // 新瓶无法安全打开时全关并停机，禁止继续使用不确定的阀门状态。
                break;
            }
            system->switch_enter_ms = now_ms;
            system->switch_state = GAS_SWITCH_VERIFY_NEW;
            break;

        case GAS_SWITCH_VERIFY_NEW: // 等待新阀机械动作完成后再正式交换工作瓶身份。
            if (A_GasControl_TimeReached(now_ms,
                                         system->switch_enter_ms + context->config.valve_open_wait_ms))
            {
                if (system->cylinder[system->switch_old_index].state != GAS_CYL_DISABLED)
                {
                    F_GasControl_EnterLowReplace(context, system->switch_old_index);
                }
                system->cylinder[system->switch_new_index].state = GAS_CYL_ACTIVE;
                system->active_index = system->switch_new_index;
                system->switch_state = GAS_SWITCH_IDLE;
                system->switch_old_index = GAS_NO_ACTIVE_CYLINDER;
                system->switch_new_index = GAS_NO_ACTIVE_CYLINDER;
                system->low_sample_count = 0U;
                system->alarm_bits &= ~(uint32_t) GAS_ALARM_NO_BACKUP;
                // 旧瓶转入待换、新瓶转入使用以及索引清理作为一次完整状态提交。
            }
            break;

        case GAS_SWITCH_NO_BACKUP: // 没有备用瓶时不主动切断旧瓶，仅报警并继续低压供气。
            system->alarm_bits |= GAS_ALARM_NO_BACKUP;
            active->state = GAS_CYL_LOW_WARNING;
            system->low_sample_count = 0U;
            system->switch_state = GAS_SWITCH_IDLE;
            break;

        default:
            system->switch_state = GAS_SWITCH_IDLE;
            system->low_sample_count = 0U;
            break;
    }
}

/*
 * 函数名：A_GasControl_ProcessHmiButton。
 * 说明：把串口屏阀门、停用、测试合格、日志查询和参数页按钮转换为对应业务操作。
 * 输入：context 为气源控制应用上下文输入输出指针。
 * 输出：无；操作失败时设置人工阀门互锁报警。
 */
static void A_GasControl_ProcessHmiButton(A_Gas_Control_Context *context)
{
    uint16_t button_id; // 当前作用域变量，用于保存串口屏控件标识。
    uint8_t value; // 当前作用域变量，用于保存当前处理值。
    uint8_t index; // 当前作用域变量，用于保存遍历索引。
    bool success = true; // success 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示操作失败，true表示操作成功。

    if (!A_Hmi_TakeButtonEvent(&context->hmi, &button_id, &value))
    {
        return;
    }

    if ((button_id >= A_HMI_EXHAUST_BUTTON_BASE) &&
        (button_id < (A_HMI_EXHAUST_BUTTON_BASE + GAS_CYLINDER_COUNT)))
    {
        index = (uint8_t) (button_id - A_HMI_EXHAUST_BUTTON_BASE);
        if (value != 0U)
        {
            success = A_GasControl_StartExhaust(context, index);
        }
        A_Hmi_RequestExhaustSync(&context->hmi, index);
        // 0仅表示屏端开关被再次触碰，不作为提前关阀命令；最终按钮状态始终回写MCU实际排气命令。
    }
    else if ((button_id >= A_HMI_TEST_BUTTON_BASE) &&
             (button_id < (A_HMI_TEST_BUTTON_BASE + GAS_CYLINDER_COUNT)))
    {
        index = (uint8_t) (button_id - A_HMI_TEST_BUTTON_BASE);
        success = A_GasControl_SetTestValve(context, index, value != 0U);
        A_Hmi_RequestTestSync(&context->hmi, index);
        // 无论请求是否通过互锁，都以MCU最终测试阀命令校正屏幕开关。
    }
    else if ((button_id >= A_HMI_DISABLE_BUTTON_BASE) &&
             (button_id < (A_HMI_DISABLE_BUTTON_BASE + GAS_CYLINDER_COUNT)))
    {
        index = (uint8_t) (button_id - A_HMI_DISABLE_BUTTON_BASE);
        success = A_GasControl_SetCylinderDisabled(context, index, value != 0U);
        A_Hmi_RequestDisableSync(&context->hmi, index);
        // 停用请求被拒绝时也立即恢复按钮，避免本地开关外观与气瓶状态不一致。
    }
    else if ((button_id >= A_HMI_QUALIFIED_BUTTON_BASE) &&
             (button_id < (A_HMI_QUALIFIED_BUTTON_BASE + GAS_CYLINDER_COUNT)))
    {
        index = (uint8_t) (button_id - A_HMI_QUALIFIED_BUTTON_BASE);
        success = A_GasControl_SetQualificationPassed(context, index, value != 0U);
        A_Hmi_RequestQualificationSync(&context->hmi, index);
        // 无论请求是否被状态门槛接受，都优先把按钮校正为MCU中的最终测试结论。
    }
    else if (A_HmiLog_HandleFilterButton(&context->hmi_log, button_id, value))
    {
        success = true;
        // 条件页选择、事件/常规查询和结果页签由日志模块统一处理。
    }
    else if ((button_id == A_HMI_EVENT_LOG_QUERY_BUTTON_ID) && (value != 0U))
    {
        success = A_HmiLog_Request(&context->hmi_log, A_HMI_LOG_QUERY_EVENT);
        // 刷新事件日志时重建单类型RAM索引，并优先显示最新的第1页。
    }
    else if ((button_id == A_HMI_REGULAR_LOG_QUERY_BUTTON_ID) && (value != 0U))
    {
        success = A_HmiLog_Request(&context->hmi_log, A_HMI_LOG_QUERY_REGULAR);
        // 刷新常规日志时重建索引，每页10条并保持单条压力、状态双行格式。
    }
    else if ((button_id == A_HMI_EVENT_LOG_LATEST_BUTTON_ID) && (value != 0U))
    {
        success = A_HmiLog_RequestPage(&context->hmi_log,
                                       A_HMI_LOG_QUERY_EVENT,
                                       A_HMI_LOG_PAGE_LATEST);
    }
    else if ((button_id == A_HMI_EVENT_LOG_PREVIOUS_BUTTON_ID) && (value != 0U))
    {
        success = A_HmiLog_RequestPage(&context->hmi_log,
                                       A_HMI_LOG_QUERY_EVENT,
                                       A_HMI_LOG_PAGE_PREVIOUS);
    }
    else if ((button_id == A_HMI_EVENT_LOG_NEXT_BUTTON_ID) && (value != 0U))
    {
        success = A_HmiLog_RequestPage(&context->hmi_log,
                                       A_HMI_LOG_QUERY_EVENT,
                                       A_HMI_LOG_PAGE_NEXT);
    }
    else if ((button_id == A_HMI_REGULAR_LOG_LATEST_BUTTON_ID) && (value != 0U))
    {
        success = A_HmiLog_RequestPage(&context->hmi_log,
                                       A_HMI_LOG_QUERY_REGULAR,
                                       A_HMI_LOG_PAGE_LATEST);
    }
    else if ((button_id == A_HMI_REGULAR_LOG_PREVIOUS_BUTTON_ID) && (value != 0U))
    {
        success = A_HmiLog_RequestPage(&context->hmi_log,
                                       A_HMI_LOG_QUERY_REGULAR,
                                       A_HMI_LOG_PAGE_PREVIOUS);
    }
    else if ((button_id == A_HMI_REGULAR_LOG_NEXT_BUTTON_ID) && (value != 0U))
    {
        success = A_HmiLog_RequestPage(&context->hmi_log,
                                       A_HMI_LOG_QUERY_REGULAR,
                                       A_HMI_LOG_PAGE_NEXT);
    }
    else if ((button_id == A_HMI_CONFIG_MENU_BUTTON_ID) && (value != 0U))
    {
        success = A_HmiConfig_Open(&context->hmi_config,
                                   &context->config);
    }
    else if ((button_id == A_HMI_LOG_CLEAR_BUTTON_ID) && (value != 0U))
    {
        success = A_HmiConfig_OpenLogClear(&context->hmi_config,
                                           A_GasLog_GetCount(&context->log_service));
        if (success && A_GasLog_IsClearBusy(&context->log_service))
        {
            A_HmiConfig_ReportLogClear(&context->hmi_config,
                                       A_HMI_LOG_CLEAR_BUSY,
                                       A_GasLog_GetClearProgress(&context->log_service),
                                       0U);
        }
        // 已在后台清除时重新进入Screen7只显示当前进度，不允许重复建立第二个擦除任务。
    }
    else if (A_HmiConfig_HandleLogClearButton(&context->hmi_config,
                                              button_id,
                                              value))
    {
        success = true;
    }
    else if (A_HmiConfig_HandleButton(&context->hmi_config,
                                      button_id,
                                      value,
                                      &context->config))
    {
        success = true;
    }

    if (!success)
    {
        context->system.alarm_bits |= GAS_ALARM_MANUAL_VALVE_ABORTED;
        // 按钮请求被状态或互锁拒绝时统一告警，串口屏协议层不直接判断业务安全条件。
    }
}

/*
 * 函数名：A_GasControl_CheckOutputInvariant。
 * 说明：检查最多一路进气、进气与人工阀互锁以及停用瓶全关不变量；排气阀和测试阀允许同时开启。
 * 输入：context 为气源控制应用上下文输入输出指针。
 * 输出：无；发现冲突时关闭全部阀门、停止自动模式并设置报警。
 */
static void A_GasControl_CheckOutputInvariant(A_Gas_Control_Context *context)
{
    uint8_t index; // 当前作用域变量，用于保存遍历索引。
    uint8_t supply_count = 0U; // 当前作用域变量，用于保存数量计数。
    bool any_test_on = false; // 测试阀汇总状态标志；使用范围：当前测试阀运行检查函数内；取值范围：false/true，false表示六路测试阀均关闭，true表示至少一路测试阀开启。
    bool conflict = false; // conflict 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无冲突，true表示存在冲突。

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        const Gas_Cylinder *cylinder = &context->system.cylinder[index]; // 当前作用域变量，用于保存气瓶对象或编号指针。

        if (cylinder->supply_cmd)
        {
            supply_count++;
        }
        if ((cylinder->supply_cmd && (cylinder->exhaust_cmd || cylinder->test_cmd)) ||
            ((cylinder->state == GAS_CYL_DISABLED) &&
             (cylinder->supply_cmd || cylinder->exhaust_cmd || cylinder->test_cmd)))
        {
            conflict = true;
        }
        if (cylinder->test_cmd)
        {
            any_test_on = true; // 当前作用域变量，用于保存当前处理数据。
        }
    }

    if ((any_test_on && !context->system.total_test_cmd) ||
        (!any_test_on && (context->total_test.pending_open_mask == 0U) &&
         context->system.total_test_cmd))
    {
        conflict = true;
    }
    // 总测试阀允许在分路等待阶段预先打开，除此之外必须与六路测试阀的或逻辑一致。

    if ((supply_count > 1U) || conflict)
    {
        A_GasControl_ForceAllValvesOff(context);
        context->system.active_index = GAS_NO_ACTIVE_CYLINDER;
        context->system.mode = GAS_MODE_STOPPED;
        context->system.switch_state = GAS_SWITCH_IDLE;
        context->system.alarm_bits |= GAS_ALARM_VALVE_INTERLOCK;
        if (supply_count > 1U)
        {
            context->system.alarm_bits |= GAS_ALARM_MULTIPLE_SUPPLY;
        }
        // 不变量检查是所有业务路径之后的最后保护，任何冲突都执行全关和停止。
    }
}

/*
 * 函数名：A_GasControl_AllValveCommandsAreOff。
 * 说明：检查十八路分瓶阀和VAL_CAL总测试阀软件命令是否已经全部关闭。
 * 输入：system 为只读气源系统状态指针。
 * 输出：十九路阀门命令全部为关闭时返回 true，参数无效或存在开阀命令时返回 false。
 */
static bool A_GasControl_AllValveCommandsAreOff(const Gas_System *system)
{
    uint8_t index; // 当前作用域变量，用于保存遍历索引。

    if (system == NULL)
    {
        return false;
    }
    if (system->total_test_cmd)
    {
        return false;
    }

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        const Gas_Cylinder *cylinder = &system->cylinder[index]; // 当前作用域变量，用于保存气瓶对象或编号指针。

        if (cylinder->supply_cmd || cylinder->exhaust_cmd || cylinder->test_cmd)
        {
            return false;
        }
    }
    return true;
}

/*
 * 函数名：A_GasControl_AllCylindersDisabled。
 * 说明：检查六只气瓶是否都由工作人员置于停用状态，作为外部通讯切换和Modbus整组提交的维护门槛。
 * 输入：system为只读气源系统状态指针。
 * 输出：六瓶全部停用时返回true，参数无效或任一气瓶未停用时返回false。
 */
static bool A_GasControl_AllCylindersDisabled(const Gas_System *system)
{
    uint8_t index; // 当前作用域变量，用于保存遍历索引。

    if (system == NULL)
    {
        return false;
    }
    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        if (system->cylinder[index].state != GAS_CYL_DISABLED)
        {
            return false;
        }
    }
    return true;
}

/*
 * 函数名：A_GasControl_InitExternalComm。
 * 说明：按照指定模式初始化CAN或SCI0/RS485外部通讯实例。
 * 输入：context为应用上下文；mode为待启用通讯模式。
 * 输出：目标通讯成功初始化时返回true，否则返回false。
 */
static bool A_GasControl_InitExternalComm(A_Gas_Control_Context *context,
                                          gas_external_comm_mode_t mode)
{
    if (mode == GAS_EXTERNAL_COMM_CAN)
    {
        if (!A_Can_Init(&context->external_can, &context->config))
        {
            context->system.alarm_bits |= GAS_ALARM_EXTERNAL_CAN;
            return false;
        }
        A_Can_UpdateLogInfo(&context->external_can,
                            A_GasLog_GetCount(&context->log_service),
                            A_GAS_LOG_RECORD_CAPACITY);
        context->system.alarm_bits &= ~(uint32_t) GAS_ALARM_EXTERNAL_CAN;
        return true;
    }
    if (mode == GAS_EXTERNAL_COMM_RS485)
    {
        if (!A_Modbus_Init(&context->external_modbus, &context->config))
        {
            context->system.alarm_bits |= GAS_ALARM_EXTERNAL_MODBUS;
            return false;
        }
        A_Modbus_UpdateLogInfo(&context->external_modbus,
                               A_GasLog_GetCount(&context->log_service),
                               A_GAS_LOG_RECORD_CAPACITY);
        context->system.alarm_bits &= ~(uint32_t) GAS_ALARM_EXTERNAL_MODBUS;
        return true;
    }
    return false;
}

/*
 * 函数名：A_GasControl_DeinitExternalComm。
 * 说明：关闭指定的CAN或SCI0/RS485外部通讯实例。
 * 输入：context为应用上下文；mode为待关闭通讯模式。
 * 输出：接口已经关闭或成功关闭时返回true，否则返回false。
 */
static bool A_GasControl_DeinitExternalComm(A_Gas_Control_Context *context,
                                            gas_external_comm_mode_t mode)
{
    return (mode == GAS_EXTERNAL_COMM_CAN) ? A_Can_Deinit(&context->external_can) :
           ((mode == GAS_EXTERNAL_COMM_RS485) ? A_Modbus_Deinit(&context->external_modbus) : false);
}

/*
 * 函数名：A_GasControl_TakeExternalConfig。
 * 说明：从当前外部通讯实例取得一份已经校验的参数提交请求。
 * 输入：context为应用上下文；config为参数输出指针。
 * 输出：存在待处理参数时返回true，否则返回false。
 */
static bool A_GasControl_TakeExternalConfig(A_Gas_Control_Context *context, Gas_Config *config)
{
    return (context->external_comm_mode == GAS_EXTERNAL_COMM_CAN) ?
           A_Can_TakeConfigRequest(&context->external_can, config) :
           A_Modbus_TakeConfigRequest(&context->external_modbus, config);
}

/*
 * 函数名：A_GasControl_UpdateExternalConfig。
 * 说明：把当前生效参数刷新到当前外部通讯实例。
 * 输入：context为应用上下文；config为只读生效参数。
 * 输出：参数镜像更新成功时返回true，否则返回false。
 */
static bool A_GasControl_UpdateExternalConfig(A_Gas_Control_Context *context,
                                              const Gas_Config *config)
{
    return (context->external_comm_mode == GAS_EXTERNAL_COMM_CAN) ?
           A_Can_UpdateConfig(&context->external_can, config) :
           A_Modbus_UpdateConfigRegisters(&context->external_modbus, config);
}

/*
 * 函数名：A_GasControl_SetExternalConfigResult。
 * 说明：向当前外部通讯实例公布参数应用结果。
 * 输入：context为应用上下文；result为公共参数处理结果。
 * 输出：无；结果写入当前通讯地址表。
 */
static void A_GasControl_SetExternalConfigResult(A_Gas_Control_Context *context,
                                                 gas_external_config_result_t result)
{
    if (context->external_comm_mode == GAS_EXTERNAL_COMM_CAN)
    {
        A_Can_SetConfigResult(&context->external_can, result);
    }
    else
    {
        A_Modbus_SetConfigResult(&context->external_modbus,
                                 (a_modbus_config_result_t) result);
    }
}

/*
 * 函数名：A_GasControl_TakeExternalLogRequest。
 * 说明：从当前外部通讯实例取得一条日志读取请求。
 * 输入：context为应用上下文；logical_index为日志序号输出指针。
 * 输出：存在待处理日志请求时返回true，否则返回false。
 */
static bool A_GasControl_TakeExternalLogRequest(A_Gas_Control_Context *context,
                                                uint16_t *logical_index)
{
    return (context->external_comm_mode == GAS_EXTERNAL_COMM_CAN) ?
           A_Can_TakeLogReadRequest(&context->external_can, logical_index) :
           A_Modbus_TakeLogReadRequest(&context->external_modbus, logical_index);
}

/*
 * 函数名：A_GasControl_SetExternalLogRecord。
 * 说明：把32字节原始日志写入当前外部通讯的数据窗口。
 * 输入：context为应用上下文；record为32字节只读日志。
 * 输出：数据窗口更新成功时返回true，否则返回false。
 */
static bool A_GasControl_SetExternalLogRecord(A_Gas_Control_Context *context,
                                              const uint8_t record[A_GAS_LOG_RECORD_SIZE])
{
    return (context->external_comm_mode == GAS_EXTERNAL_COMM_CAN) ?
           A_Can_SetLogRecord(&context->external_can, record) :
           A_Modbus_SetLogRecord(&context->external_modbus, record);
}

/*
 * 函数名：A_GasControl_SetExternalLogResult。
 * 说明：向当前外部通讯实例公布日志读取结果。
 * 输入：context为应用上下文；result为公共日志结果。
 * 输出：无；结果和必要的数据清理写入当前通讯实例。
 */
static void A_GasControl_SetExternalLogResult(A_Gas_Control_Context *context,
                                              gas_external_log_result_t result)
{
    if (context->external_comm_mode == GAS_EXTERNAL_COMM_CAN)
    {
        A_Can_SetLogReadResult(&context->external_can, result);
    }
    else
    {
        A_Modbus_SetLogReadResult(&context->external_modbus, (A_Modbus_Log_Result) result);
    }
}

/*
 * 函数名：A_GasControl_IsMechanicalSwitching。
 * 说明：判断自动切瓶是否正在执行实际关阀、无气死区、开阀或新阀稳定等待。
 * 输入：system为只读气源系统状态。
 * 输出：处于机械切换阶段时返回true，否则返回false。
 */
static bool A_GasControl_IsMechanicalSwitching(const Gas_System *system)
{
    if (system == NULL)
    {
        return false;
    }
    return ((system->switch_state == GAS_SWITCH_CLOSE_OLD) ||
            (system->switch_state == GAS_SWITCH_DEAD_TIME) ||
            (system->switch_state == GAS_SWITCH_OPEN_NEW) ||
            (system->switch_state == GAS_SWITCH_VERIFY_NEW));
}

/*
 * 函数名：A_GasControl_ProcessCanControl。
 * 说明：通过现有气源业务接口执行CAN单瓶排气、测试、停用和测试结论请求，并返回最终互锁结果。
 * 输入：context为气源控制应用上下文输入输出指针。
 * 输出：无；执行结果通过功能码6和CAN只读诊断地址返回，不直接绕过业务层操作GPIO。
 */
static void A_GasControl_ProcessCanControl(A_Gas_Control_Context *context)
{
    A_Can_Control_Request request; // 当前作用域变量，用于保存待处理请求。
    A_Can_Write_Result result = A_CAN_WRITE_EXECUTION_ERROR; // 当前作用域变量，用于保存操作结果。
    A_Can_Write_Detail detail = A_CAN_WRITE_DETAIL_STATE_DISALLOWED; // 当前作用域变量，用于保存队列尾位置。
    Gas_Cylinder *cylinder; // 当前作用域变量，用于保存气瓶对象或编号指针。
    bool success = false; // success 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示操作失败，true表示操作成功。

    if ((context == NULL) ||
        (context->external_comm_mode != GAS_EXTERNAL_COMM_CAN) ||
        !A_Can_TakeControlRequest(&context->external_can, &request))
    {
        return;
    }
    cylinder = &context->system.cylinder[request.index];

    if (!context->system.platform_ready)
    {
        detail = A_CAN_WRITE_DETAIL_HARDWARE_NOT_READY;
    }
    else if (A_GasControl_IsMechanicalSwitching(&context->system))
    {
        detail = A_CAN_WRITE_DETAIL_SWITCHING_BUSY;
    }
    else
    {
        switch (request.type)
        {
            case A_CAN_CONTROL_EXHAUST:
                success = request.enabled ?
                    A_GasControl_StartExhaust(context, request.index) :
                    A_GasControl_StopExhaust(context, request.index);
                if (success)
                {
                    A_Hmi_RequestExhaustSync(&context->hmi, request.index);
                }
                break;
            case A_CAN_CONTROL_TEST:
                success = A_GasControl_SetTestValve(context,
                                                    request.index,
                                                    request.enabled);
                A_Hmi_RequestTestSync(&context->hmi, request.index);
                break;
            case A_CAN_CONTROL_DISABLE:
                success = A_GasControl_SetCylinderDisabled(context,
                                                          request.index,
                                                          request.enabled);
                A_Hmi_RequestDisableSync(&context->hmi, request.index);
                break;
            case A_CAN_CONTROL_QUALIFICATION:
                success = A_GasControl_SetQualificationPassed(context,
                                                             request.index,
                                                             request.enabled);
                if (success)
                {
                    A_Hmi_RequestQualificationSync(&context->hmi, request.index);
                }
                break;
            default:
                success = false;
                break;
        }

        if (!success && request.enabled && cylinder->supply_cmd &&
            ((request.type == A_CAN_CONTROL_EXHAUST) ||
             (request.type == A_CAN_CONTROL_TEST)))
        {
            detail = A_CAN_WRITE_DETAIL_VALVE_INTERLOCK;
        }
        else if (!success &&
                 ((context->system.alarm_bits & GAS_ALARM_PLATFORM_NOT_READY) != 0U))
        {
            detail = A_CAN_WRITE_DETAIL_HARDWARE_NOT_READY;
        }
    }

    if (success)
    {
        result = A_CAN_WRITE_SUCCESS;
        detail = A_CAN_WRITE_DETAIL_NONE;
    }
    A_Can_CompleteControlRequest(&context->external_can, result, detail);
}

/*
 * 函数名：A_GasControl_ReclassifyPressureQuality。
 * 说明：参数生效后按新的压力合法上限立即重判现有有效样本，避免等待下一轮传感器采样才更新控制资格和显示颜色。
 * 输入：system为气源系统输入输出指针；config为已经生效的新运行参数。
 * 输出：无；仅在原质量为有效或超量程时更新六瓶及总管压力质量，失效和陈旧样本保持原状态。
 */
static void A_GasControl_ReclassifyPressureQuality(Gas_System *system,
                                                   const Gas_Config *config)
{
    uint8_t index; // 当前作用域变量，用于保存遍历索引。

    if ((system == NULL) || (config == NULL))
    {
        return;
    }
    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        gas_pressure_quality_t quality = system->cylinder[index].pressure_quality; // 当前作用域变量，用于保存当前处理数据。
        if ((quality == GAS_PRESSURE_VALID) || (quality == GAS_PRESSURE_OUT_OF_RANGE))
        {
            system->cylinder[index].pressure_quality =
                (system->cylinder[index].pressure_mpa > config->pressure_max_mpa) ?
                GAS_PRESSURE_OUT_OF_RANGE : GAS_PRESSURE_VALID;
        }
    }
    if ((system->total_pressure.pressure_quality == GAS_PRESSURE_VALID) ||
        (system->total_pressure.pressure_quality == GAS_PRESSURE_OUT_OF_RANGE))
    {
        system->total_pressure.pressure_quality =
            (system->total_pressure.pressure_mpa > config->pressure_max_mpa) ?
            GAS_PRESSURE_OUT_OF_RANGE : GAS_PRESSURE_VALID;
    }
}

/*
 * 函数名：A_GasControl_ProcessExternalConfig。
 * 说明：保存并应用外部通讯提交的运行参数；CAN单地址写直接生效，Modbus整组提交要求六瓶全部停用。
 * 输入：context 为气源控制应用上下文输入输出指针。
 * 输出：无；参数处理结果写入 Modbus，并在成功时同步更新 EEPROM 和参数保持寄存器。
 */
static void A_GasControl_ProcessExternalConfig(A_Gas_Control_Context *context)
{
    Gas_Config candidate; // 当前作用域变量，用于保存待校验候选值。

    if ((context == NULL) ||
        !A_GasControl_TakeExternalConfig(context, &candidate))
    {
        return;
    }

    if ((context->external_comm_mode == GAS_EXTERNAL_COMM_RS485) &&
        (!A_GasControl_AllCylindersDisabled(&context->system) ||
         !A_GasControl_AllValveCommandsAreOff(&context->system)))
    {
        (void) A_GasControl_UpdateExternalConfig(context, &context->config);
        A_GasControl_SetExternalConfigResult(context, GAS_EXTERNAL_CONFIG_SYSTEM_BUSY);
        return;
    }

    if (!A_GasConfig_Save(&context->storage_service, &candidate))
    {
        context->system.alarm_bits |= GAS_ALARM_STORAGE;
        (void) A_GasControl_UpdateExternalConfig(context, &context->config);
        A_GasControl_SetExternalConfigResult(context, GAS_EXTERNAL_CONFIG_STORAGE_FAILED);
        return;
    }

    context->config = candidate;
    A_GasControl_ReclassifyPressureQuality(&context->system, &context->config);
    if ((context->system.switch_state == GAS_SWITCH_LOW_CONFIRM) ||
        (context->system.switch_state == GAS_SWITCH_NO_BACKUP))
    {
        context->system.switch_state = GAS_SWITCH_IDLE;
        context->system.low_sample_count = 0U;
        context->system.low_last_sample_ms = 0U;
        context->system.low_start_ms = 0U;
        // CAN稳定运行中修改阈值后重新开始尚未进入机械动作的低压判断，已经完成的阀门动作不会被追溯修改。
    }
    context->system.alarm_bits &= ~(uint32_t) GAS_ALARM_STORAGE;
    (void) A_GasControl_UpdateExternalConfig(context, &context->config);
    A_GasControl_SetExternalConfigResult(context, GAS_EXTERNAL_CONFIG_SUCCESS);
}

/*
 * 函数名：A_GasControl_ProcessHmiConfig。
 * 说明：校验已由人员二次确认的完整参数，写入EEPROM成功后在运行中原子应用并同步外部10项镜像。
 * 输入：context为气源控制应用上下文输入输出指针。
 * 输出：无；处理结果通过串口屏参数模块显示，成功时更新EEPROM和当前运行参数。
 */
static void A_GasControl_ProcessHmiConfig(A_Gas_Control_Context *context)
{
    Gas_Config candidate; // 当前作用域变量，用于保存待校验候选值。
    A_Gas_Config_Validation validation; // 当前作用域变量，用于保存参数校验结果。

    if ((context == NULL) ||
        !A_HmiConfig_TakeSaveRequest(&context->hmi_config, &candidate))
    {
        return;
    }

    validation = A_GasConfig_Validate(&candidate);
    if (validation != A_GAS_CONFIG_VALID)
    {
        A_HmiConfig_ReportResult(&context->hmi_config,
            (validation == A_GAS_CONFIG_INVALID_RELATION) ?
            A_HMI_CONFIG_RESULT_INVALID_RELATION : A_HMI_CONFIG_RESULT_INVALID_RANGE,
            &context->config);
        return;
    }
    if (!A_GasConfig_Save(&context->storage_service, &candidate))
    {
        context->system.alarm_bits |= GAS_ALARM_STORAGE;
        A_HmiConfig_ReportResult(&context->hmi_config,
                                 A_HMI_CONFIG_RESULT_STORAGE_FAILED,
                                 &context->config);
        return;
    }

    context->config = candidate;
    A_GasControl_ReclassifyPressureQuality(&context->system, &context->config);
    if ((context->system.switch_state == GAS_SWITCH_LOW_CONFIRM) ||
        (context->system.switch_state == GAS_SWITCH_NO_BACKUP))
    {
        context->system.switch_state = GAS_SWITCH_IDLE;
        context->system.low_sample_count = 0U;
        context->system.low_last_sample_ms = 0U;
        context->system.low_start_ms = 0U;
        // 尚未进入机械切瓶的低压确认按新阈值重新开始，已关旧阀或开新阀的过程不被中途打断。
    }
    context->system.alarm_bits &= ~(uint32_t) GAS_ALARM_STORAGE;
    (void) A_GasControl_UpdateExternalConfig(context, &context->config);
    A_HmiConfig_ReportResult(&context->hmi_config,
                             A_HMI_CONFIG_RESULT_SUCCESS,
                             &context->config);
}

/*
 * 函数名：A_GasControl_ProcessHmiLogClear。
 * 说明：接收密码参数页二次确认的日志清除请求，驱动EEPROM逐页状态机并反馈进度和结果。
 * 输入：context为气源控制应用上下文输入输出指针。
 * 输出：无；日志查询缓存、存储报警和Screen7动态文本按最终执行结果更新。
 */
static void A_GasControl_ProcessHmiLogClear(A_Gas_Control_Context *context)
{
    A_Gas_Log_Clear_Result result; // 当前作用域变量，用于保存操作结果。

    if (context == NULL)
    {
        return;
    }
    if (A_HmiConfig_TakeLogClearRequest(&context->hmi_config))
    {
        if (!A_GasLog_RequestClear(&context->log_service))
        {
            context->system.alarm_bits |= GAS_ALARM_STORAGE;
            A_HmiConfig_ReportLogClear(&context->hmi_config,
                                       A_HMI_LOG_CLEAR_FAILED,
                                       0U,
                                       A_GasLog_GetCount(&context->log_service));
            return;
        }
        A_HmiLog_InvalidateCache(&context->hmi_log);
        // 清除请求一经接受立即废弃RAM分页索引，防止后续使用指向旧物理日志的逻辑位置。
    }

    A_GasLog_ClearTask(&context->log_service, &context->system);
    result = A_GasLog_GetClearResult(&context->log_service);
    if (result == A_GAS_LOG_CLEAR_RESULT_BUSY)
    {
        A_HmiConfig_ReportLogClear(&context->hmi_config,
                                   A_HMI_LOG_CLEAR_BUSY,
                                   A_GasLog_GetClearProgress(&context->log_service),
                                   0U);
    }
    else if ((context->hmi_config.log_clear_status == A_HMI_LOG_CLEAR_BUSY) &&
             (result == A_GAS_LOG_CLEAR_RESULT_SUCCESS))
    {
        context->system.alarm_bits &= ~(uint32_t) GAS_ALARM_STORAGE;
        A_HmiConfig_ReportLogClear(&context->hmi_config,
                                   A_HMI_LOG_CLEAR_SUCCESS,
                                   100U,
                                   A_GasLog_GetCount(&context->log_service));
    }
    else if ((context->hmi_config.log_clear_status == A_HMI_LOG_CLEAR_BUSY) &&
             (result == A_GAS_LOG_CLEAR_RESULT_FAILED))
    {
        context->system.alarm_bits |= GAS_ALARM_STORAGE;
        A_HmiConfig_ReportLogClear(&context->hmi_config,
                                   A_HMI_LOG_CLEAR_FAILED,
                                   A_GasLog_GetClearProgress(&context->log_service),
                                   A_GasLog_GetCount(&context->log_service));
    }
}

/*
 * 函数名：A_GasControl_ProcessExternalLogRead。
 * 说明：执行当前外部通讯提交的日志逻辑序号读取请求，并把32字节记录写入通信数据窗口。
 * 输入：context 为气源控制应用上下文输入输出指针。
 * 输出：无；日志数量、数据窗口和读取结果通过外部Modbus保持寄存器更新。
 */
static void A_GasControl_ProcessExternalLogRead(A_Gas_Control_Context *context)
{
    uint8_t record[A_GAS_LOG_RECORD_SIZE]; // 当前作用域变量，用于保存日志或配置记录缓冲区。
    uint16_t logical_index; // 当前作用域变量，用于保存日志逻辑索引。
    uint16_t valid_count; // 当前作用域变量，用于保存数量计数。

    if ((context == NULL) ||
        !A_GasControl_TakeExternalLogRequest(context, &logical_index))
    {
        return;
    }

    if (A_GasLog_IsClearBusy(&context->log_service))
    {
        A_GasControl_SetExternalLogResult(context, GAS_EXTERNAL_LOG_BUSY);
        return;
    }
    if (!A_GasLog_IsReady(&context->log_service))
    {
        context->system.alarm_bits |= GAS_ALARM_STORAGE;
        A_GasControl_SetExternalLogResult(context, GAS_EXTERNAL_LOG_READ_FAILED);
        return;
    }

    valid_count = A_GasLog_GetCount(&context->log_service);
    if (logical_index >= valid_count)
    {
        A_GasControl_SetExternalLogResult(context, GAS_EXTERNAL_LOG_INVALID_INDEX);
        return;
    }
    if (!A_GasLog_ReadRecord(&context->log_service, logical_index, record) ||
        !A_GasControl_SetExternalLogRecord(context, record))
    {
        context->system.alarm_bits |= GAS_ALARM_STORAGE;
        A_GasControl_SetExternalLogResult(context, GAS_EXTERNAL_LOG_READ_FAILED);
        return;
    }

    A_GasControl_SetExternalLogResult(context, GAS_EXTERNAL_LOG_SUCCESS);
}

/*
 * 函数名：A_GasControl_Init。
 * 说明：初始化六瓶状态机、三阀控制、内部轮询、默认CAN外部通讯、串口屏和EEPROM存储服务。
 * 输入：context 为待初始化的气源控制应用上下文指针。
 * 输出：无；通过 context 输出系统及各功能模块的完整初始状态。
 */
void A_GasControl_Init(A_Gas_Control_Context *context)
{
    Gas_System *system; // 当前作用域变量，用于保存气源系统对象指针。
    uint32_t now_ms; // 当前作用域变量，用于保存当前毫秒时刻。
    uint8_t index; // 当前作用域变量，用于保存遍历索引。
    bool sensor_ready; // sensor_ready 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。
    bool comm_record_valid = false; // comm_record_valid 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无效，true表示有效。

    if (context == NULL)
    {
        return;
    }

    (void) memset(context, 0, sizeof(*context));
    A_GasConfig_LoadDefaults(&context->config);
    context->external_comm_mode = (gas_external_comm_mode_t) GAS_DEFAULT_EXTERNAL_COMM_MODE;
    system = &context->system;
    system->mode = GAS_MODE_AUTO;
    system->active_index = GAS_NO_ACTIVE_CYLINDER;
    system->switch_old_index = GAS_NO_ACTIVE_CYLINDER;
    system->switch_new_index = GAS_NO_ACTIVE_CYLINDER;
    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        system->cylinder[index].state = GAS_CYL_INIT;
        system->cylinder[index].qualification_passed = false;
    }
    // 先建立安全的软件初态；只有后续平台、传感器和阀门初始化成功才允许真实输出。

    system->platform_ready = F_GasRuntime_Init(&context->runtime_service);
    now_ms = F_GasRuntime_Millis(&context->runtime_service);
    system->switch_enter_ms = now_ms;
    if (!system->platform_ready)
    {
        system->alarm_bits |= GAS_ALARM_PLATFORM_NOT_READY;
    }
    F_ValveControl_Init(&context->runtime_service.platform, system);
    // 阀门初始化在任何通信和存储业务之前执行，保证上电期间十八路分瓶阀和总测试阀保持关闭。

    sensor_ready = A_ModbusPoll_Init(&context->sensor_poll,
                                     &context->runtime_service.platform,
                                     system);
    if (!sensor_ready)
    {
        system->platform_ready = false;
        system->alarm_bits |= GAS_ALARM_SENSOR_COMM | GAS_ALARM_PLATFORM_NOT_READY;
    }

    if (A_Storage_Init(&context->storage_service))
    {
        Gas_Config stored_config; // 当前作用域变量，用于保存运行参数。

        comm_record_valid = A_GasControl_LoadCommMode(&context->storage_service,
                                                      &context->external_comm_mode);
        // 通讯模式必须先于参数加载；V2参数自动升级会占用旧模式记录所在的0x0020～0x0023。
        if (A_GasConfig_Load(&context->storage_service, &stored_config))
        {
            context->config = stored_config;
            // EEPROM记录只有标识、版本、CRC和参数关系全部有效时才覆盖编译期默认值。
        }
        else if (!A_GasConfig_Save(&context->storage_service, &context->config))
        {
            system->alarm_bits |= GAS_ALARM_STORAGE;
        }
        if (!A_GasLog_Init(&context->log_service, &context->storage_service, system))
        {
            system->alarm_bits |= GAS_ALARM_STORAGE;
        }
        // 参数区和日志区使用不同页地址，共享同一个已经初始化的软件IIC存储实例。
    }
    else
    {
        system->alarm_bits |= GAS_ALARM_STORAGE;
    }

    if (A_GasControl_InitExternalComm(context, context->external_comm_mode) &&
        context->storage_service.ready && !comm_record_valid &&
        !A_GasControl_SaveCommMode(&context->storage_service, context->external_comm_mode))
    {
        system->alarm_bits |= GAS_ALARM_STORAGE;
    }
    // EEPROM中没有有效模式记录时使用默认CAN；接口初始化成功后补写记录，避免保存一个不可用模式。
    if (!A_Hmi_Init(&context->hmi))
    {
        system->alarm_bits |= GAS_ALARM_HMI_COMM;
    }
    if (!A_HmiConfig_Init(&context->hmi_config, &context->hmi))
    {
        system->alarm_bits |= GAS_ALARM_HMI_COMM;
    }
    if (!A_HmiLog_Init(&context->hmi_log,
                       &context->hmi,
                       &context->log_service,
                       &context->system))
    {
        system->alarm_bits |= GAS_ALARM_HMI_COMM;
    }
}

/*
 * 函数名：A_GasControl_SetExternalCommMode。
 * 说明：在六瓶全部停用且十九路阀门关闭时切换CAN或RS485外部通讯，并把成功模式保存到EEPROM。
 * 输入：context为应用上下文；mode为目标通讯模式，0表示CAN、1表示RS485/Modbus。
 * 输出：目标接口成功启用并完成持久化时返回true；运行中、初始化失败或存储失败时返回false并恢复原模式。
 */
bool A_GasControl_SetExternalCommMode(A_Gas_Control_Context *context,
                                      gas_external_comm_mode_t mode)
{
    gas_external_comm_mode_t previous_mode; // 当前作用域变量，用于保存工作模式。

    if ((context == NULL) || (mode > GAS_EXTERNAL_COMM_RS485) ||
        !A_GasControl_AllCylindersDisabled(&context->system) ||
        !A_GasControl_AllValveCommandsAreOff(&context->system))
    {
        return false;
    }
    if (mode == context->external_comm_mode)
    {
        return (mode == GAS_EXTERNAL_COMM_CAN) ? A_Can_IsReady(&context->external_can) :
                                                A_Modbus_IsReady(&context->external_modbus);
    }

    previous_mode = context->external_comm_mode;
    if (!A_GasControl_DeinitExternalComm(context, previous_mode) ||
        !A_GasControl_InitExternalComm(context, mode))
    {
        (void) A_GasControl_DeinitExternalComm(context, mode);
        (void) A_GasControl_InitExternalComm(context, previous_mode);
        context->external_comm_mode = previous_mode;
        return false;
    }

    context->external_comm_mode = mode;
    if (!context->storage_service.ready ||
        !A_GasControl_SaveCommMode(&context->storage_service, mode))
    {
        (void) A_GasControl_DeinitExternalComm(context, mode);
        context->external_comm_mode = previous_mode;
        (void) A_GasControl_SaveCommMode(&context->storage_service, previous_mode);
        // 目标模式写入或读回失败时尽力重写原模式，避免下次上电误选尚未确认成功的接口。
        (void) A_GasControl_InitExternalComm(context, previous_mode);
        context->system.alarm_bits |= GAS_ALARM_STORAGE;
        return false;
    }
    context->system.alarm_bits &= ~(uint32_t) GAS_ALARM_STORAGE;
    return true;
}

/*
 * 函数名：A_GasControl_StartExhaust。
 * 说明：在允许状态打开指定排气阀并设置独立截止时间；已在排气时保持原截止时间且允许测试阀同时开启。
 * 输入：context 为应用上下文；index 为从 0 开始的气瓶索引。
 * 输出：状态、运行模式和供气互锁允许且开阀成功时返回 true，否则返回 false。
 */
bool A_GasControl_StartExhaust(A_Gas_Control_Context *context, uint8_t index)
{
    uint32_t now_ms; // 当前作用域变量，用于保存当前毫秒时刻。

    if ((context == NULL) || (index >= GAS_CYLINDER_COUNT))
    {
        return false;
    }
    if (context->system.cylinder[index].exhaust_cmd)
    {
        return true;
        // 排气中重复点击不重新计算截止时间，防止误触延长一次人工排气过程。
    }
    if ((context->system.mode != GAS_MODE_AUTO) ||
        ((context->system.cylinder[index].state != GAS_CYL_INIT) &&
         (context->system.cylinder[index].state != GAS_CYL_WAIT_TEST) &&
         (context->system.cylinder[index].state != GAS_CYL_READY) &&
         (context->system.cylinder[index].state != GAS_CYL_LOW_REPLACE)))
    {
        return false;
    }

    now_ms = F_GasRuntime_Millis(&context->runtime_service);
    if (!F_ValveControl_SetExhaust(&context->runtime_service.platform,
                                   &context->system,
                                   &context->config,
                                   index,
                                   true))
    {
        return false;
    }

    context->system.cylinder[index].exhaust_deadline_ms =
        now_ms + context->config.manual_exhaust_time_ms;
    return true;
}

/*
 * 函数名：A_GasControl_StopExhaust。
 * 说明：立即关闭指定气瓶排气阀并取消本次人工排气截止时间，供CAN远程关闭操作使用。
 * 输入：context为应用上下文；index为从0开始的气瓶索引。
 * 输出：排气阀已经关闭或成功关闭时返回true，参数或硬件无效时返回false。
 */
bool A_GasControl_StopExhaust(A_Gas_Control_Context *context, uint8_t index)
{
    if ((context == NULL) || (index >= GAS_CYLINDER_COUNT))
    {
        return false;
    }
    if (!context->system.cylinder[index].exhaust_cmd)
    {
        context->system.cylinder[index].exhaust_deadline_ms = 0U;
        A_Hmi_RequestExhaustSync(&context->hmi, index);
        return true;
    }
    if (!F_ValveControl_SetExhaust(&context->runtime_service.platform,
                                   &context->system,
                                   &context->config,
                                   index,
                                   false))
    {
        return false;
    }
    context->system.cylinder[index].exhaust_deadline_ms = 0U;
    A_Hmi_RequestExhaustSync(&context->hmi, index);
    // 关闭操作不受气瓶状态限制，成功后立即同步屏幕按钮并取消原自动关闭计时。
    return true;
}

/*
 * 函数名：A_GasControl_SetTestValve。
 * 说明：按照串口屏开关状态打开或关闭指定测试阀，开启后按独立上限自动关闭；允许排气阀同时开启。
 * 输入：context 为应用上下文；index 为气瓶索引；on 为目标开关状态。
 * 输出：状态、运行模式和供气互锁允许且命令成功时返回 true，否则返回 false。
 */
bool A_GasControl_SetTestValve(A_Gas_Control_Context *context, uint8_t index, bool on)
{
    uint32_t now_ms; // 当前作用域变量，用于保存当前毫秒时刻。
    uint8_t bit; // 当前作用域变量，用于保存位掩码。

    if ((context == NULL) || (index >= GAS_CYLINDER_COUNT))
    {
        return false;
    }
    bit = (uint8_t) (1U << index);
    if (on && ((context->system.mode != GAS_MODE_AUTO) ||
        ((context->system.cylinder[index].state != GAS_CYL_INIT) &&
         (context->system.cylinder[index].state != GAS_CYL_WAIT_TEST) &&
         (context->system.cylinder[index].state != GAS_CYL_READY) &&
         (context->system.cylinder[index].state != GAS_CYL_LOW_REPLACE))))
    {
        return false;
    }

    if (on)
    {
        if (context->system.cylinder[index].test_cmd ||
            ((context->total_test.pending_open_mask & bit) != 0U))
        {
            return true;
        }

        now_ms = F_GasRuntime_Millis(&context->runtime_service);
        context->total_test.pending_open_mask |= bit;
        context->total_test.open_not_before_ms[index] = context->system.total_test_cmd ?
            now_ms + GAS_VALVE_BOOST_MIN_INTERVAL_MS : 0U;
        context->system.cylinder[index].test_deadline_ms = 0U;
        // 周期任务会先等待VALP1+可用并吸合总阀，再延时500 ms真正打开分路测试阀。
        return true;
    }

    context->total_test.pending_open_mask &= (uint8_t) ~bit;
    context->total_test.open_not_before_ms[index] = 0U;
    if (context->system.cylinder[index].test_cmd &&
        !F_ValveControl_SetTest(&context->runtime_service.platform,
                                &context->system,
                                &context->config,
                                index,
                                false))
    {
        return false;
    }
    context->system.cylinder[index].test_deadline_ms = 0U;

    if (!A_GasControl_AnyTestValveOn(&context->system) &&
        (context->total_test.pending_open_mask == 0U) &&
        context->system.total_test_cmd &&
        !F_ValveControl_SetTotalTest(&context->runtime_service.platform,
                                     &context->system,
                                     &context->config,
                                     false))
    {
        return false;
    }
    // 分路先关闭；仅当没有其他实际或等待中的测试阀时才关闭VAL_CAL总测试阀。
    return true;
}

/*
 * 函数名：A_GasControl_SetCylinderDisabled。
 * 说明：设置或解除指定气瓶停用状态，停用时立即关闭该瓶三只电磁阀并退出自动选择。
 * 输入：context 为应用上下文；index 为气瓶索引；disabled 为目标停用状态。
 * 输出：状态允许且操作完成时返回 true，否则返回 false。
 */
bool A_GasControl_SetCylinderDisabled(A_Gas_Control_Context *context,
                                      uint8_t index,
                                      bool disabled)
{
    Gas_System *system; // 当前作用域变量，用于保存气源系统对象指针。
    Gas_Cylinder *cylinder; // 当前作用域变量，用于保存气瓶对象或编号指针。

    if ((context == NULL) || (index >= GAS_CYLINDER_COUNT))
    {
        return false;
    }
    system = &context->system;
    cylinder = &system->cylinder[index];

    if (disabled)
    {
        if (cylinder->state == GAS_CYL_DISABLED)
        {
            return true;
        }
        if (cylinder->state == GAS_CYL_INIT)
        {
            return false;
        }
        if (!A_GasControl_CloseCylinderValves(context, index))
        {
            return false;
        }

        cylinder->state = GAS_CYL_DISABLED;
        cylinder->qualification_passed = false;
        cylinder->recovery_sample_count = 0U;
        A_Hmi_RequestQualificationSync(&context->hmi, index);
        // 停用通常用于维护或换瓶，因此同时撤销原测试结论，重新启用后必须再次测试。
        if ((system->active_index == index) ||
            (system->switch_old_index == index) ||
            (system->switch_new_index == index))
        {
            system->switch_old_index = index;
            system->switch_new_index = GAS_NO_ACTIVE_CYLINDER;
            system->active_index = GAS_NO_ACTIVE_CYLINDER;
            system->switch_state = GAS_SWITCH_IDLE;
            system->low_sample_count = 0U;
        }
        return true;
    }

    if (cylinder->state != GAS_CYL_DISABLED)
    {
        return true;
    }
    cylinder->state = GAS_CYL_INIT;
    cylinder->exhaust_deadline_ms = 0U;
    cylinder->test_deadline_ms = 0U;
    return true;
}

/*
 * 函数名：A_GasControl_SetQualificationPassed。
 * 说明：设置指定气瓶由人员确认的测试合格标志；只有待测试且压力合格时允许确认通过，待用状态允许撤销。
 * 输入：context 为应用上下文；index 为气瓶索引；passed 为目标测试结果，true表示通过、false表示不通过。
 * 输出：参数有效并完成设置时返回 true，否则返回 false。
 */
bool A_GasControl_SetQualificationPassed(A_Gas_Control_Context *context,
                                         uint8_t index,
                                         bool passed)
{
    Gas_Cylinder *cylinder; // 当前作用域变量，用于保存气瓶对象或编号指针。
    uint32_t now_ms; // 当前作用域变量，用于保存当前毫秒时刻。

    if ((context == NULL) || (index >= GAS_CYLINDER_COUNT))
    {
        return false;
    }

    cylinder = &context->system.cylinder[index];
    if (passed)
    {
        now_ms = F_GasRuntime_Millis(&context->runtime_service);
        if ((cylinder->state != GAS_CYL_WAIT_TEST) ||
            !A_GasControl_PressureIsFresh(cylinder, &context->config, now_ms) ||
            (cylinder->pressure_mpa < context->config.ready_min_pressure_mpa))
        {
            return false;
        }
    }
    else if (((cylinder->state == GAS_CYL_ACTIVE) ||
              (cylinder->state == GAS_CYL_LOW_WARNING)) &&
             cylinder->qualification_passed)
    {
        return false;
    }

    cylinder->qualification_passed = passed;
    return true;
}

/*
 * 函数名：A_GasControl_Task。
 * 说明：周期执行压力轮询、七状态判断、三阀计时、自动切瓶、日志、串口屏和当前外部通讯。
 * 输入：context 为气源控制应用上下文输入输出指针。
 * 输出：无；通过 context 输出本周期处理后的系统状态。
 */
void A_GasControl_Task(A_Gas_Control_Context *context)
{
    uint32_t now_ms; // 当前作用域变量，用于保存当前毫秒时刻。

    if (context == NULL)
    {
        return;
    }

    now_ms = F_GasRuntime_Millis(&context->runtime_service);
    A_ModbusPoll_Task(&context->sensor_poll, &context->system, &context->config, now_ms);
    F_ValveControl_Task(&context->runtime_service.platform, &context->system, now_ms);
    A_Hmi_Task(&context->hmi, &context->system, now_ms);
    A_HmiLog_InputTask(&context->hmi_log);
    A_HmiConfig_InputTask(&context->hmi_config,
                          &context->config);
    A_GasControl_ProcessHmiButton(context);
    // 先提交本周期收到的文本输入，再处理查询或确认按钮，保证按钮建立的快照使用最后一次输入值。
    A_GasControl_ProcessHmiConfig(context);
    A_GasControl_ProcessHmiLogClear(context);
    A_GasControl_ManualValveTask(context, now_ms);
    A_GasControl_UpdateCylinderStates(context, now_ms);
    A_GasControl_SwitchTask(context, now_ms);
    A_GasControl_TotalTestTask(context, now_ms);
    A_GasControl_CheckOutputInvariant(context);
    // 状态机处理完成后统一检查硬安全不变量，日志只记录经过安全检查后的最终状态。
    if (!A_GasLog_IsClearBusy(&context->log_service) &&
        A_GasLog_IsReady(&context->log_service) &&
        !A_GasLog_Task(&context->log_service, &context->system))
    {
        context->system.alarm_bits |= GAS_ALARM_STORAGE;
    }
    A_HmiConfig_Task(&context->hmi_config);
    A_HmiLog_Task(&context->hmi_log);
    // 参数刷新先尝试占用SCI9；无待发参数时日志和监控继续运行，避免返回事件丢失后长期暂停刷新。
    if (context->external_comm_mode == GAS_EXTERNAL_COMM_CAN)
    {
        A_Can_UpdateLogInfo(&context->external_can,
                            A_GasLog_GetCount(&context->log_service),
                            A_GAS_LOG_RECORD_CAPACITY);
        A_Can_Task(&context->external_can,
                   &context->system,
                   context->external_comm_mode);
        if (A_Can_HasFault(&context->external_can))
        {
            context->system.alarm_bits |= GAS_ALARM_EXTERNAL_CAN;
        }
        else
        {
            context->system.alarm_bits &= ~(uint32_t) GAS_ALARM_EXTERNAL_CAN;
        }
    }
    else
    {
        A_Modbus_UpdateLogInfo(&context->external_modbus,
                               A_GasLog_GetCount(&context->log_service),
                               A_GAS_LOG_RECORD_CAPACITY);
        A_Modbus_Refresh(&context->external_modbus, &context->system);
        A_Modbus_Task(&context->external_modbus);
        if (A_Modbus_HasFault(&context->external_modbus))
        {
            context->system.alarm_bits |= GAS_ALARM_EXTERNAL_MODBUS;
        }
        else
        {
            context->system.alarm_bits &= ~(uint32_t) GAS_ALARM_EXTERNAL_MODBUS;
        }
    }
    A_GasControl_ProcessCanControl(context);
    A_GasControl_ProcessExternalConfig(context);
    A_GasControl_ProcessExternalLogRead(context);
    // 协议层先解析一帧请求，再由气源应用层执行控制、参数持久化或EEPROM日志读取。
    if (!A_HmiLog_IsBusy(&context->hmi_log))
    {
        A_Hmi_Refresh(&context->hmi, &context->system, now_ms);
    }
    // 查询期间暂停监控控件轮询刷新，把SCI9带宽优先让给日志清表和逐行发送。
    F_GasRuntime_Idle(&context->runtime_service);
}

/*
 * 函数名：A_GasControl_GetSystem。
 * 说明：获取指定应用实例中的只读气源系统状态。
 * 输入：context 为只读气源控制应用上下文指针。
 * 输出：返回只读系统状态指针；context 为 NULL 时返回 NULL。
 */
const Gas_System *A_GasControl_GetSystem(const A_Gas_Control_Context *context)
{
    return (context != NULL) ? &context->system : NULL;
}
