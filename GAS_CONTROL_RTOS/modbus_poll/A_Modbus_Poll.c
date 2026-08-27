#include "A_Modbus_Poll.h"

#include <limits.h>
#include <string.h>

/*
 * 函数名：A_ModbusPoll_TimeReached。
 * 说明：使用无符号毫秒差判断当前时间是否已经到达截止时间。
 * 输入：now_ms 为当前时间；deadline_ms 为截止时间。
 * 输出：已经到达或超过截止时间时返回 true，否则返回 false。
 */
static bool A_ModbusPoll_TimeReached(uint32_t now_ms, uint32_t deadline_ms)
{
    return ((int32_t) (now_ms - deadline_ms) >= 0);
}

/*
 * 函数名：A_ModbusPoll_DecodePressure。
 * 说明：按照传感器AB CD字节序把两个Modbus寄存器转换为float32压力，并区分有效与超量程质量。
 * 输入：data 为四字节数据区；length 为数据长度；maximum_mpa 为运行时压力上限；pressure_mpa 和 quality 为输出指针。
 * 输出：浮点数据有限、非负且处于诊断显示范围时返回 true，并输出压力及质量；非法数据返回 false。
 */
bool A_ModbusPoll_DecodePressure(const uint8_t *data,
                                 size_t length,
                                 float maximum_mpa,
                                 float *pressure_mpa,
                                 gas_pressure_quality_t *quality)
{
    uint32_t raw_value;

    if ((data == NULL) || (pressure_mpa == NULL) || (quality == NULL) || (length != 4U) ||
        !(maximum_mpa > 0.0F))
    {
        return false;
    }

#if (GAS_SENSOR_DATA_FORMAT == 2U)
    raw_value = ((uint32_t) data[0] << 24U) |
                ((uint32_t) data[1] << 16U) |
                ((uint32_t) data[2] << 8U) |
                (uint32_t) data[3];
#else
#error "当前内部 Modbus 轮询仅启用传感器 float AB CD 数据格式"
#endif

    (void) memcpy(pressure_mpa, &raw_value, sizeof(raw_value));
    if (((raw_value & 0x80000000UL) != 0UL) ||
        ((raw_value & 0x7F800000UL) == 0x7F800000UL) ||
        (*pressure_mpa > GAS_PRESSURE_DISPLAY_MAX_MPA))
    {
        // 负数、NaN、无穷大和超出画面表达能力的异常值不具备可靠诊断意义。
        return false;
    }

    *quality = (*pressure_mpa > maximum_mpa) ?
            GAS_PRESSURE_OUT_OF_RANGE : GAS_PRESSURE_VALID;
    // 超过配置上限时保留原始压力，仅把质量标记为超量程，状态机仍不会把它当作有效控制输入。
    return true;
}

/*
 * 函数名：A_ModbusPoll_RecordFailure。
 * 说明：记录一次气瓶压力或总压力传感器通信失败，并在达到门限时设置报警；气瓶七状态由气源业务层统一维护。
 * 输入：system 为系统状态；index 为发生失败的压力传感器索引，0～5对应气瓶，6对应总压力。
 * 输出：无；更新对应传感器的失败计数、压力质量和报警，必要时更新非工作瓶状态。
 */
static void A_ModbusPoll_RecordFailure(Gas_System *system, uint8_t index)
{
    Gas_Cylinder *cylinder;
    Gas_Total_Pressure *total_pressure;

    if ((system == NULL) || (index >= GAS_PRESSURE_SENSOR_COUNT))
    {
        return;
    }

    if (index == GAS_TOTAL_PRESSURE_SENSOR_INDEX)
    {
        total_pressure = &system->total_pressure;
        // 总压力传感器只维护数据质量和公共通信报警，不改变任何气瓶工作状态。
        if (total_pressure->comm_fail_count < UINT8_MAX)
        {
            total_pressure->comm_fail_count++;
        }
        if (total_pressure->comm_fail_count >= GAS_SENSOR_COMM_WARN_COUNT)
        {
            system->alarm_bits |= GAS_ALARM_SENSOR_COMM;
        }
        if (total_pressure->comm_fail_count >= GAS_SENSOR_COMM_FAULT_COUNT)
        {
            total_pressure->pressure_quality = GAS_PRESSURE_INVALID;
        }
        return;
    }

    cylinder = &system->cylinder[index];

    if (cylinder->comm_fail_count < UINT8_MAX)
    {
        cylinder->comm_fail_count++;
    }
    if (cylinder->comm_fail_count >= GAS_SENSOR_COMM_WARN_COUNT)
    {
        system->alarm_bits |= GAS_ALARM_SENSOR_COMM;
    }

    if (cylinder->comm_fail_count >= GAS_SENSOR_COMM_FAULT_COUNT)
    {
        cylinder->pressure_quality = GAS_PRESSURE_INVALID;
        if (index == system->active_index)
        {
            // 工作瓶压力失联属于立即可见故障，由气源状态机决定是否停止或切换。
            system->alarm_bits |= GAS_ALARM_ACTIVE_SENSOR;
        }
        else if (cylinder->state != GAS_CYL_DISABLED)
        {
            // 非工作瓶退回初始化，恢复有效压力后再重新判断是否具备待用条件。
            cylinder->state = GAS_CYL_INIT;
        }
    }
}

/*
 * 函数名：A_ModbusPoll_Init。
 * 说明：初始化内部 Modbus 主站、1～7号传感器地址、六瓶压力和总压力初始质量。
 * 输入：context 为轮询应用上下文；platform 为硬件平台；system 为系统状态。
 * 输出：SCI1 主站硬件绑定成功时返回 true，否则返回 false。
 */
bool A_ModbusPoll_Init(A_Modbus_Poll_Context *context,
                       H_Gas_Platform_Context *platform,
                       Gas_System *system)
{
    uint8_t i;

    if ((context == NULL) || (platform == NULL) || (system == NULL))
    {
        return false;
    }

    (void) memset(context, 0, sizeof(*context));
    context->sensor_addresses[0] = GAS_SENSOR_ADDRESS_1;
    context->sensor_addresses[1] = GAS_SENSOR_ADDRESS_2;
    context->sensor_addresses[2] = GAS_SENSOR_ADDRESS_3;
    context->sensor_addresses[3] = GAS_SENSOR_ADDRESS_4;
    context->sensor_addresses[4] = GAS_SENSOR_ADDRESS_5;
    context->sensor_addresses[5] = GAS_SENSOR_ADDRESS_6;
    context->sensor_addresses[GAS_TOTAL_PRESSURE_SENSOR_INDEX] = GAS_SENSOR_ADDRESS_TOTAL;
    // 轮询索引 0～5 固定映射气瓶 1～6，索引 6 固定映射地址 7 的总压力传感器。

    for (i = 0U; i < GAS_CYLINDER_COUNT; ++i)
    {
        system->cylinder[i].modbus_address = context->sensor_addresses[i];
        system->cylinder[i].pressure_quality = GAS_PRESSURE_INVALID;
        system->cylinder[i].comm_fail_count = 0U;
    }

    system->total_pressure.modbus_address = GAS_SENSOR_ADDRESS_TOTAL;
    system->total_pressure.pressure_quality = GAS_PRESSURE_INVALID;
    system->total_pressure.comm_fail_count = 0U;

    context->ready = F_ModbusPoll_Init(&context->master, platform);
    return context->ready;
}

/*
 * 函数名：A_ModbusPoll_Task。
 * 说明：轮询1～7号地址的输入寄存器0和1，解析AB CD浮点压力并分别维护六瓶压力和总压力质量。
 * 输入：context 为轮询应用上下文；system 为系统状态；config 为运行参数；now_ms 为当前毫秒计数。
 * 输出：无；通过 system 更新压力、状态、通信计数和报警位。
 */
void A_ModbusPoll_Task(A_Modbus_Poll_Context *context,
                       Gas_System *system,
                       const Gas_Config *config,
                       uint32_t now_ms)
{
    F_Modbus_Poll_Result result;
    const uint8_t *payload;
    size_t payload_length;
    float pressure_mpa;
    gas_pressure_quality_t pressure_quality;
    uint8_t i;

    if ((context == NULL) || (system == NULL) || (config == NULL) || !context->ready)
    {
        if (system != NULL)
        {
            system->alarm_bits |= GAS_ALARM_PLATFORM_NOT_READY;
        }
        return;
    }

    F_ModbusPoll_Task(&context->master, now_ms);

    if (F_ModbusPoll_TakeResult(&context->master, &result, &payload, &payload_length))
    {
        // 协议和浮点格式正确时保留样本；超出配置上限的压力以独立质量保存，供红色诊断显示。
        if ((result == MODBUS_POLL_RESULT_OK) &&
            A_ModbusPoll_DecodePressure(payload,
                                        payload_length,
                                        config->pressure_max_mpa,
                                        &pressure_mpa,
                                        &pressure_quality))
        {
            if (context->pending_index == GAS_TOTAL_PRESSURE_SENSOR_INDEX)
            {
                system->total_pressure.pressure_mpa = pressure_mpa;
                system->total_pressure.pressure_quality = pressure_quality;
                system->total_pressure.pressure_timestamp_ms = now_ms;
                system->total_pressure.comm_fail_count = 0U;
                // 总压力仅供显示和诊断使用，不参与六瓶状态机及自动切换判断。
            }
            else
            {
                Gas_Cylinder *cylinder = &system->cylinder[context->pending_index];

                cylinder->pressure_mpa = pressure_mpa;
                cylinder->pressure_quality = pressure_quality;
                cylinder->pressure_timestamp_ms = now_ms;
                cylinder->comm_fail_count = 0U;
                // 只更新压力数据和质量，初始化、待测试、待用等七状态由A_Gas_Control统一判断。
            }
        }
        else
        {
            A_ModbusPoll_RecordFailure(system, context->pending_index);
        }

        context->next_poll_ms = now_ms + GAS_SENSOR_POLL_INTERVAL_MS;
        context->poll_index = (uint8_t) ((context->pending_index + 1U) % GAS_PRESSURE_SENSOR_COUNT);
        // 无论成功或失败都轮转到下一地址，避免单个离线设备长期占用内部总线。
    }

    for (i = 0U; i < GAS_CYLINDER_COUNT; ++i)
    {
        Gas_Cylinder *cylinder = &system->cylinder[i];

        if (((cylinder->pressure_quality == GAS_PRESSURE_VALID) ||
             (cylinder->pressure_quality == GAS_PRESSURE_OUT_OF_RANGE)) &&
            A_ModbusPoll_TimeReached(now_ms,
                                     cylinder->pressure_timestamp_ms + config->pressure_fresh_ms))
        {
            cylinder->pressure_quality = GAS_PRESSURE_STALE;
            // 超时样本保留最后数值用于诊断，但状态机不得再把它当作有效压力使用。
        }
    }

    if (((system->total_pressure.pressure_quality == GAS_PRESSURE_VALID) ||
         (system->total_pressure.pressure_quality == GAS_PRESSURE_OUT_OF_RANGE)) &&
        A_ModbusPoll_TimeReached(now_ms,
                                 system->total_pressure.pressure_timestamp_ms + config->pressure_fresh_ms))
    {
        system->total_pressure.pressure_quality = GAS_PRESSURE_STALE;
    }

    if ((context->master.state == MODBUS_POLL_STATE_IDLE) &&
        !context->master.result_pending && A_ModbusPoll_TimeReached(now_ms, context->next_poll_ms))
    {
        context->pending_index = context->poll_index;
        // 一次只发起一个地址的功能码 04 请求，结果由下一个任务周期异步收取。
        (void) F_ModbusPoll_StartRead(&context->master,
                                      context->sensor_addresses[context->pending_index],
                                      GAS_SENSOR_FUNCTION_CODE,
                                      GAS_SENSOR_PRESSURE_REGISTER,
                                      GAS_SENSOR_PRESSURE_REG_COUNT,
                                      now_ms,
                                      GAS_SENSOR_RESPONSE_TIMEOUT_MS);
    }
}
