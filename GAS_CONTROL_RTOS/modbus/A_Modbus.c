/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现外部Modbus状态、参数、控制命令和日志寄存器映射。
 */

// 本文件实现 SCI0/RS485 外部Modbus从站的状态、参数和日志读取映射。
#include "A_Modbus.h"

#include <stddef.h>
#include <string.h>

#include "../MyUnitFile/A_Gas_Config.h"

/*
 * 函数名：A_Modbus_FloatToRegisters。
 * 说明：将 IEEE-754 float32 转换为 Modbus 的 AB CD 两个连续寄存器。
 * 输入：value 为浮点数；high_register 为高 16 位寄存器输出指针；low_register 为低 16 位寄存器输出指针。
 * 输出：无；转换结果分别通过 high_register 和 low_register 输出。
 */
static void A_Modbus_FloatToRegisters(float value,
                             uint16_t *high_register,
                             uint16_t *low_register)
{
    uint32_t raw_value; // 当前作用域变量，用于保存当前处理值。

    memcpy(&raw_value, &value, sizeof(raw_value));
    *high_register = (uint16_t) (raw_value >> 16U);
    *low_register = (uint16_t) raw_value;
}

/*
 * 函数名：A_Modbus_WriteEventContainsRegister。
 * 说明：判断一次保持寄存器写入范围是否包含指定寄存器偏移。
 * 输入：start_offset 为写入起始偏移；register_count 为连续写入数量；target_offset 为目标偏移。
 * 输出：写入范围包含目标寄存器时返回 true，否则返回 false。
 */
static bool A_Modbus_WriteEventContainsRegister(uint16_t start_offset,
                                                uint16_t register_count,
                                                uint16_t target_offset)
{
    return ((register_count != 0U) &&
            (start_offset <= target_offset) &&
            ((uint32_t) start_offset + register_count > target_offset));
}

/*
 * 函数名：A_Modbus_PressureToRegister。
 * 说明：把 MPa 压力转换为外部 Modbus 使用的乘以 1000 定点数。
 * 输入：pressure_mpa 为待编码压力，单位 MPa。
 * 输出：返回四舍五入后的无符号 16 位寄存器值。
 */
static uint16_t A_Modbus_PressureToRegister(float pressure_mpa)
{
    return (uint16_t) ((pressure_mpa * GAS_CONFIG_PRESSURE_SCALE) + 0.5F);
}

/*
 * 函数名：A_Modbus_ReadConfigRegisters。
 * 说明：按照 0x0106～0x010F 的定义把参数保持寄存器解码为运行参数结构体。
 * 输入：context 为只读外部 Modbus 上下文；config 为运行参数输出指针。
 * 输出：全部寄存器读取成功时返回 true，否则返回 false。
 */
static bool A_Modbus_ReadConfigRegisters(const A_Modbus_Context *context, Gas_Config *config)
{
    uint16_t value[A_GAS_CONFIG_REGISTER_COUNT]; // 当前作用域变量，用于保存当前处理值数组。
    uint16_t index; // 当前作用域变量，用于保存遍历索引。

    if ((context == NULL) || (config == NULL))
    {
        return false;
    }

    *config = context->current_config;
    // 先复制完整参数镜像，再覆盖外部开放的10项，确保HMI专属三项不会被未初始化数据替换。

    for (index = 0U; index < A_GAS_CONFIG_REGISTER_COUNT; ++index)
    {
        if (!F_Modbus_GetHoldingRegister(&context->function,
                                         (uint16_t) (A_MODBUS_HOLDING_CONFIG_BASE + index),
                                         &value[index]))
        {
            return false;
        }
    }

    config->switch_pressure_mpa = (float) value[A_MODBUS_CONFIG_SWITCH_PRESSURE] /
                                  GAS_CONFIG_PRESSURE_SCALE;
    config->switch_release_mpa = (float) value[A_MODBUS_CONFIG_SWITCH_RELEASE] /
                                 GAS_CONFIG_PRESSURE_SCALE;
    config->valve_pull_in_time_ms = value[A_MODBUS_CONFIG_VALVE_PULL_IN_TIME];
    config->ready_min_pressure_mpa = (float) value[A_MODBUS_CONFIG_READY_MIN_PRESSURE] /
                                     GAS_CONFIG_PRESSURE_SCALE;
    config->pressure_max_mpa = (float) value[A_MODBUS_CONFIG_PRESSURE_MAX] /
                               GAS_CONFIG_PRESSURE_SCALE;
    config->low_confirm_time_ms = value[A_MODBUS_CONFIG_LOW_CONFIRM_TIME];
    config->low_confirm_samples = value[A_MODBUS_CONFIG_LOW_CONFIRM_SAMPLES];
    config->valve_close_wait_ms = value[A_MODBUS_CONFIG_VALVE_CLOSE_WAIT];
    config->valve_open_wait_ms = value[A_MODBUS_CONFIG_VALVE_OPEN_WAIT];
    config->pressure_fresh_ms = value[A_MODBUS_CONFIG_PRESSURE_FRESH];
    return true;
}

/*
 * 函数名：A_Modbus_Init。
 * 说明：初始化外部 SCI0 Modbus 从站及应用寄存器表。
 * 输入：context 为待初始化的外部 Modbus 应用层上下文；config 为当前有效运行参数。
 * 输出：全部层级初始化成功时返回 true，否则返回 false。
 */
bool A_Modbus_Init(A_Modbus_Context *context, const Gas_Config *config)
{
    if ((context == NULL) || (config == NULL))
    {
        return false;
    }

    memset(context, 0, sizeof(*context));
    if (!F_Modbus_Init(&context->function, &context->hardware, A_MODBUS_SLAVE_ADDRESS))
    {
        return false;
    }

    context->ready = true;
    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_RESULT,
                                       A_MODBUS_RESULT_IDLE);
    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_CONFIG_RESULT,
                                       A_MODBUS_CONFIG_RESULT_IDLE);
    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_CONFIG_VERSION,
                                       A_MODBUS_CONFIG_VERSION_VALUE);
    A_Modbus_UpdateLogInfo(context, 0U, 0U);
    A_Modbus_SetLogReadResult(context, A_MODBUS_LOG_RESULT_IDLE);
    if (!A_Modbus_UpdateConfigRegisters(context, config))
    {
        context->ready = false;
        return false;
    }
    // 先发布固定版本和空闲状态，再填入经过校验的运行参数，避免主站读取到未初始化映射。
    return true;
}

/*
 * 函数名：A_Modbus_Deinit。
 * 说明：关闭外部SCI0/RS485 Modbus并清除应用层就绪状态。
 * 输入：context为外部Modbus应用层上下文输入输出指针。
 * 输出：接口已经关闭或成功关闭时返回true，否则返回false。
 */
bool A_Modbus_Deinit(A_Modbus_Context *context)
{
    bool success; // success 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示操作失败，true表示操作成功。

    if (context == NULL)
    {
        return false;
    }
    success = F_Modbus_Deinit(&context->function);
    context->ready = false;
    return success;
}

/*
 * 函数名：A_Modbus_Refresh。
 * 说明：将六瓶压力与状态、总压力、阀门位图、测试合格位图和系统状态刷新到外部输入寄存器。
 * 输入：context 为外部 Modbus 应用层上下文；system 为只读气源系统状态。
 * 输出：无；输入寄存器表在 context 中被更新。
 */
void A_Modbus_Refresh(A_Modbus_Context *context, const Gas_System *system)
{
    uint8_t index; // 当前作用域变量，用于保存遍历索引。
    uint16_t exhaust_mask = 0U; // 当前刷新函数使用的排气阀位图；bit0～bit5分别对应1～6号瓶，0表示关阀，1表示开阀，bit6～bit15保留为0。
    uint16_t qualified_mask = 0U; // 当前刷新函数使用的测试合格位图；bit0～bit5分别对应1～6号瓶，0表示未合格，1表示已合格，bit6～bit15保留为0。
    uint16_t supply_mask = 0U; // 当前刷新函数使用的供气阀位图；bit0～bit5分别对应1～6号瓶，0表示关阀，1表示开阀，bit6～bit15保留为0。
    uint16_t test_mask = 0U; // 当前刷新函数使用的测试阀位图；bit0～bit5分别对应1～6号瓶，0表示关阀，1表示开阀，bit6～bit15保留为0。
    uint16_t total_pressure_high; // 当前作用域变量，用于保存压力值。
    uint16_t total_pressure_low; // 当前作用域变量，用于保存压力值。

    if ((context == NULL) || (system == NULL) || !context->ready)
    {
        return;
    }

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        uint16_t pressure_high; // 当前作用域变量，用于保存压力值。
        uint16_t pressure_low; // 当前作用域变量，用于保存压力值。
        uint16_t pressure_offset = (uint16_t) (A_MODBUS_INPUT_PRESSURE_BASE + (index * 2U)); // 当前作用域变量，用于保存数据偏移量。

        A_Modbus_FloatToRegisters(system->cylinder[index].pressure_mpa, &pressure_high, &pressure_low);
        (void) F_Modbus_SetInputRegister(&context->function, pressure_offset, pressure_high);
        (void) F_Modbus_SetInputRegister(&context->function, (uint16_t) (pressure_offset + 1U), pressure_low);
        (void) F_Modbus_SetInputRegister(&context->function,
                                       (uint16_t) (A_MODBUS_INPUT_STATE_BASE + index),
                                       (uint16_t) system->cylinder[index].state);
        (void) F_Modbus_SetInputRegister(&context->function,
                                       (uint16_t) (A_MODBUS_INPUT_QUALITY_BASE + index),
                                       (uint16_t) system->cylinder[index].pressure_quality);
        if (system->cylinder[index].exhaust_cmd)
        {
            exhaust_mask = (uint16_t) (exhaust_mask | (uint16_t) (1U << index));
        }
        if (system->cylinder[index].qualification_passed)
        {
            qualified_mask = (uint16_t) (qualified_mask | (uint16_t) (1U << index));
        }
        if (system->cylinder[index].supply_cmd)
        {
            supply_mask = (uint16_t) (supply_mask | (uint16_t) (1U << index));
        }
        if (system->cylinder[index].test_cmd)
        {
            test_mask = (uint16_t) (test_mask | (uint16_t) (1U << index));
        }
    }
    // 六路离散状态压缩成位图，减少上位机一次状态刷新所需的寄存器数量。

    (void) F_Modbus_SetInputRegister(&context->function, A_MODBUS_INPUT_MODE, (uint16_t) system->mode);
    (void) F_Modbus_SetInputRegister(&context->function,
                                   A_MODBUS_INPUT_ACTIVE_BOTTLE,
                                   (system->active_index < GAS_CYLINDER_COUNT) ?
                                   (uint16_t) (system->active_index + 1U) : 0U);
    (void) F_Modbus_SetInputRegister(&context->function,
                                   A_MODBUS_INPUT_SWITCH_STATE,
                                   (uint16_t) system->switch_state);
    (void) F_Modbus_SetInputRegister(&context->function,
                                   A_MODBUS_INPUT_EXHAUST_STATE,
                                   exhaust_mask);
    (void) F_Modbus_SetInputRegister(&context->function,
                                   A_MODBUS_INPUT_ALARM_HIGH,
                                   (uint16_t) (system->alarm_bits >> 16U));
    (void) F_Modbus_SetInputRegister(&context->function,
                                   A_MODBUS_INPUT_ALARM_LOW,
                                   (uint16_t) system->alarm_bits);
    (void) F_Modbus_SetInputRegister(&context->function,
                                   A_MODBUS_INPUT_PLATFORM_READY,
                                   system->platform_ready ? 1U : 0U);
    (void) F_Modbus_SetInputRegister(&context->function,
                                   A_MODBUS_INPUT_VERSION,
                                   A_MODBUS_SOFTWARE_VERSION);
    A_Modbus_FloatToRegisters(system->total_pressure.pressure_mpa,
                              &total_pressure_high,
                              &total_pressure_low);
    (void) F_Modbus_SetInputRegister(&context->function,
                                     A_MODBUS_INPUT_TOTAL_PRESSURE_BASE,
                                     total_pressure_high);
    (void) F_Modbus_SetInputRegister(&context->function,
                                     (uint16_t) (A_MODBUS_INPUT_TOTAL_PRESSURE_BASE + 1U),
                                     total_pressure_low);
    (void) F_Modbus_SetInputRegister(&context->function,
                                     A_MODBUS_INPUT_TOTAL_QUALITY,
                                     (uint16_t) system->total_pressure.pressure_quality);
    (void) F_Modbus_SetInputRegister(&context->function,
                                     A_MODBUS_INPUT_QUALIFIED_MASK,
                                     qualified_mask);
    (void) F_Modbus_SetInputRegister(&context->function,
                                     A_MODBUS_INPUT_SUPPLY_MASK,
                                     supply_mask);
    (void) F_Modbus_SetInputRegister(&context->function,
                                     A_MODBUS_INPUT_TEST_MASK,
                                     test_mask);
}

/*
 * 函数名：A_Modbus_ProcessCommandWrite。
 * 说明：清除已经废弃的整机启停寄存器，并对任何非零旧命令返回无效命令结果。
 * 输入：context 为外部 Modbus 应用层上下文输入输出指针。
 * 输出：无；寄存器立即清零，非零写入在命令结果寄存器中标记为无效。
 */
static void A_Modbus_ProcessCommandWrite(A_Modbus_Context *context)
{
    uint16_t command_value; // 当前作用域变量，用于保存操作命令。
    (void) F_Modbus_GetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_COMMAND,
                                       &command_value);
    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_COMMAND,
                                       A_MODBUS_COMMAND_NONE); // 废弃地址写入后立即清零，避免旧值残留。

    if (command_value == A_MODBUS_COMMAND_NONE)
    {
        return;
    }
    A_Modbus_SetCommandResult(context, A_MODBUS_RESULT_INVALID_COMMAND);
    // V1.08固定自动控制，旧版启动和停止操作码均不再进入业务层。
}

/*
 * 函数名：A_Modbus_ProcessConfigCommit。
 * 说明：处理参数提交键值，解码 10 个三阀版参数并完成范围及关系校验。
 * 输入：context 为外部 Modbus 应用层上下文输入输出指针。
 * 输出：无；合法参数保存为待应用请求，处理状态写入参数结果寄存器。
 */
static void A_Modbus_ProcessConfigCommit(A_Modbus_Context *context)
{
    uint16_t key; // 当前作用域变量，用于保存当前处理数据。
    Gas_Config candidate; // 当前作用域变量，用于保存待校验候选值。
    A_Gas_Config_Validation validation; // 当前作用域变量，用于保存参数校验结果。

    (void) F_Modbus_GetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_CONFIG_COMMIT,
                                       &key);
    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_CONFIG_COMMIT,
                                       0U);
    if (key == 0U)
    {
        return;
    }
    if (key != A_MODBUS_CONFIG_COMMIT_KEY)
    {
        A_Modbus_SetConfigResult(context, A_MODBUS_CONFIG_RESULT_INVALID_KEY);
        return;
    }
    if (context->config_pending)
    {
        A_Modbus_SetConfigResult(context, A_MODBUS_CONFIG_RESULT_SYSTEM_BUSY);
        return;
    }
    if (!A_Modbus_ReadConfigRegisters(context, &candidate))
    {
        A_Modbus_SetConfigResult(context, A_MODBUS_CONFIG_RESULT_INVALID_RANGE);
        return;
    }

    validation = A_GasConfig_Validate(&candidate);
    if (validation == A_GAS_CONFIG_INVALID_RANGE)
    {
        A_Modbus_SetConfigResult(context, A_MODBUS_CONFIG_RESULT_INVALID_RANGE);
        return;
    }
    // 参数先解码后做单项范围及阈值关系校验，通过后仍需业务层检查停机和全关条件。
    if (validation == A_GAS_CONFIG_INVALID_RELATION)
    {
        A_Modbus_SetConfigResult(context, A_MODBUS_CONFIG_RESULT_INVALID_RELATION);
        return;
    }

    context->pending_config = candidate;
    context->config_pending = true;
    A_Modbus_SetConfigResult(context, A_MODBUS_CONFIG_RESULT_PENDING);
    // 候选参数与正在运行的context->config分离，EEPROM保存成功前不影响控制逻辑。
}

/*
 * 函数名：A_Modbus_ProcessConfigDefault。
 * 说明：处理恢复默认键值并生成一份待业务层保存和应用的默认参数。
 * 输入：context 为外部 Modbus 应用层上下文输入输出指针。
 * 输出：无；键值正确时在 context 中形成参数请求，否则写入错误结果。
 */
static void A_Modbus_ProcessConfigDefault(A_Modbus_Context *context)
{
    uint16_t key; // 当前作用域变量，用于保存当前处理数据。
    float low_warning_pressure_mpa; // 当前作用域变量，用于保存压力值。
    uint32_t manual_exhaust_time_ms; // 当前作用域变量，用于保存毫秒时间值。
    uint32_t test_valve_max_time_ms; // 当前作用域变量，用于保存毫秒时间值。

    (void) F_Modbus_GetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_CONFIG_DEFAULT,
                                       &key);
    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_CONFIG_DEFAULT,
                                       0U);
    if (key == 0U)
    {
        return;
    }
    if (key != A_MODBUS_CONFIG_DEFAULT_KEY)
    {
        A_Modbus_SetConfigResult(context, A_MODBUS_CONFIG_RESULT_INVALID_KEY);
        return;
    }
    if (context->config_pending)
    {
        A_Modbus_SetConfigResult(context, A_MODBUS_CONFIG_RESULT_SYSTEM_BUSY);
        return;
    }

    low_warning_pressure_mpa = context->current_config.low_warning_pressure_mpa;
    manual_exhaust_time_ms = context->current_config.manual_exhaust_time_ms; // 当前作用域变量，用于保存毫秒时间值。
    test_valve_max_time_ms = context->current_config.test_valve_max_time_ms; // 当前作用域变量，用于保存毫秒时间值。
    A_GasConfig_LoadDefaults(&context->pending_config);
    context->pending_config.low_warning_pressure_mpa = low_warning_pressure_mpa;
    context->pending_config.manual_exhaust_time_ms = manual_exhaust_time_ms;
    context->pending_config.test_valve_max_time_ms = test_valve_max_time_ms;
    // 外部“恢复默认”只恢复公开的10项，三项HMI安全参数保持当前值。
    context->config_pending = true;
    A_Modbus_SetConfigResult(context, A_MODBUS_CONFIG_RESULT_PENDING);
}

/*
 * 函数名：A_Modbus_ProcessLogCommandWrite。
 * 说明：解析日志命令和逻辑序号寄存器，并形成一条待气源应用层处理的日志读取请求。
 * 输入：context 为外部Modbus应用层上下文输入输出指针。
 * 输出：无；合法请求写入context，命令状态写入日志结果寄存器。
 */
static void A_Modbus_ProcessLogCommandWrite(A_Modbus_Context *context)
{
    uint16_t command_value; // 当前作用域变量，用于保存操作命令。
    uint16_t logical_index; // 当前作用域变量，用于保存日志逻辑索引。

    (void) F_Modbus_GetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_LOG_COMMAND,
                                       &command_value);
    (void) F_Modbus_GetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_LOG_INDEX,
                                       &logical_index);
    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_LOG_COMMAND,
                                       A_MODBUS_LOG_COMMAND_NONE); // 命令锁存后清零，防止同一请求被重复执行。

    if (command_value == A_MODBUS_LOG_COMMAND_NONE)
    {
        return;
    }
    if (command_value != A_MODBUS_LOG_COMMAND_READ)
    {
        A_Modbus_SetLogReadResult(context, A_MODBUS_LOG_RESULT_INVALID_COMMAND);
        return;
    }
    if (context->log_read_pending)
    {
        A_Modbus_SetLogReadResult(context, A_MODBUS_LOG_RESULT_BUSY);
        return;
    }

    context->pending_log_index = logical_index;
    context->log_read_pending = true;
    A_Modbus_SetLogReadResult(context, A_MODBUS_LOG_RESULT_PENDING);
    // 逻辑序号的范围由日志模块依据实时有效数量判断，Modbus层不接触EEPROM物理地址。
}

/*
 * 函数名：A_Modbus_Task。
 * 说明：周期处理外部主站请求，并把写寄存器操作转换为控制、运行参数或日志读取请求。
 * 输入：context 为外部 Modbus 应用层上下文输入输出指针。
 * 输出：无；待执行控制、参数和日志请求及对应结果状态写入context。
 */
void A_Modbus_Task(A_Modbus_Context *context)
{
    uint16_t write_start; // 当前作用域变量，用于保存起始边界。
    uint16_t write_count; // 当前作用域变量，用于保存数量计数。

    if ((context == NULL) || !context->ready)
    {
        return;
    }

    F_Modbus_Task(&context->function);
    if (!F_Modbus_TakeWriteEvent(&context->function, &write_start, &write_count))
    {
        return;
    }

    if (A_Modbus_WriteEventContainsRegister(write_start,
                                            write_count,
                                            A_MODBUS_HOLDING_COMMAND))
    {
        A_Modbus_ProcessCommandWrite(context);
    }
    if (A_Modbus_WriteEventContainsRegister(write_start,
                                            write_count,
                                            A_MODBUS_HOLDING_CONFIG_COMMIT))
    {
        A_Modbus_ProcessConfigCommit(context);
    }
    if (A_Modbus_WriteEventContainsRegister(write_start,
                                            write_count,
                                            A_MODBUS_HOLDING_CONFIG_DEFAULT))
    {
        A_Modbus_ProcessConfigDefault(context);
    }
    if (A_Modbus_WriteEventContainsRegister(write_start,
                                            write_count,
                                            A_MODBUS_HOLDING_LOG_COMMAND))
    {
        A_Modbus_ProcessLogCommandWrite(context);
    }
    // 功能码10可能一次覆盖多个触发寄存器，因此按写入范围分别检查所有业务入口。

    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_CONFIG_VERSION,
                                       A_MODBUS_CONFIG_VERSION_VALUE); // 版本寄存器即使被主站写入，也立即恢复为本机固定值。
}

/*
 * 函数名：A_Modbus_SetCommandResult。
 * 说明：将气源应用层的命令执行结果写入外部 Modbus 结果寄存器。
 * 输入：context 为外部 Modbus 应用层上下文；result 为待写入的结果状态码。
 * 输出：无；结果保存到保持寄存器 0x0101。
 */
void A_Modbus_SetCommandResult(A_Modbus_Context *context, a_modbus_result_t result)
{
    if ((context == NULL) || !context->ready)
    {
        return;
    }

    (void) F_Modbus_SetHoldingRegister(&context->function,
                                     A_MODBUS_HOLDING_RESULT,
                                     (uint16_t) result);
}

/*
 * 函数名：A_Modbus_UpdateConfigRegisters。
 * 说明：把当前有效运行参数刷新到外部 Modbus 参数保持寄存器。
 * 输入：context 为外部 Modbus 应用层上下文；config 为只读运行参数。
 * 输出：参数和上下文有效时返回 true，否则返回 false。
 */
bool A_Modbus_UpdateConfigRegisters(A_Modbus_Context *context, const Gas_Config *config)
{
    uint16_t value[A_GAS_CONFIG_REGISTER_COUNT]; // 当前作用域变量，用于保存当前处理值数组。
    uint16_t index; // 当前作用域变量，用于保存遍历索引。

    if ((context == NULL) || (config == NULL) || !context->ready ||
        (A_GasConfig_Validate(config) != A_GAS_CONFIG_VALID))
    {
        return false;
    }

    value[A_MODBUS_CONFIG_SWITCH_PRESSURE] = A_Modbus_PressureToRegister(config->switch_pressure_mpa);
    value[A_MODBUS_CONFIG_SWITCH_RELEASE] = A_Modbus_PressureToRegister(config->switch_release_mpa);
    value[A_MODBUS_CONFIG_VALVE_PULL_IN_TIME] = (uint16_t) config->valve_pull_in_time_ms;
    value[A_MODBUS_CONFIG_READY_MIN_PRESSURE] = A_Modbus_PressureToRegister(config->ready_min_pressure_mpa);
    value[A_MODBUS_CONFIG_PRESSURE_MAX] = A_Modbus_PressureToRegister(config->pressure_max_mpa);
    value[A_MODBUS_CONFIG_LOW_CONFIRM_TIME] = (uint16_t) config->low_confirm_time_ms;
    value[A_MODBUS_CONFIG_LOW_CONFIRM_SAMPLES] = config->low_confirm_samples;
    value[A_MODBUS_CONFIG_VALVE_CLOSE_WAIT] = (uint16_t) config->valve_close_wait_ms;
    value[A_MODBUS_CONFIG_VALVE_OPEN_WAIT] = (uint16_t) config->valve_open_wait_ms;
    value[A_MODBUS_CONFIG_PRESSURE_FRESH] = (uint16_t) config->pressure_fresh_ms;

    for (index = 0U; index < A_GAS_CONFIG_REGISTER_COUNT; ++index)
    {
        if (!F_Modbus_SetHoldingRegister(&context->function,
                                         (uint16_t) (A_MODBUS_HOLDING_CONFIG_BASE + index),
                                         value[index]))
        {
            return false;
        }
    }
    context->current_config = *config;
    return true;
}

/*
 * 函数名：A_Modbus_TakeConfigRequest。
 * 说明：取出一份已经通过寄存器解码和参数校验的待应用运行参数。
 * 输入：context 为外部 Modbus 上下文；config 为运行参数输出指针。
 * 输出：存在待处理参数请求时返回 true，否则返回 false。
 */
bool A_Modbus_TakeConfigRequest(A_Modbus_Context *context, Gas_Config *config)
{
    if ((context == NULL) || (config == NULL) || !context->config_pending)
    {
        return false;
    }

    *config = context->pending_config;
    context->config_pending = false;
    return true;
}

/*
 * 函数名：A_Modbus_SetConfigResult。
 * 说明：更新外部 Modbus 参数处理结果寄存器。
 * 输入：context 为外部 Modbus 上下文；result 为参数处理结果码。
 * 输出：无；结果写入 PDU 地址 0x0103。
 */
void A_Modbus_SetConfigResult(A_Modbus_Context *context, a_modbus_config_result_t result)
{
    if ((context == NULL) || !context->ready)
    {
        return;
    }

    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_CONFIG_RESULT,
                                       (uint16_t) result);
}

/*
 * 函数名：A_Modbus_TakeLogReadRequest。
 * 说明：取出并清除一条由外部主站提交的日志读取请求。
 * 输入：context 为外部Modbus上下文；logical_index为零起始日志逻辑序号输出指针。
 * 输出：存在待处理日志请求时返回true，否则返回false。
 */
bool A_Modbus_TakeLogReadRequest(A_Modbus_Context *context, uint16_t *logical_index)
{
    if ((context == NULL) || (logical_index == NULL) || !context->log_read_pending)
    {
        return false;
    }

    *logical_index = context->pending_log_index;
    context->pending_log_index = 0U;
    context->log_read_pending = false;
    return true;
}

/*
 * 函数名：A_Modbus_UpdateLogInfo。
 * 说明：刷新外部保持寄存器中的有效日志数量、容量和单条记录长度。
 * 输入：context 为外部Modbus上下文；valid_count为有效数量；capacity为最大数量。
 * 输出：无；三个只读信息寄存器被更新。
 */
void A_Modbus_UpdateLogInfo(A_Modbus_Context *context,
                            uint16_t valid_count,
                            uint16_t capacity)
{
    if ((context == NULL) || !context->ready)
    {
        return;
    }

    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_LOG_COUNT,
                                       valid_count);
    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_LOG_CAPACITY,
                                       capacity);
    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_LOG_RECORD_SIZE,
                                       A_MODBUS_LOG_RECORD_SIZE);
}

/*
 * 函数名：A_Modbus_SetLogRecord。
 * 说明：把一条32字节原始日志按高字节在前顺序写入16个日志数据保持寄存器。
 * 输入：context 为外部Modbus上下文；record为32字节只读日志缓存。
 * 输出：全部数据寄存器更新成功时返回true，否则返回false。
 */
bool A_Modbus_SetLogRecord(A_Modbus_Context *context,
                           const uint8_t record[A_MODBUS_LOG_RECORD_SIZE])
{
    uint16_t index; // 当前作用域变量，用于保存遍历索引。

    if ((context == NULL) || (record == NULL) || !context->ready)
    {
        return false;
    }

    for (index = 0U; index < A_MODBUS_LOG_REGISTER_COUNT; ++index)
    {
        uint16_t value = (uint16_t) (((uint16_t) record[index * 2U] << 8U) |
                                     record[(index * 2U) + 1U]);

        if (!F_Modbus_SetHoldingRegister(&context->function,
                                         (uint16_t) (A_MODBUS_HOLDING_LOG_DATA_BASE + index),
                                         value))
        {
            return false;
        }
    }
    // 每个寄存器承载原始记录的连续两个字节，高字节在前，便于上位机无损还原32字节。
    return true;
}

/*
 * 函数名：A_Modbus_SetLogReadResult。
 * 说明：更新日志读取结果，失败结果同时清空日志数据窗口以避免误用上一次数据。
 * 输入：context 为外部Modbus上下文；result为日志读取结果码。
 * 输出：无；结果和必要的数据窗口内容写入保持寄存器。
 */
void A_Modbus_SetLogReadResult(A_Modbus_Context *context, A_Modbus_Log_Result result)
{
    uint16_t index; // 当前作用域变量，用于保存遍历索引。

    if ((context == NULL) || !context->ready)
    {
        return;
    }

    if (result != A_MODBUS_LOG_RESULT_SUCCESS)
    {
        for (index = 0U; index < A_MODBUS_LOG_REGISTER_COUNT; ++index)
        {
            (void) F_Modbus_SetHoldingRegister(&context->function,
                                               (uint16_t) (A_MODBUS_HOLDING_LOG_DATA_BASE + index),
                                               0U);
        }
        // 任何非成功状态都清空旧数据，主站必须同时检查结果码才能使用窗口内容。
    }
    (void) F_Modbus_SetHoldingRegister(&context->function,
                                       A_MODBUS_HOLDING_LOG_RESULT,
                                       (uint16_t) result);
}

/*
 * 函数名：A_Modbus_IsReady。
 * 说明：查询外部 Modbus 是否已经成功初始化。
 * 输入：context 为只读外部 Modbus 应用层上下文指针。
 * 输出：初始化完成时返回 true，否则返回 false。
 */
bool A_Modbus_IsReady(const A_Modbus_Context *context)
{
    return ((context != NULL) && context->ready);
}

/*
 * 函数名：A_Modbus_HasFault。
 * 说明：查询外部Modbus应用层及其下层是否存在影响通讯的故障。
 * 输入：context为只读外部Modbus应用层上下文。
 * 输出：应用层未就绪或SCI0硬件故障时返回true，否则返回false。
 */
bool A_Modbus_HasFault(const A_Modbus_Context *context)
{
    return ((context == NULL) || !context->ready ||
            F_Modbus_HasFault(&context->function));
}
