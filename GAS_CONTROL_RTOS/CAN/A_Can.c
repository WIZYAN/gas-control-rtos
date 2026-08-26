#include "A_Can.h"

#include <stddef.h>
#include <string.h>

#include "A_Gas_Config.h"

/*
 * 函数名：A_Can_FloatToRaw。
 * 说明：无别名访问地取得IEEE-754 float32的32位原始位模式。
 * 输入：value为待转换浮点值。
 * 输出：返回同一位模式的uint32数值。
 */
static uint32_t A_Can_FloatToRaw(float value)
{
    uint32_t raw = 0U;
    (void) memcpy(&raw, &value, sizeof(raw));
    return raw;
}

/*
 * 函数名：A_Can_RawToFloat。
 * 说明：无别名访问地把32位原始位模式转换为IEEE-754 float32。
 * 输入：raw为待转换原始位模式。
 * 输出：返回具有相同位模式的浮点值。
 */
static float A_Can_RawToFloat(uint32_t raw)
{
    float value = 0.0F;
    (void) memcpy(&value, &raw, sizeof(value));
    return value;
}

/*
 * 函数名：A_Can_GetCylinderMask。
 * 说明：按指定类别汇总六瓶布尔状态为bit0～bit5位图。
 * 输入：system为系统状态；kind为0进气阀、1排气阀、2测试阀、3测试合格标志。
 * 输出：返回六位状态位图。
 */
static uint32_t A_Can_GetCylinderMask(const Gas_System *system, uint8_t kind)
{
    uint32_t mask = 0U;
    uint8_t index;

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        bool set = false;
        if (kind == 0U) { set = system->cylinder[index].supply_cmd; }
        else if (kind == 1U) { set = system->cylinder[index].exhaust_cmd; }
        else if (kind == 2U) { set = system->cylinder[index].test_cmd; }
        else { set = system->cylinder[index].qualification_passed; }
        if (set)
        {
            mask |= (1UL << index);
        }
    }
    return mask;
}

/*
 * 函数名：A_Can_ReadValue。
 * 说明：将一个CAN数据地址映射为当前压力、状态、参数、命令结果或日志原始数据。
 * 输入：context为CAN上下文；system为系统状态；comm_mode为通讯模式；address为数据地址；value为32位输出指针。
 * 输出：地址可读且成功输出时返回true，不存在的地址返回false。
 */
static bool A_Can_ReadValue(const A_Can_Context *context,
                            const Gas_System *system,
                            gas_external_comm_mode_t comm_mode,
                            uint16_t address,
                            uint32_t *value)
{
    uint16_t offset;

    if ((context == NULL) || (system == NULL) || (value == NULL))
    {
        return false;
    }
    if (address < (A_CAN_ADDRESS_PRESSURE_BASE + GAS_CYLINDER_COUNT))
    {
        *value = A_Can_FloatToRaw(system->cylinder[address - A_CAN_ADDRESS_PRESSURE_BASE].pressure_mpa);
        return true;
    }
    if (address == A_CAN_ADDRESS_TOTAL_PRESSURE)
    {
        *value = A_Can_FloatToRaw(system->total_pressure.pressure_mpa);
        return true;
    }
    if ((address >= A_CAN_ADDRESS_STATE_BASE) &&
        (address < (A_CAN_ADDRESS_STATE_BASE + GAS_CYLINDER_COUNT)))
    {
        *value = (uint32_t) system->cylinder[address - A_CAN_ADDRESS_STATE_BASE].state;
        return true;
    }
    if ((address >= A_CAN_ADDRESS_QUALITY_BASE) &&
        (address < (A_CAN_ADDRESS_QUALITY_BASE + GAS_CYLINDER_COUNT)))
    {
        *value = (uint32_t) system->cylinder[address - A_CAN_ADDRESS_QUALITY_BASE].pressure_quality;
        return true;
    }

    switch (address)
    {
        case A_CAN_ADDRESS_TOTAL_QUALITY: *value = (uint32_t) system->total_pressure.pressure_quality; return true;
        case A_CAN_ADDRESS_SYSTEM_MODE: *value = (uint32_t) system->mode; return true;
        case A_CAN_ADDRESS_ACTIVE_BOTTLE:
            *value = (system->active_index < GAS_CYLINDER_COUNT) ? (uint32_t) (system->active_index + 1U) : 0U;
            return true;
        case A_CAN_ADDRESS_SWITCH_STATE: *value = (uint32_t) system->switch_state; return true;
        case A_CAN_ADDRESS_EXHAUST_MASK: *value = A_Can_GetCylinderMask(system, 1U); return true;
        case A_CAN_ADDRESS_ALARM_BITS: *value = system->alarm_bits; return true;
        case A_CAN_ADDRESS_PLATFORM_READY: *value = system->platform_ready ? 1U : 0U; return true;
        case A_CAN_ADDRESS_SOFTWARE_VERSION: *value = A_CAN_SOFTWARE_VERSION; return true;
        case A_CAN_ADDRESS_QUALIFIED_MASK: *value = A_Can_GetCylinderMask(system, 3U); return true;
        case A_CAN_ADDRESS_SUPPLY_MASK: *value = A_Can_GetCylinderMask(system, 0U); return true;
        case A_CAN_ADDRESS_TEST_MASK: *value = A_Can_GetCylinderMask(system, 2U); return true;
        case A_CAN_ADDRESS_COMM_MODE: *value = (uint32_t) comm_mode; return true;
        case A_CAN_ADDRESS_SWITCH_PRESSURE: *value = A_Can_FloatToRaw(context->staged_config.switch_pressure_mpa); return true;
        case A_CAN_ADDRESS_SWITCH_RELEASE: *value = A_Can_FloatToRaw(context->staged_config.switch_release_mpa); return true;
        case A_CAN_ADDRESS_READY_MIN_PRESSURE: *value = A_Can_FloatToRaw(context->staged_config.ready_min_pressure_mpa); return true;
        case A_CAN_ADDRESS_PRESSURE_MAX: *value = A_Can_FloatToRaw(context->staged_config.pressure_max_mpa); return true;
        case A_CAN_ADDRESS_VALVE_PULL_IN_TIME: *value = context->staged_config.valve_pull_in_time_ms; return true;
        case A_CAN_ADDRESS_LOW_CONFIRM_TIME: *value = context->staged_config.low_confirm_time_ms; return true;
        case A_CAN_ADDRESS_LOW_CONFIRM_SAMPLES: *value = context->staged_config.low_confirm_samples; return true;
        case A_CAN_ADDRESS_VALVE_CLOSE_WAIT: *value = context->staged_config.valve_close_wait_ms; return true;
        case A_CAN_ADDRESS_VALVE_OPEN_WAIT: *value = context->staged_config.valve_open_wait_ms; return true;
        case A_CAN_ADDRESS_PRESSURE_FRESH: *value = context->staged_config.pressure_fresh_ms; return true;
        case A_CAN_ADDRESS_COMMAND: *value = (uint32_t) context->pending_command; return true;
        case A_CAN_ADDRESS_COMMAND_RESULT: *value = (uint32_t) context->command_result; return true;
        case A_CAN_ADDRESS_CONFIG_COMMIT: *value = 0U; return true;
        case A_CAN_ADDRESS_CONFIG_RESULT: *value = (uint32_t) context->config_result; return true;
        case A_CAN_ADDRESS_CONFIG_VERSION: *value = A_CAN_CONFIG_VERSION_VALUE; return true;
        case A_CAN_ADDRESS_CONFIG_DEFAULT: *value = 0U; return true;
        case A_CAN_ADDRESS_LOG_COMMAND: *value = 0U; return true;
        case A_CAN_ADDRESS_LOG_INDEX: *value = context->selected_log_index; return true;
        case A_CAN_ADDRESS_LOG_RESULT: *value = (uint32_t) context->log_result; return true;
        case A_CAN_ADDRESS_LOG_COUNT: *value = context->log_count; return true;
        case A_CAN_ADDRESS_LOG_CAPACITY: *value = context->log_capacity; return true;
        case A_CAN_ADDRESS_LOG_RECORD_SIZE: *value = A_CAN_LOG_RECORD_SIZE; return true;
        default: break;
    }

    if ((address >= A_CAN_ADDRESS_LOG_DATA_BASE) &&
        (address < (A_CAN_ADDRESS_LOG_DATA_BASE + (A_CAN_LOG_RECORD_SIZE / 4U))))
    {
        offset = (uint16_t) ((address - A_CAN_ADDRESS_LOG_DATA_BASE) * 4U);
        *value = ((uint32_t) context->log_record[offset]) |
                 ((uint32_t) context->log_record[offset + 1U] << 8U) |
                 ((uint32_t) context->log_record[offset + 2U] << 16U) |
                 ((uint32_t) context->log_record[offset + 3U] << 24U);
        return true;
    }
    return false;
}

/*
 * 函数名：A_Can_WriteValue。
 * 说明：处理一个CAN可写地址，将参数写入暂存区或生成控制、提交和日志请求。
 * 输入：context为CAN上下文；address为数据地址；value为32位原始写值。
 * 输出：返回写成功、只读、范围错误或忙状态码。
 */
static A_Can_Write_Result A_Can_WriteValue(A_Can_Context *context,
                                            uint16_t address,
                                            uint32_t value)
{
    A_Gas_Config_Validation validation;

    switch (address)
    {
        case A_CAN_ADDRESS_SWITCH_PRESSURE: context->staged_config.switch_pressure_mpa = A_Can_RawToFloat(value); return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_SWITCH_RELEASE: context->staged_config.switch_release_mpa = A_Can_RawToFloat(value); return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_READY_MIN_PRESSURE: context->staged_config.ready_min_pressure_mpa = A_Can_RawToFloat(value); return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_PRESSURE_MAX: context->staged_config.pressure_max_mpa = A_Can_RawToFloat(value); return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_VALVE_PULL_IN_TIME: context->staged_config.valve_pull_in_time_ms = value; return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_LOW_CONFIRM_TIME: context->staged_config.low_confirm_time_ms = value; return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_LOW_CONFIRM_SAMPLES:
            if (value > 0xFFFFU) { return A_CAN_WRITE_INVALID_RANGE; }
            context->staged_config.low_confirm_samples = (uint16_t) value;
            return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_VALVE_CLOSE_WAIT: context->staged_config.valve_close_wait_ms = value; return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_VALVE_OPEN_WAIT: context->staged_config.valve_open_wait_ms = value; return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_PRESSURE_FRESH: context->staged_config.pressure_fresh_ms = value; return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_COMMAND:
            if (context->command_pending) { return A_CAN_WRITE_BUSY; }
            if ((value != GAS_EXTERNAL_COMMAND_START_AUTO) && (value != GAS_EXTERNAL_COMMAND_STOP))
            {
                context->command_result = GAS_EXTERNAL_RESULT_INVALID_COMMAND;
                return A_CAN_WRITE_INVALID_RANGE;
            }
            context->pending_command = (gas_external_command_t) value;
            context->command_pending = true;
            context->command_result = GAS_EXTERNAL_RESULT_PENDING;
            return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_CONFIG_COMMIT:
            if (value != A_CAN_CONFIG_COMMIT_KEY)
            {
                context->config_result = GAS_EXTERNAL_CONFIG_INVALID_KEY;
                return A_CAN_WRITE_INVALID_RANGE;
            }
            if (context->config_pending) { return A_CAN_WRITE_BUSY; }
            validation = A_GasConfig_Validate(&context->staged_config);
            if (validation != A_GAS_CONFIG_VALID)
            {
                context->config_result = (validation == A_GAS_CONFIG_INVALID_RELATION) ?
                    GAS_EXTERNAL_CONFIG_INVALID_RELATION : GAS_EXTERNAL_CONFIG_INVALID_RANGE;
                return A_CAN_WRITE_INVALID_RANGE;
            }
            context->pending_config = context->staged_config;
            context->config_pending = true;
            context->config_result = GAS_EXTERNAL_CONFIG_PENDING;
            return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_CONFIG_DEFAULT:
        {
            float low_warning_pressure_mpa;
            uint32_t manual_exhaust_time_ms;
            uint32_t test_valve_max_time_ms;

            if (value != A_CAN_CONFIG_DEFAULT_KEY)
            {
                context->config_result = GAS_EXTERNAL_CONFIG_INVALID_KEY;
                return A_CAN_WRITE_INVALID_RANGE;
            }
            if (context->config_pending) { return A_CAN_WRITE_BUSY; }
            low_warning_pressure_mpa = context->staged_config.low_warning_pressure_mpa;
            manual_exhaust_time_ms = context->staged_config.manual_exhaust_time_ms;
            test_valve_max_time_ms = context->staged_config.test_valve_max_time_ms;
            A_GasConfig_LoadDefaults(&context->pending_config);
            context->pending_config.low_warning_pressure_mpa = low_warning_pressure_mpa;
            context->pending_config.manual_exhaust_time_ms = manual_exhaust_time_ms;
            context->pending_config.test_valve_max_time_ms = test_valve_max_time_ms;
            // CAN恢复默认只处理协议已公开的10项，不改变密码页管理的三项安全参数。
            context->staged_config = context->pending_config;
            context->config_pending = true;
            context->config_result = GAS_EXTERNAL_CONFIG_PENDING;
            return A_CAN_WRITE_SUCCESS;
        }
        case A_CAN_ADDRESS_LOG_INDEX:
            if (value > 0xFFFFU) { return A_CAN_WRITE_INVALID_RANGE; }
            context->selected_log_index = (uint16_t) value;
            return A_CAN_WRITE_SUCCESS;
        case A_CAN_ADDRESS_LOG_COMMAND:
            if (value != 1U)
            {
                context->log_result = GAS_EXTERNAL_LOG_INVALID_COMMAND;
                return A_CAN_WRITE_INVALID_RANGE;
            }
            if (context->log_read_pending)
            {
                context->log_result = GAS_EXTERNAL_LOG_BUSY;
                return A_CAN_WRITE_BUSY;
            }
            context->pending_log_index = context->selected_log_index;
            context->log_read_pending = true;
            context->log_result = GAS_EXTERNAL_LOG_PENDING;
            return A_CAN_WRITE_SUCCESS;
        default: return A_CAN_WRITE_READ_ONLY;
    }
}

/*
 * 函数名：A_Can_Init。
 * 说明：初始化CAN0、自定义协议和当前运行参数镜像。
 * 输入：context为待初始化上下文；config为当前有效运行参数。
 * 输出：硬件层和功能层均初始化成功时返回true，否则返回false。
 */
bool A_Can_Init(A_Can_Context *context, const Gas_Config *config)
{
    if ((context == NULL) || (config == NULL) ||
        (A_GasConfig_Validate(config) != A_GAS_CONFIG_VALID))
    {
        return false;
    }
    (void) memset(context, 0, sizeof(*context));
    context->staged_config = *config;
    context->command_result = GAS_EXTERNAL_RESULT_IDLE;
    context->config_result = GAS_EXTERNAL_CONFIG_IDLE;
    context->log_result = GAS_EXTERNAL_LOG_IDLE;
    if (!F_CanProtocol_Init(&context->function,
                            &context->hardware,
                            F_CAN_LOCAL_TYPE,
                            F_CAN_LOCAL_ADDRESS))
    {
        return false;
    }
    context->ready = true;
    return true;
}

/*
 * 函数名：A_Can_Deinit。
 * 说明：关闭CAN0并清除应用层就绪状态，供停止状态切换外部通讯使用。
 * 输入：context为CAN应用层上下文输入输出指针。
 * 输出：CAN已经关闭或成功关闭时返回true，否则返回false。
 */
bool A_Can_Deinit(A_Can_Context *context)
{
    bool success;
    if (context == NULL) { return false; }
    success = F_CanProtocol_Deinit(&context->function);
    context->ready = false;
    return success;
}

/*
 * 函数名：A_Can_QueueReadResponses。
 * 说明：把连续读请求按发送队列当前空闲量分批转换为单地址响应，队列满时保留进度供后续周期继续。
 * 输入：context为CAN上下文；system为只读系统状态；comm_mode为当前外部通讯模式。
 * 输出：无；成功入队的地址从待发送数量中扣除，未入队部分保存在context中。
 */
static void A_Can_QueueReadResponses(A_Can_Context *context,
                                     const Gas_System *system,
                                     gas_external_comm_mode_t comm_mode)
{
    while (context->read_response_remaining > 0U)
    {
        uint16_t address = context->read_response_address;
        uint32_t value;

        if (!A_Can_ReadValue(context, system, comm_mode, address, &value))
        {
            value = 0xFFFFFFFFUL;
        }
        if (!F_CanProtocol_QueueReadResponse(&context->function,
                                             context->read_response_target_type,
                                             context->read_response_target_address,
                                             address,
                                             value))
        {
            return;
        }
        context->read_response_address = (uint16_t) (address + 1U);
        context->read_response_remaining--;
        // 每成功加入一帧才推进地址，发送队列暂满时不会丢失后续连续地址。
    }
}

/*
 * 函数名：A_Can_Task。
 * 说明：周期解析CAN读写请求、访问地址表并分批组织非阻塞响应。
 * 输入：context为CAN应用层上下文；system为只读气源系统状态；comm_mode为当前外部通讯模式。
 * 输出：无；请求、结果及发送队列状态保存在context中。
 */
void A_Can_Task(A_Can_Context *context,
                const Gas_System *system,
                gas_external_comm_mode_t comm_mode)
{
    F_Can_Request request;

    if ((context == NULL) || (system == NULL) || !context->ready)
    {
        return;
    }
    F_CanProtocol_Task(&context->function);
    A_Can_QueueReadResponses(context, system, comm_mode);
    if (context->read_response_remaining > 0U)
    {
        return;
    }
    if (!F_CanProtocol_TakeRequest(&context->function, &request))
    {
        return;
    }

    if ((request.function == F_CAN_FUNCTION_READ) ||
        (request.function == F_CAN_FUNCTION_BROADCAST_READ))
    {
        context->read_response_address = request.data_address;
        context->read_response_remaining = request.data_length;
        context->read_response_target_type = request.source_type;
        context->read_response_target_address = request.source_address;
        A_Can_QueueReadResponses(context, system, comm_mode);
        // 连续读响应超过队列容量时分多个主循环周期发送，不再因队列暂满丢弃后续地址。
    }
    else
    {
        A_Can_Write_Result result = A_Can_WriteValue(context,
                                                      request.data_address,
                                                      request.value);
        if (!F_CanProtocol_QueueWriteResponse(&context->function,
                                              request.source_type,
                                              request.source_address,
                                              request.data_address,
                                              (uint32_t) result))
        {
            context->response_drop_count++;
        }
    }
}

/*
 * 函数名：A_Can_TakeCommand。
 * 说明：取出并清除一条CAN控制命令。
 * 输入：context为CAN上下文；command为命令输出指针。
 * 输出：存在待处理命令时返回true，否则返回false。
 */
bool A_Can_TakeCommand(A_Can_Context *context, gas_external_command_t *command)
{
    if ((context == NULL) || (command == NULL) || !context->command_pending) { return false; }
    *command = context->pending_command;
    context->pending_command = GAS_EXTERNAL_COMMAND_NONE;
    context->command_pending = false;
    return true;
}

/*
 * 函数名：A_Can_SetCommandResult。
 * 说明：更新CAN只读整数地址0x0118公布的控制命令结果。
 * 输入：context为CAN上下文；result为命令结果。
 * 输出：无；结果保存到context。
 */
void A_Can_SetCommandResult(A_Can_Context *context, gas_external_result_t result)
{
    if (context != NULL) { context->command_result = result; }
}

/*
 * 函数名：A_Can_UpdateConfig。
 * 说明：用当前生效参数同时刷新CAN读值和下一轮写入暂存区。
 * 输入：context为CAN上下文；config为当前有效运行参数。
 * 输出：参数有效且更新成功时返回true，否则返回false。
 */
bool A_Can_UpdateConfig(A_Can_Context *context, const Gas_Config *config)
{
    if ((context == NULL) || (config == NULL) ||
        (A_GasConfig_Validate(config) != A_GAS_CONFIG_VALID)) { return false; }
    context->staged_config = *config;
    return true;
}

/*
 * 函数名：A_Can_TakeConfigRequest。
 * 说明：取出一份已经校验、等待业务层保存应用的运行参数。
 * 输入：context为CAN上下文；config为参数输出指针。
 * 输出：存在待处理参数请求时返回true，否则返回false。
 */
bool A_Can_TakeConfigRequest(A_Can_Context *context, Gas_Config *config)
{
    if ((context == NULL) || (config == NULL) || !context->config_pending) { return false; }
    *config = context->pending_config;
    context->config_pending = false;
    return true;
}

/*
 * 函数名：A_Can_SetConfigResult。
 * 说明：更新CAN只读整数地址0x0119公布的参数处理结果。
 * 输入：context为CAN上下文；result为参数处理结果。
 * 输出：无；结果保存到context。
 */
void A_Can_SetConfigResult(A_Can_Context *context, gas_external_config_result_t result)
{
    if (context != NULL) { context->config_result = result; }
}

/*
 * 函数名：A_Can_TakeLogReadRequest。
 * 说明：取出并清除一条CAN日志读取请求。
 * 输入：context为CAN上下文；logical_index为日志逻辑序号输出指针。
 * 输出：存在待处理请求时返回true，否则返回false。
 */
bool A_Can_TakeLogReadRequest(A_Can_Context *context, uint16_t *logical_index)
{
    if ((context == NULL) || (logical_index == NULL) || !context->log_read_pending) { return false; }
    *logical_index = context->pending_log_index;
    context->log_read_pending = false;
    return true;
}

/*
 * 函数名：A_Can_UpdateLogInfo。
 * 说明：更新CAN地址表中的日志数量和容量信息。
 * 输入：context为CAN上下文；valid_count为有效记录数；capacity为最大记录数。
 * 输出：无；信息保存到context。
 */
void A_Can_UpdateLogInfo(A_Can_Context *context, uint16_t valid_count, uint16_t capacity)
{
    if (context != NULL) { context->log_count = valid_count; context->log_capacity = capacity; }
}

/*
 * 函数名：A_Can_SetLogRecord。
 * 说明：把一条32字节EEPROM原始日志复制到八个连续CAN数据地址窗口。
 * 输入：context为CAN上下文；record为32字节只读记录。
 * 输出：复制成功时返回true，否则返回false。
 */
bool A_Can_SetLogRecord(A_Can_Context *context, const uint8_t record[A_CAN_LOG_RECORD_SIZE])
{
    if ((context == NULL) || (record == NULL) || !context->ready) { return false; }
    (void) memcpy(context->log_record, record, A_CAN_LOG_RECORD_SIZE);
    return true;
}

/*
 * 函数名：A_Can_SetLogReadResult。
 * 说明：更新CAN日志读取结果，非成功结果同时清空旧日志窗口。
 * 输入：context为CAN上下文；result为日志读取结果。
 * 输出：无；结果和日志窗口保存在context中。
 */
void A_Can_SetLogReadResult(A_Can_Context *context, gas_external_log_result_t result)
{
    if (context == NULL) { return; }
    context->log_result = result;
    if (result != GAS_EXTERNAL_LOG_SUCCESS)
    {
        (void) memset(context->log_record, 0, sizeof(context->log_record));
    }
}

/*
 * 函数名：A_Can_IsReady。
 * 说明：查询CAN外部通讯应用层是否已经成功初始化。
 * 输入：context为只读CAN上下文。
 * 输出：初始化成功时返回true，否则返回false。
 */
bool A_Can_IsReady(const A_Can_Context *context)
{
    return ((context != NULL) && context->ready);
}

/*
 * 函数名：A_Can_HasFault。
 * 说明：查询CAN应用层及其下层是否存在影响外部通讯的故障。
 * 输入：context为只读CAN应用层上下文。
 * 输出：应用层未就绪或CAN0故障时返回true，否则返回false。
 */
bool A_Can_HasFault(const A_Can_Context *context)
{
    return ((context == NULL) || !context->ready ||
            F_CanProtocol_HasFault(&context->function));
}
