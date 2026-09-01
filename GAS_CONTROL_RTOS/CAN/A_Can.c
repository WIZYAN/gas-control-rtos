/*
 * Version: v1.12
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现CAN状态映射、参数访问、控制命令和日志读取业务。
 */

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
    uint32_t raw = 0U; // 当前作用域变量，用于保存当前处理数据。
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
    float value = 0.0F; // 当前作用域变量，用于保存当前处理值。
    (void) memcpy(&value, &raw, sizeof(value));
    return value;
}

/*
 * 函数名：A_Can_PackWriteResult。
 * 说明：把基础结果码和详细原因码组合为功能码6使用的32位小端结果。
 * 输入：result为基础结果码；detail为详细原因码。
 * 输出：返回低8位为基础码、bit8～bit15为详细码的32位结果。
 */
static uint32_t A_Can_PackWriteResult(A_Can_Write_Result result,
                                      A_Can_Write_Detail detail)
{
    return ((uint32_t) result) | ((uint32_t) detail << 8U);
}

/*
 * 函数名：A_Can_FloatRawIsFinite。
 * 说明：通过IEEE-754指数位检查原始float32是否为有限数，拒绝NaN和正负无穷大。
 * 输入：raw为float32的32位原始位模式。
 * 输出：指数位不是全1时返回true，否则返回false。
 */
static bool A_Can_FloatRawIsFinite(uint32_t raw)
{
    return ((raw & 0x7F800000UL) != 0x7F800000UL);
}

/*
 * 函数名：A_Can_IsMechanicalSwitching。
 * 说明：判断自动切瓶是否已经进入实际关阀、死区、开阀或新阀验证阶段。
 * 输入：system为只读气源系统状态。
 * 输出：机械切换阶段返回true，其他阶段或参数无效返回false。
 */
static bool A_Can_IsMechanicalSwitching(const Gas_System *system)
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
 * 函数名：A_Can_RecordWriteAttempt。
 * 说明：记录一条已经通过协议帧格式和CRC检查的CAN写请求，供连续诊断地址读取。
 * 输入：context为CAN上下文；address为写地址；value为32位原始写值。
 * 输出：无；更新最近地址、原始值和累计序号。
 */
static void A_Can_RecordWriteAttempt(A_Can_Context *context,
                                     uint16_t address,
                                     uint32_t value)
{
    context->last_write_address = address;
    context->last_write_value = value;
    context->write_sequence++;
}

/*
 * 函数名：A_Can_SetImmediateWriteResult。
 * 说明：记录无需业务层异步处理的CAN写结果，并立即加入功能码6发送队列。
 * 输入：context为CAN上下文；request为原写请求；result为基础结果；detail为详细原因。
 * 输出：响应成功入队时返回true；发送队列已满时返回false并累计丢弃数。
 */
static bool A_Can_SetImmediateWriteResult(A_Can_Context *context,
                                          const F_Can_Request *request,
                                          A_Can_Write_Result result,
                                          A_Can_Write_Detail detail)
{
    uint32_t packed = A_Can_PackWriteResult(result, detail); // 当前作用域变量，用于保存当前处理数据。

    context->last_write_result = packed;
    if (!F_CanProtocol_QueueWriteResponse(&context->function,
                                          request->source_type,
                                          request->source_address,
                                          request->data_address,
                                          packed))
    {
        context->response_drop_count++;
        return false;
    }
    return true;
}

/*
 * 函数名：A_Can_BeginDeferredWrite。
 * 说明：保存需要气源业务层执行后才能应答的写请求来源，保证成功响应代表最终完成。
 * 输入：context为CAN上下文；request为原写请求。
 * 输出：没有其他延迟写请求时返回true；已有请求未完成时返回false。
 */
static bool A_Can_BeginDeferredWrite(A_Can_Context *context,
                                     const F_Can_Request *request)
{
    if (context->deferred_write_pending || context->deferred_write_response_ready)
    {
        return false;
    }
    context->deferred_write_address = request->data_address;
    context->deferred_write_target_type = request->source_type;
    context->deferred_write_target_address = request->source_address;
    context->deferred_write_pending = true;
    return true;
}

/*
 * 函数名：A_Can_CompleteDeferredWrite。
 * 说明：完成当前延迟写请求并保存最终结果，实际功能码6入队由CAN周期任务重试完成。
 * 输入：context为CAN上下文；result为基础结果；detail为详细原因。
 * 输出：存在匹配的延迟写请求并完成记录时返回true，否则返回false。
 */
static bool A_Can_CompleteDeferredWrite(A_Can_Context *context,
                                        A_Can_Write_Result result,
                                        A_Can_Write_Detail detail)
{
    if ((context == NULL) || !context->deferred_write_pending)
    {
        return false;
    }
    context->deferred_write_result = A_Can_PackWriteResult(result, detail);
    context->last_write_result = context->deferred_write_result;
    context->deferred_write_pending = false;
    context->deferred_write_response_ready = true;
    return true;
}

/*
 * 函数名：A_Can_QueueDeferredWriteResponse。
 * 说明：把已经完成的延迟写最终结果加入协议发送队列，队列暂满时保留到下一周期重试。
 * 输入：context为CAN上下文。
 * 输出：没有待发结果或结果成功入队时返回true，队列暂满时返回false。
 */
static bool A_Can_QueueDeferredWriteResponse(A_Can_Context *context)
{
    if (!context->deferred_write_response_ready)
    {
        return true;
    }
    if (!F_CanProtocol_QueueWriteResponse(&context->function,
                                          context->deferred_write_target_type,
                                          context->deferred_write_target_address,
                                          context->deferred_write_address,
                                          context->deferred_write_result))
    {
        return false;
    }
    context->deferred_write_response_ready = false;
    return true;
}

/*
 * 函数名：A_Can_GetCylinderMask。
 * 说明：按指定类别汇总六瓶布尔状态为bit0～bit5位图。
 * 输入：system为系统状态；kind为0进气阀、1排气阀、2测试阀、3测试合格标志。
 * 输出：返回六位状态位图。
 */
static uint32_t A_Can_GetCylinderMask(const Gas_System *system, uint8_t kind)
{
    uint32_t mask = 0U; // 当前CAN读取函数使用的六瓶状态位图；bit0～bit5对应1～6号瓶，0表示对应状态未置位，1表示已置位，bit6～bit31保留为0。
    uint8_t index; // 当前作用域变量，用于保存遍历索引。

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        bool set = false; // 当前气瓶状态置位标志；使用范围：本次六瓶状态汇总循环内；取值范围：false/true，false表示kind指定的阀门关闭或测试未合格，true表示阀门开启或测试合格。
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
    uint16_t offset; // 当前作用域变量，用于保存数据偏移量。

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
        case A_CAN_ADDRESS_COMMAND: *value = 0U; return true;
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
        case A_CAN_ADDRESS_LAST_WRITE_ADDRESS: *value = context->last_write_address; return true;
        case A_CAN_ADDRESS_LAST_WRITE_RESULT: *value = context->last_write_result; return true;
        case A_CAN_ADDRESS_LAST_WRITE_VALUE: *value = context->last_write_value; return true;
        case A_CAN_ADDRESS_WRITE_SEQUENCE: *value = context->write_sequence; return true;
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

    if ((address >= A_CAN_ADDRESS_EXHAUST_CONTROL_BASE) &&
        (address < (A_CAN_ADDRESS_EXHAUST_CONTROL_BASE + GAS_CYLINDER_COUNT)))
    {
        *value = system->cylinder[address - A_CAN_ADDRESS_EXHAUST_CONTROL_BASE].exhaust_cmd ? 1U : 0U;
        return true;
    }
    if ((address >= A_CAN_ADDRESS_TEST_CONTROL_BASE) &&
        (address < (A_CAN_ADDRESS_TEST_CONTROL_BASE + GAS_CYLINDER_COUNT)))
    {
        *value = system->cylinder[address - A_CAN_ADDRESS_TEST_CONTROL_BASE].test_cmd ? 1U : 0U;
        return true;
    }
    if ((address >= A_CAN_ADDRESS_DISABLE_CONTROL_BASE) &&
        (address < (A_CAN_ADDRESS_DISABLE_CONTROL_BASE + GAS_CYLINDER_COUNT)))
    {
        *value = (system->cylinder[address - A_CAN_ADDRESS_DISABLE_CONTROL_BASE].state == GAS_CYL_DISABLED) ? 1U : 0U;
        return true;
    }
    if ((address >= A_CAN_ADDRESS_QUALIFY_CONTROL_BASE) &&
        (address < (A_CAN_ADDRESS_QUALIFY_CONTROL_BASE + GAS_CYLINDER_COUNT)))
    {
        *value = system->cylinder[address - A_CAN_ADDRESS_QUALIFY_CONTROL_BASE].qualification_passed ? 1U : 0U;
        return true;
    }
    return false;
}

/*
 * 函数名：A_Can_AssignParameterCandidate。
 * 说明：把一个公开CAN参数写值赋给正式参数副本，并完成该字段可编码范围的精确检查。
 * 输入：candidate为参数副本；address为参数地址；raw为32位原始写值；detail为错误明细输出指针。
 * 输出：地址和值均有效并完成赋值时返回true，否则返回false并输出具体原因。
 */
static bool A_Can_AssignParameterCandidate(Gas_Config *candidate,
                                           uint16_t address,
                                           uint32_t raw,
                                           A_Can_Write_Detail *detail)
{
    float pressure; // 当前作用域变量，用于保存压力值。
    uint32_t minimum_fresh_ms = GAS_SENSOR_POLL_INTERVAL_MS * GAS_PRESSURE_SENSOR_COUNT; // 当前作用域变量，用于保存毫秒时间值。

    if ((address >= A_CAN_ADDRESS_SWITCH_PRESSURE) &&
        (address <= A_CAN_ADDRESS_PRESSURE_MAX))
    {
        if (!A_Can_FloatRawIsFinite(raw))
        {
            *detail = A_CAN_WRITE_DETAIL_VALUE_FORMAT;
            return false;
        }
        pressure = A_Can_RawToFloat(raw);
        if (pressure < 0.001F)
        {
            *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_LOW;
            return false;
        }
        if (pressure > GAS_CONFIG_PRESSURE_MAX_ENCODED_MPA)
        {
            *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_HIGH;
            return false;
        }
        pressure = (float) ((uint32_t) ((pressure * GAS_CONFIG_PRESSURE_SCALE) + 0.5F)) /
                   GAS_CONFIG_PRESSURE_SCALE;
        // EEPROM以0.001 MPa定点数保存，先规范化再应用，保证立即读回值与重新上电后的值完全一致。
        if (address == A_CAN_ADDRESS_SWITCH_PRESSURE) { candidate->switch_pressure_mpa = pressure; }
        else if (address == A_CAN_ADDRESS_SWITCH_RELEASE) { candidate->switch_release_mpa = pressure; }
        else if (address == A_CAN_ADDRESS_READY_MIN_PRESSURE) { candidate->ready_min_pressure_mpa = pressure; }
        else { candidate->pressure_max_mpa = pressure; }
        return true;
    }

    switch (address)
    {
        case A_CAN_ADDRESS_VALVE_PULL_IN_TIME:
            if (raw == 0U) { *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_LOW; return false; }
            if (raw > GAS_CONFIG_TIME_MAX_MS) { *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_HIGH; return false; }
            candidate->valve_pull_in_time_ms = raw;
            return true;
        case A_CAN_ADDRESS_LOW_CONFIRM_TIME:
            if (raw > GAS_CONFIG_TIME_MAX_MS) { *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_HIGH; return false; }
            candidate->low_confirm_time_ms = raw;
            return true;
        case A_CAN_ADDRESS_LOW_CONFIRM_SAMPLES:
            if (raw == 0U) { *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_LOW; return false; }
            if (raw > GAS_CONFIG_SAMPLE_MAX_COUNT) { *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_HIGH; return false; }
            candidate->low_confirm_samples = (uint16_t) raw;
            return true;
        case A_CAN_ADDRESS_VALVE_CLOSE_WAIT:
            if (raw > GAS_CONFIG_TIME_MAX_MS) { *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_HIGH; return false; }
            candidate->valve_close_wait_ms = raw;
            return true;
        case A_CAN_ADDRESS_VALVE_OPEN_WAIT:
            if (raw > GAS_CONFIG_TIME_MAX_MS) { *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_HIGH; return false; }
            candidate->valve_open_wait_ms = raw;
            return true;
        case A_CAN_ADDRESS_PRESSURE_FRESH:
            if (raw < minimum_fresh_ms) { *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_LOW; return false; }
            if (raw > GAS_CONFIG_TIME_MAX_MS) { *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_HIGH; return false; }
            candidate->pressure_fresh_ms = raw;
            return true;
        default:
            *detail = A_CAN_WRITE_DETAIL_ADDRESS_NOT_FOUND;
            return false;
    }
}

/*
 * 函数名：A_Can_StartConfigWrite。
 * 说明：校验单地址候选参数并建立等待EEPROM保存和业务应用的延迟写请求。
 * 输入：context为CAN上下文；system为系统状态；request为原写请求；candidate为待校验参数。
 * 输出：需要立即应答时返回true并写入result和detail；成功建立延迟请求时返回false。
 */
static bool A_Can_StartConfigWrite(A_Can_Context *context,
                                   const Gas_System *system,
                                   const F_Can_Request *request,
                                   const Gas_Config *candidate,
                                   A_Can_Write_Result *result,
                                   A_Can_Write_Detail *detail)
{
    A_Gas_Config_Validation validation = A_GasConfig_Validate(candidate); // 当前作用域变量，用于保存参数校验结果。

    if (validation != A_GAS_CONFIG_VALID)
    {
        context->config_result = (validation == A_GAS_CONFIG_INVALID_RELATION) ?
            GAS_EXTERNAL_CONFIG_INVALID_RELATION : GAS_EXTERNAL_CONFIG_INVALID_RANGE;
        *result = (validation == A_GAS_CONFIG_INVALID_RELATION) ?
            A_CAN_WRITE_EXECUTION_ERROR : A_CAN_WRITE_VALUE_ERROR;
        *detail = (validation == A_GAS_CONFIG_INVALID_RELATION) ?
            A_CAN_WRITE_DETAIL_RELATION_CONFLICT : A_CAN_WRITE_DETAIL_VALUE_FORMAT;
        return true;
    }
    if (A_Can_IsMechanicalSwitching(system))
    {
        context->config_result = GAS_EXTERNAL_CONFIG_SYSTEM_BUSY;
        *result = A_CAN_WRITE_EXECUTION_ERROR;
        *detail = A_CAN_WRITE_DETAIL_SWITCHING_BUSY;
        return true;
    }
    if (memcmp(candidate, &context->staged_config, sizeof(*candidate)) == 0)
    {
        context->config_result = GAS_EXTERNAL_CONFIG_SUCCESS;
        *result = A_CAN_WRITE_SUCCESS;
        *detail = A_CAN_WRITE_DETAIL_NONE;
        return true;
    }
    if (!A_Can_BeginDeferredWrite(context, request))
    {
        *result = A_CAN_WRITE_EXECUTION_ERROR;
        *detail = A_CAN_WRITE_DETAIL_REQUEST_BUSY;
        return true;
    }

    context->pending_config = *candidate;
    context->config_pending = true;
    context->config_result = GAS_EXTERNAL_CONFIG_PENDING;
    return false;
}

/*
 * 函数名：A_Can_DecodeControlAddress。
 * 说明：把连续的四组六瓶控制地址转换为控制类型和从0开始的气瓶索引。
 * 输入：address为CAN数据地址；request为控制请求输出指针。
 * 输出：地址属于人工控制区时返回true，否则返回false。
 */
static bool A_Can_DecodeControlAddress(uint16_t address,
                                       A_Can_Control_Request *request)
{
    if ((address >= A_CAN_ADDRESS_EXHAUST_CONTROL_BASE) &&
        (address < (A_CAN_ADDRESS_EXHAUST_CONTROL_BASE + GAS_CYLINDER_COUNT)))
    {
        request->type = A_CAN_CONTROL_EXHAUST;
        request->index = (uint8_t) (address - A_CAN_ADDRESS_EXHAUST_CONTROL_BASE);
        return true;
    }
    if ((address >= A_CAN_ADDRESS_TEST_CONTROL_BASE) &&
        (address < (A_CAN_ADDRESS_TEST_CONTROL_BASE + GAS_CYLINDER_COUNT)))
    {
        request->type = A_CAN_CONTROL_TEST;
        request->index = (uint8_t) (address - A_CAN_ADDRESS_TEST_CONTROL_BASE);
        return true;
    }
    if ((address >= A_CAN_ADDRESS_DISABLE_CONTROL_BASE) &&
        (address < (A_CAN_ADDRESS_DISABLE_CONTROL_BASE + GAS_CYLINDER_COUNT)))
    {
        request->type = A_CAN_CONTROL_DISABLE;
        request->index = (uint8_t) (address - A_CAN_ADDRESS_DISABLE_CONTROL_BASE);
        return true;
    }
    if ((address >= A_CAN_ADDRESS_QUALIFY_CONTROL_BASE) &&
        (address < (A_CAN_ADDRESS_QUALIFY_CONTROL_BASE + GAS_CYLINDER_COUNT)))
    {
        request->type = A_CAN_CONTROL_QUALIFICATION;
        request->index = (uint8_t) (address - A_CAN_ADDRESS_QUALIFY_CONTROL_BASE);
        return true;
    }
    return false;
}

/*
 * 函数名：A_Can_WriteValue。
 * 说明：处理一条CAN写请求；简单操作立即给出结果，参数和阀门操作等待业务层最终完成后应答。
 * 输入：context为CAN上下文；system为系统状态；comm_mode为通讯模式；request为原写请求；result和detail为立即结果输出。
 * 输出：需要立即发送功能码6响应时返回true；已经建立延迟业务请求时返回false。
 */
static bool A_Can_WriteValue(A_Can_Context *context,
                             const Gas_System *system,
                             gas_external_comm_mode_t comm_mode,
                             const F_Can_Request *request,
                             A_Can_Write_Result *result,
                             A_Can_Write_Detail *detail)
{
    Gas_Config candidate; // 当前作用域变量，用于保存待校验候选值。
    A_Can_Control_Request control = {A_CAN_CONTROL_NONE, 0U, false}; // 当前作用域变量，用于保存当前处理数据。

    *result = A_CAN_WRITE_SUCCESS;
    *detail = A_CAN_WRITE_DETAIL_NONE;
    if (context->deferred_write_pending || context->deferred_write_response_ready)
    {
        *result = A_CAN_WRITE_EXECUTION_ERROR;
        *detail = A_CAN_WRITE_DETAIL_REQUEST_BUSY;
        return true;
    }

    if (((request->data_address >= A_CAN_ADDRESS_SWITCH_PRESSURE) &&
         (request->data_address <= A_CAN_ADDRESS_PRESSURE_MAX)) ||
        ((request->data_address >= A_CAN_ADDRESS_VALVE_PULL_IN_TIME) &&
         (request->data_address <= A_CAN_ADDRESS_PRESSURE_FRESH)))
    {
        candidate = context->staged_config;
        if (!A_Can_AssignParameterCandidate(&candidate,
                                            request->data_address,
                                            request->value,
                                            detail))
        {
            context->config_result = GAS_EXTERNAL_CONFIG_INVALID_RANGE;
            *result = A_CAN_WRITE_VALUE_ERROR;
            return true;
        }
        return A_Can_StartConfigWrite(context,
                                      system,
                                      request,
                                      &candidate,
                                      result,
                                      detail);
    }

    if (request->data_address == A_CAN_ADDRESS_COMMAND)
    {
        context->command_result = GAS_EXTERNAL_RESULT_INVALID_COMMAND;
        *result = A_CAN_WRITE_ADDRESS_ERROR;
        *detail = A_CAN_WRITE_DETAIL_DEPRECATED;
        return true;
        // 地址保留用于旧上位机兼容，但V1.08不再接受任何人工整机启停请求。
    }

    if (request->data_address == A_CAN_ADDRESS_CONFIG_COMMIT)
    {
        *result = A_CAN_WRITE_ADDRESS_ERROR;
        *detail = A_CAN_WRITE_DETAIL_DEPRECATED;
        return true;
    }

    if (request->data_address == A_CAN_ADDRESS_CONFIG_DEFAULT)
    {
        if (request->value != A_CAN_CONFIG_DEFAULT_KEY)
        {
            context->config_result = GAS_EXTERNAL_CONFIG_INVALID_KEY;
            *result = A_CAN_WRITE_VALUE_ERROR;
            *detail = A_CAN_WRITE_DETAIL_VALUE_FORMAT;
            return true;
        }
        candidate = context->staged_config;
        A_GasConfig_LoadDefaults(&candidate);
        candidate.low_warning_pressure_mpa = context->staged_config.low_warning_pressure_mpa;
        candidate.manual_exhaust_time_ms = context->staged_config.manual_exhaust_time_ms;
        candidate.test_valve_max_time_ms = context->staged_config.test_valve_max_time_ms;
        // 恢复默认只改变CAN公开的10项，密码页管理的三项安全参数保持不变。
        return A_Can_StartConfigWrite(context,
                                      system,
                                      request,
                                      &candidate,
                                      result,
                                      detail);
    }

    if (request->data_address == A_CAN_ADDRESS_LOG_INDEX)
    {
        if (request->value > 0xFFFFU)
        {
            *result = A_CAN_WRITE_VALUE_ERROR;
            *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_HIGH;
            return true;
        }
        context->selected_log_index = (uint16_t) request->value;
        return true;
    }
    if (request->data_address == A_CAN_ADDRESS_LOG_COMMAND)
    {
        if (request->value != 1U)
        {
            context->log_result = GAS_EXTERNAL_LOG_INVALID_COMMAND;
            *result = A_CAN_WRITE_VALUE_ERROR;
            *detail = A_CAN_WRITE_DETAIL_VALUE_FORMAT;
            return true;
        }
        if (context->log_read_pending)
        {
            context->log_result = GAS_EXTERNAL_LOG_BUSY;
            *result = A_CAN_WRITE_EXECUTION_ERROR;
            *detail = A_CAN_WRITE_DETAIL_REQUEST_BUSY;
            return true;
        }
        context->pending_log_index = context->selected_log_index;
        context->log_read_pending = true;
        context->log_result = GAS_EXTERNAL_LOG_PENDING;
        return true;
    }

    if (A_Can_DecodeControlAddress(request->data_address, &control))
    {
        if (request->value > 1U)
        {
            *result = A_CAN_WRITE_VALUE_ERROR;
            *detail = A_CAN_WRITE_DETAIL_VALUE_TOO_HIGH;
            return true;
        }
        if (A_Can_IsMechanicalSwitching(system))
        {
            *result = A_CAN_WRITE_EXECUTION_ERROR;
            *detail = A_CAN_WRITE_DETAIL_SWITCHING_BUSY;
            return true;
        }
        if (!A_Can_BeginDeferredWrite(context, request))
        {
            *result = A_CAN_WRITE_EXECUTION_ERROR;
            *detail = A_CAN_WRITE_DETAIL_REQUEST_BUSY;
            return true;
        }
        control.enabled = (request->value != 0U);
        context->pending_control = control;
        context->control_pending = true;
        return false;
    }

    {
        uint32_t read_value; // 当前作用域变量，用于保存当前处理值。
        if (A_Can_ReadValue(context,
                            system,
                            comm_mode,
                            request->data_address,
                            &read_value))
        {
            *result = A_CAN_WRITE_ADDRESS_ERROR;
            *detail = A_CAN_WRITE_DETAIL_READ_ONLY;
        }
        else
        {
            *result = A_CAN_WRITE_ADDRESS_ERROR;
            *detail = A_CAN_WRITE_DETAIL_ADDRESS_NOT_FOUND;
        }
    }
    return true;
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
    bool success; // success 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示操作失败，true表示操作成功。
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
        uint16_t address = context->read_response_address; // 当前作用域变量，用于保存存储或寄存器地址。
        uint32_t value; // 当前作用域变量，用于保存当前处理值。

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
    F_Can_Request request; // 当前作用域变量，用于保存待处理请求。

    if ((context == NULL) || (system == NULL) || !context->ready)
    {
        return;
    }
    F_CanProtocol_Task(&context->function);
    if (!A_Can_QueueDeferredWriteResponse(context))
    {
        return;
    }
    // EEPROM或阀门业务已经给出最终结果后，在这里持续重试入队，成功响应不会因发送队列短时占满而丢失。
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
        A_Can_Write_Result result; // 当前作用域变量，用于保存操作结果。
        A_Can_Write_Detail detail; // 当前作用域变量，用于保存队列尾位置。
        bool immediate; // immediate 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示延后处理，true表示立即处理。

        A_Can_RecordWriteAttempt(context, request.data_address, request.value);
        immediate = A_Can_WriteValue(context,
                                     system,
                                     comm_mode,
                                     &request,
                                     &result,
                                     &detail);
        if (immediate)
        {
            (void) A_Can_SetImmediateWriteResult(context, &request, result, detail);
        }
    }
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
    A_Can_Write_Result write_result; // 当前作用域变量，用于保存操作结果。
    A_Can_Write_Detail detail; // 当前作用域变量，用于保存队列尾位置。

    if (context == NULL)
    {
        return;
    }
    context->config_result = result;
    if (result == GAS_EXTERNAL_CONFIG_SUCCESS)
    {
        write_result = A_CAN_WRITE_SUCCESS;
        detail = A_CAN_WRITE_DETAIL_NONE;
    }
    else if (result == GAS_EXTERNAL_CONFIG_STORAGE_FAILED)
    {
        write_result = A_CAN_WRITE_EXECUTION_ERROR;
        detail = A_CAN_WRITE_DETAIL_STORAGE_FAILED;
    }
    else if (result == GAS_EXTERNAL_CONFIG_INVALID_RELATION)
    {
        write_result = A_CAN_WRITE_EXECUTION_ERROR;
        detail = A_CAN_WRITE_DETAIL_RELATION_CONFLICT;
    }
    else if (result == GAS_EXTERNAL_CONFIG_SYSTEM_BUSY)
    {
        write_result = A_CAN_WRITE_EXECUTION_ERROR;
        detail = A_CAN_WRITE_DETAIL_STATE_DISALLOWED;
    }
    else
    {
        write_result = A_CAN_WRITE_VALUE_ERROR;
        detail = A_CAN_WRITE_DETAIL_VALUE_FORMAT;
    }
    (void) A_Can_CompleteDeferredWrite(context, write_result, detail);
}

/*
 * 函数名：A_Can_TakeControlRequest。
 * 说明：取出并清除一条已经通过地址和值检查的CAN人工控制请求。
 * 输入：context为CAN上下文；request为控制请求输出指针。
 * 输出：存在待处理控制请求时返回true，否则返回false。
 */
bool A_Can_TakeControlRequest(A_Can_Context *context,
                              A_Can_Control_Request *request)
{
    if ((context == NULL) || (request == NULL) || !context->control_pending)
    {
        return false;
    }
    *request = context->pending_control;
    context->pending_control.type = A_CAN_CONTROL_NONE;
    context->control_pending = false;
    return true;
}

/*
 * 函数名：A_Can_CompleteControlRequest。
 * 说明：保存CAN人工控制最终执行结果，并组织与原请求对应的功能码6响应。
 * 输入：context为CAN上下文；result为基础结果码；detail为详细原因码。
 * 输出：无；完整结果保存在诊断地址并等待协议层发送。
 */
void A_Can_CompleteControlRequest(A_Can_Context *context,
                                  A_Can_Write_Result result,
                                  A_Can_Write_Detail detail)
{
    (void) A_Can_CompleteDeferredWrite(context, result, detail);
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
