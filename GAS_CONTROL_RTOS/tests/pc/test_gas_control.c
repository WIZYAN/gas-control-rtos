/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现气源控制、通信、日志、HMI和参数迁移的PC回归测试。
 */

#include "test_gas_control.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// 主机测试状态，集中保存 EEPROM 和 HMI 发送帧。
typedef struct
{
    uint8_t eeprom[AT24C256_CAPACITY_BYTES]; // 模拟 EEPROM 存储区。
    uint8_t hmi_tx[F_HMI_TX_MAX_SIZE];       // 最近一帧HMI发送数据，容量覆盖最长日志表格行。
    size_t hmi_tx_length;                    // 最近一帧 HMI 发送长度。
    uint32_t hmi_tx_count;                   // 模拟HMI累计发送帧数，用于识别分步查询期间的新帧。
    H_Can_Frame can_tx;                      // 最近一帧模拟CAN发送数据。
    uint32_t can_tx_count;                   // 模拟CAN累计发送帧数。
    bool all_valves_off_fail;                // 全关故障注入标志；使用范围：主机测试硬件模拟层；false表示写入成功，true表示模拟至少一路关阀失败。
    uint32_t all_valves_off_count;            // 模拟硬件全关调用次数，用于验证失败后的周期重试。
} Test_State;

static Test_State g_test_state; // 主机测试唯一状态实例。

/*
 * 函数名：Test_PushHmiFrame。
 * 说明：把一帧串口屏上传数据写入模拟SCI9接收环形缓冲区。
 * 输入：context为气源应用上下文；frame为只读帧；length为帧长度。
 * 输出：无；数据写入HMI硬件层接收缓存。
 */
static void Test_PushHmiFrame(A_Gas_Control_Context *context,
                              const uint8_t *frame,
                              size_t length);

/*
 * 函数名：H_GasPlatform_Init。
 * 说明：初始化模拟气源硬件上下文。
 * 输入：context 为硬件上下文输入输出指针。
 * 输出：参数有效时返回 true，否则返回 false。
 */
bool H_GasPlatform_Init(H_Gas_Platform_Context *context)
{
    if (context == NULL) return false;
    (void) memset(context, 0, sizeof(*context));
    context->sensor_uart_open = true;
    return true;
}

/*
 * 函数名：H_GasPlatform_Millis。
 * 说明：读取模拟毫秒计数。
 * 输入：context 为硬件上下文。
 * 输出：返回模拟毫秒计数。
 */
uint32_t H_GasPlatform_Millis(H_Gas_Platform_Context *context)
{
    return (context != NULL) ? context->millis : 0U;
}

/*
 * 函数名：H_GasPlatform_SensorTxStart。
 * 说明：模拟启动内部传感器发送。
 * 输入：context 为上下文；data 为数据；length 为长度。
 * 输出：参数有效时返回 true。
 */
bool H_GasPlatform_SensorTxStart(H_Gas_Platform_Context *context, const uint8_t *data, size_t length)
{
    return ((context != NULL) && (data != NULL) && (length > 0U));
}

/*
 * 函数名：H_GasPlatform_SensorRxStart。
 * 说明：模拟启动内部传感器接收。
 * 输入：context 为上下文；data 为缓冲区；length 为长度。
 * 输出：参数有效时返回 true。
 */
bool H_GasPlatform_SensorRxStart(H_Gas_Platform_Context *context, uint8_t *data, size_t length)
{
    return ((context != NULL) && (data != NULL) && (length > 0U));
}

/*
 * 函数名：H_GasPlatform_SensorAbort。
 * 说明：清除模拟传感器收发完成标志。
 * 输入：context 为硬件上下文。
 * 输出：无。
 */
void H_GasPlatform_SensorAbort(H_Gas_Platform_Context *context)
{
    if (context != NULL)
    {
        context->sensor_tx_done = false;
        context->sensor_rx_done = false;
    }
}

/*
 * 函数名：H_GasPlatform_SensorTxDone。
 * 说明：查询模拟发送完成状态。
 * 输入：context 为硬件上下文。
 * 输出：完成时返回 true。
 */
bool H_GasPlatform_SensorTxDone(const H_Gas_Platform_Context *context)
{
    return ((context != NULL) && context->sensor_tx_done);
}

/*
 * 函数名：H_GasPlatform_SensorRxDone。
 * 说明：查询模拟接收完成状态。
 * 输入：context 为硬件上下文。
 * 输出：完成时返回 true。
 */
bool H_GasPlatform_SensorRxDone(const H_Gas_Platform_Context *context)
{
    return ((context != NULL) && context->sensor_rx_done);
}

/*
 * 函数名：Test_WriteValve。
 * 说明：更新一类模拟阀门和吸合状态。
 * 输入：context 为上下文；state 为状态数组；index 为索引；on 为目标；pull_ms 为吸合时间。
 * 输出：参数有效时返回 true。
 */
static bool Test_WriteValve(H_Gas_Platform_Context *context,
                            bool *state,
                            uint8_t index,
                            bool on,
                            uint32_t pull_ms)
{
    if ((context == NULL) || (state == NULL) || (index >= GAS_CYLINDER_COUNT) || (on && (pull_ms == 0U)))
    {
        return false;
    }
    state[index] = on;
    if (on)
    {
        context->boost_state[index] = true;
        context->boost_deadline_ms[index] = context->millis + pull_ms;
        context->boost_interval_active[index] = true;
        context->boost_available_ms[index] = context->millis + GAS_VALVE_BOOST_MIN_INTERVAL_MS;
    }
    else if (!context->supply_state[index] && !context->exhaust_state[index] &&
             !context->test_state[index] && ((index != 0U) || !context->total_test_state))
    {
        context->boost_state[index] = false;
    }
    return true;
}

/*
 * 函数名：H_GasPlatform_WriteSupplyValve。
 * 说明：更新模拟进气阀。
 * 输入：context 为上下文；index 为索引；on 为状态；pull_ms 为吸合时间。
 * 输出：参数有效时返回 true。
 */
bool H_GasPlatform_WriteSupplyValve(H_Gas_Platform_Context *context, uint8_t index, bool on, uint32_t pull_ms)
{
    return Test_WriteValve(context, context->supply_state, index, on, pull_ms);
}

/*
 * 函数名：H_GasPlatform_WriteExhaustValve。
 * 说明：更新模拟排气阀。
 * 输入：context 为上下文；index 为索引；on 为状态；pull_ms 为吸合时间。
 * 输出：参数有效时返回 true。
 */
bool H_GasPlatform_WriteExhaustValve(H_Gas_Platform_Context *context, uint8_t index, bool on, uint32_t pull_ms)
{
    return Test_WriteValve(context, context->exhaust_state, index, on, pull_ms);
}

/*
 * 函数名：H_GasPlatform_WriteTestValve。
 * 说明：更新模拟测试阀。
 * 输入：context 为上下文；index 为索引；on 为状态；pull_ms 为吸合时间。
 * 输出：参数有效时返回 true。
 */
bool H_GasPlatform_WriteTestValve(H_Gas_Platform_Context *context, uint8_t index, bool on, uint32_t pull_ms)
{
    return Test_WriteValve(context, context->test_state, index, on, pull_ms);
}

/*
 * 函数名：H_GasPlatform_WriteTotalTestValve。
 * 说明：模拟VAL_CAL总测试阀，并复用0号阀组的吸合状态。
 * 输入：context为硬件上下文；on为目标状态；pull_ms为吸合时间。
 * 输出：参数有效且命令执行时返回true，否则返回false。
 */
bool H_GasPlatform_WriteTotalTestValve(H_Gas_Platform_Context *context,
                                       bool on,
                                       uint32_t pull_ms)
{
    if ((context == NULL) || (on && (pull_ms == 0U)))
    {
        return false;
    }
    context->total_test_state = on;
    if (on)
    {
        context->boost_state[0] = true;
        context->boost_deadline_ms[0] = context->millis + pull_ms;
        context->boost_interval_active[0] = true;
        context->boost_available_ms[0] = context->millis + GAS_VALVE_BOOST_MIN_INTERVAL_MS;
    }
    else if (!context->supply_state[0] && !context->exhaust_state[0] &&
             !context->test_state[0])
    {
        context->boost_state[0] = false;
    }
    return true;
}

/*
 * 函数名：H_GasPlatform_ValveTask。
 * 说明：到时关闭模拟 12 V 吸合状态。
 * 输入：context 为上下文；now_ms 为当前时间。
 * 输出：参数有效时返回 true。
 */
bool H_GasPlatform_ValveTask(H_Gas_Platform_Context *context, uint32_t now_ms)
{
    uint8_t index; // 当前作用域变量，用于保存遍历索引。
    if (context == NULL) return false;
    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        if (context->boost_interval_active[index] &&
            ((int32_t) (now_ms - context->boost_available_ms[index]) >= 0))
        {
            context->boost_interval_active[index] = false;
        }
        if (context->boost_state[index] && ((int32_t) (now_ms - context->boost_deadline_ms[index]) >= 0))
        {
            context->boost_state[index] = false;
        }
    }
    return true;
}

/*
 * 函数名：H_GasPlatform_AllValvesOff。
 * 说明：清除十八路分瓶模拟阀门和一路总测试阀状态。
 * 输入：context 为硬件上下文。
 * 输出：未注入故障时返回true并清除模拟阀门，注入故障时返回false并保留原状态。
 */
bool H_GasPlatform_AllValvesOff(H_Gas_Platform_Context *context)
{
    if (context == NULL)
    {
        return false;
    }
    g_test_state.all_valves_off_count++;
    if (g_test_state.all_valves_off_fail)
    {
        return false;
    }
    (void) memset(context->supply_state, 0, sizeof(context->supply_state));
    (void) memset(context->exhaust_state, 0, sizeof(context->exhaust_state));
    (void) memset(context->test_state, 0, sizeof(context->test_state));
    context->total_test_state = false;
    (void) memset(context->boost_state, 0, sizeof(context->boost_state));
    return true;
}

/*
 * 函数名：A_Storage_Init。
 * 说明：初始化模拟 EEPROM 服务。
 * 输入：context 为存储上下文。
 * 输出：参数有效时返回 true。
 */
bool A_Storage_Init(A_Storage_Context *context)
{
    if (context == NULL) return false;
    (void) memset(context, 0, sizeof(*context));
    context->initialized = true;
    return true;
}

/*
 * 函数名：A_Storage_Read。
 * 说明：读取模拟 EEPROM。
 * 输入：context 为上下文；address 为地址；data 为输出；length 为长度。
 * 输出：范围有效时返回 true。
 */
bool A_Storage_Read(A_Storage_Context *context, uint16_t address, uint8_t *data, size_t length)
{
    if ((context == NULL) || !context->initialized || (data == NULL) || (((size_t) address + length) > sizeof(g_test_state.eeprom))) return false;
    (void) memcpy(data, &g_test_state.eeprom[address], length);
    return true;
}

/*
 * 函数名：A_Storage_Write。
 * 说明：写入模拟 EEPROM。
 * 输入：context 为上下文；address 为地址；data 为输入；length 为长度。
 * 输出：范围有效时返回 true。
 */
bool A_Storage_Write(A_Storage_Context *context, uint16_t address, const uint8_t *data, size_t length)
{
    if ((context == NULL) || !context->initialized || (data == NULL) || (((size_t) address + length) > sizeof(g_test_state.eeprom))) return false;
    (void) memcpy(&g_test_state.eeprom[address], data, length);
    return true;
}

/*
 * 函数名：A_Storage_EraseRange。
 * 说明：把模拟EEPROM指定范围填充为0xFF，供V1.09日志物理清除测试使用。
 * 输入：context为存储上下文；address为起始地址；length为清除长度。
 * 输出：范围和上下文有效时返回true，否则返回false。
 */
bool A_Storage_EraseRange(A_Storage_Context *context, uint16_t address, size_t length)
{
    if ((context == NULL) || !context->initialized ||
        (((size_t) address + length) > sizeof(g_test_state.eeprom)))
    {
        return false;
    }
    (void) memset(&g_test_state.eeprom[address], 0xFF, length);
    return true;
}

/*
 * 函数名：H_Modbus_Init。
 * 说明：初始化主机测试使用的外部 Modbus 模拟硬件。
 * 输入：context 为外部 Modbus 硬件上下文输入输出指针。
 * 输出：参数有效时返回 true。
 */
bool H_Modbus_Init(H_Modbus_Context *context)
{
    if (context == NULL) return false;
    (void) memset(context, 0, sizeof(*context));
    context->uart_open = true;
    return true;
}

/*
 * 函数名：H_Modbus_Deinit。
 * 说明：关闭主机测试使用的模拟SCI0接口。
 * 输入：context为外部Modbus硬件层上下文。
 * 输出：参数有效时返回true，否则返回false。
 */
bool H_Modbus_Deinit(H_Modbus_Context *context)
{
    if (context == NULL) return false;
    context->uart_open = false;
    return true;
}

/*
 * 函数名：H_Modbus_TakeFrame。
 * 说明：从模拟 SCI0 接收缓存取出一帧外部 Modbus 请求。
 * 输入：context 为硬件上下文；buffer 为输出缓存；capacity 为容量；length 为长度输出指针。
 * 输出：存在完整且容量足够的请求帧时返回 true，否则返回 false。
 */
bool H_Modbus_TakeFrame(H_Modbus_Context *context,
                        uint8_t *buffer,
                        uint16_t capacity,
                        uint16_t *length)
{
    if ((context == NULL) || (buffer == NULL) || (length == NULL) ||
        !context->frame_ready || (context->receive_length > capacity))
    {
        return false;
    }

    (void) memcpy(buffer, context->receive_buffer, context->receive_length);
    *length = context->receive_length;
    context->receive_length = 0U;
    context->expected_length = 0U;
    context->frame_ready = false;
    return true;
}

/*
 * 函数名：H_Modbus_Send。
 * 说明：把外部 Modbus 响应保存到模拟 SCI0 发送缓存。
 * 输入：context 为硬件上下文；data 为响应数据；length 为响应长度。
 * 输出：参数和长度有效时返回 true，否则返回 false。
 */
bool H_Modbus_Send(H_Modbus_Context *context, const uint8_t *data, uint16_t length)
{
    if ((context == NULL) || (data == NULL) || (length == 0U) ||
        (length > H_MODBUS_FRAME_MAX_LENGTH))
    {
        return false;
    }

    (void) memcpy(context->transmit_buffer, data, length);
    context->transmit_busy = false;
    return true;
}

/*
 * 函数名：H_Modbus_IsTransmitBusy。
 * 说明：查询模拟外部 Modbus 是否正在发送。
 * 输入：context 为只读硬件上下文指针。
 * 输出：模拟环境始终返回 false。
 */
bool H_Modbus_IsTransmitBusy(const H_Modbus_Context *context)
{
    (void) context;
    return false;
}

/*
 * 函数名：H_Modbus_HasFault。
 * 说明：查询模拟SCI0外部Modbus是否存在故障。
 * 输入：context为只读硬件层上下文。
 * 输出：接口未打开或错误标志置位时返回true，否则返回false。
 */
bool H_Modbus_HasFault(const H_Modbus_Context *context)
{
    return ((context == NULL) || !context->uart_open || context->uart_error);
}

/*
 * 函数名：H_Can_Init。
 * 说明：初始化主机测试使用的模拟CAN0接口。
 * 输入：context为CAN硬件层上下文。
 * 输出：参数有效时返回true，否则返回false。
 */
bool H_Can_Init(H_Can_Context *context)
{
    if (context == NULL) return false;
    (void) memset(context, 0, sizeof(*context));
    context->can_open = true;
    return true;
}

/*
 * 函数名：H_Can_Deinit。
 * 说明：关闭主机测试使用的模拟CAN0接口。
 * 输入：context为CAN硬件层上下文。
 * 输出：参数有效时返回true，否则返回false。
 */
bool H_Can_Deinit(H_Can_Context *context)
{
    if (context == NULL) return false;
    context->can_open = false;
    return true;
}

/*
 * 函数名：H_Can_TakeFrame。
 * 说明：从模拟CAN接收环形队列取出一帧。
 * 输入：context为CAN硬件层上下文；frame为帧输出指针。
 * 输出：队列存在数据时返回true，否则返回false。
 */
bool H_Can_TakeFrame(H_Can_Context *context, H_Can_Frame *frame)
{
    if ((context == NULL) || (frame == NULL) ||
        (context->receive_tail == context->receive_head)) return false;
    *frame = context->receive_queue[context->receive_tail];
    context->receive_tail = (uint8_t) ((context->receive_tail + 1U) % H_CAN_RX_QUEUE_CAPACITY);
    return true;
}

/*
 * 函数名：H_Can_Send。
 * 说明：保存模拟CAN最近一帧发送内容。
 * 输入：context为CAN硬件层上下文；frame为待发送帧。
 * 输出：接口打开且参数有效时返回true，否则返回false。
 */
bool H_Can_Send(H_Can_Context *context, const H_Can_Frame *frame)
{
    if ((context == NULL) || (frame == NULL) || !context->can_open) return false;
    g_test_state.can_tx = *frame;
    g_test_state.can_tx_count++;
    return true;
}

/*
 * 函数名：H_Can_IsTransmitBusy。
 * 说明：查询模拟CAN发送状态。
 * 输入：context为只读CAN硬件层上下文。
 * 输出：模拟环境始终返回false。
 */
bool H_Can_IsTransmitBusy(const H_Can_Context *context)
{
    (void) context;
    return false;
}

/*
 * 函数名：H_Can_HasFault。
 * 说明：查询模拟CAN0接口是否存在故障。
 * 输入：context为只读CAN硬件层上下文。
 * 输出：接口未打开或总线关闭时返回true，否则返回false。
 */
bool H_Can_HasFault(const H_Can_Context *context)
{
    return ((context == NULL) || !context->can_open || context->bus_off);
}

/*
 * 函数名：H_Hmi_Init。
 * 说明：初始化协议单测使用的模拟 HMI 硬件。
 * 输入：context 为硬件上下文。
 * 输出：参数有效时返回 true。
 */
bool H_Hmi_Init(H_Hmi_Context *context)
{
    if (context == NULL) return false;
    (void) memset(context, 0, sizeof(*context));
    context->ready = true;
    return true;
}

/*
 * 函数名：H_Hmi_ReadByte。
 * 说明：读取模拟 SCI9 字节。
 * 输入：context 为上下文；value 为输出。
 * 输出：存在数据时返回 true。
 */
bool H_Hmi_ReadByte(H_Hmi_Context *context, uint8_t *value)
{
    if ((context == NULL) || (value == NULL) || (context->rx_tail == context->rx_head)) return false;
    *value = context->rx_buffer[context->rx_tail];
    context->rx_tail = (uint16_t) ((context->rx_tail + 1U) % H_HMI_RX_BUFFER_SIZE);
    return true;
}

/*
 * 函数名：H_Hmi_Write。
 * 说明：保存最近一帧协议发送数据。
 * 输入：context 为上下文；data 为数据；length 为长度。
 * 输出：参数有效时返回 true。
 */
bool H_Hmi_Write(H_Hmi_Context *context, const uint8_t *data, size_t length)
{
    if ((context == NULL) || (data == NULL) || (length > sizeof(g_test_state.hmi_tx))) return false;
    (void) memcpy(g_test_state.hmi_tx, data, length);
    g_test_state.hmi_tx_length = length;
    g_test_state.hmi_tx_count++;
    return true;
}

/*
 * 函数名：H_Hmi_IsTxBusy。
 * 说明：模拟 SCI9 发送始终空闲。
 * 输入：context 为硬件上下文。
 * 输出：固定返回 false。
 */
bool H_Hmi_IsTxBusy(const H_Hmi_Context *context)
{
    (void) context;
    return false;
}

/*
 * 函数名：Test_SeedPressure。
 * 说明：注入当前时刻的有效压力。
 * 输入：context 为应用上下文；index 为索引；pressure 为压力。
 * 输出：无。
 */
static void Test_SeedPressure(A_Gas_Control_Context *context, uint8_t index, float pressure)
{
    context->system.cylinder[index].pressure_mpa = pressure;
    context->system.cylinder[index].pressure_quality = GAS_PRESSURE_VALID;
    context->system.cylinder[index].pressure_timestamp_ms = context->platform.millis;
}

/*
 * 函数名：Test_Prepare。
 * 说明：建立1号低压、2～6号合格且2号工作的测试系统。
 * 输入：context 为应用上下文。
 * 输出：无。
 */
static void Test_Prepare(A_Gas_Control_Context *context)
{
    uint8_t index; // 当前作用域变量，用于保存遍历索引。
    (void) memset(&g_test_state, 0, sizeof(g_test_state));
    A_GasControl_Init(context);
    context->config.pressure_fresh_ms = 65535U;
    context->config.low_confirm_time_ms = 100U;
    context->config.low_confirm_samples = 2U;
    context->config.valve_close_wait_ms = 0U;
    context->config.valve_open_wait_ms = 0U;
    Test_SeedPressure(context, 0U, 1.0F);
    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        context->system.cylinder[index].qualification_passed = true;
    }
    for (index = 1U; index < GAS_CYLINDER_COUNT; ++index)
    {
        Test_SeedPressure(context, index, 5.0F);
    }
    A_GasControl_Task(context);
}

/*
 * 函数名：Test_Advance。
 * 说明：推进模拟时间并执行一次周期任务。
 * 输入：context 为应用上下文；milliseconds 为时间增量。
 * 输出：无。
 */
static void Test_Advance(A_Gas_Control_Context *context, uint32_t milliseconds)
{
    context->platform.millis += milliseconds;
    A_GasControl_Task(context);
}

/*
 * 函数名：Test_SetMaintenanceState。
 * 说明：把六瓶、十八路分瓶阀和一路总测试阀置于V1.08允许切换通讯及提交Modbus整组参数的维护状态。
 * 输入：context为气源控制应用上下文输入输出指针。
 * 输出：无；六瓶全部停用，阀门命令、工作瓶索引和切换过程全部清零。
 */
static void Test_SetMaintenanceState(A_Gas_Control_Context *context)
{
    uint8_t index; // 当前作用域变量，用于保存遍历索引。

    F_ValveControl_AllOff(&context->platform, &context->system);
    context->total_test_pending_open_mask = 0U;
    (void) memset(context->test_open_not_before_ms, 0, sizeof(context->test_open_not_before_ms));
    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        context->system.cylinder[index].state = GAS_CYL_DISABLED;
        context->system.cylinder[index].qualification_passed = false;
    }
    context->system.active_index = GAS_NO_ACTIVE_CYLINDER;
    context->switch_old_index = GAS_NO_ACTIVE_CYLINDER;
    context->switch_new_index = GAS_NO_ACTIVE_CYLINDER;
    context->system.switch_state = GAS_SWITCH_IDLE;
}

/*
 * 函数名：Test_ModbusWriteSingle。
 * 说明：构造一帧功能码06请求，通过模拟 SCI0 向外部 Modbus 保持寄存器写入单个数值。
 * 输入：context 为应用上下文；address 为 PDU 寄存器地址；value 为待写入数值。
 * 输出：无；请求帧写入模拟接收缓存并执行一个气源控制周期。
 */
static void Test_ModbusWriteSingle(A_Gas_Control_Context *context,
                                   uint16_t address,
                                   uint16_t value)
{
    uint8_t frame[8]; // 当前作用域变量，用于保存通信帧缓冲区或长度。
    uint16_t crc; // 当前作用域变量，用于保存CRC校验值。
    H_Modbus_Context *hardware = &context->external_modbus.hardware; // 当前作用域变量，用于保存当前处理数据指针。

    frame[0] = A_MODBUS_SLAVE_ADDRESS;
    frame[1] = 0x06U;
    frame[2] = (uint8_t) (address >> 8U);
    frame[3] = (uint8_t) address;
    frame[4] = (uint8_t) (value >> 8U);
    frame[5] = (uint8_t) value;
    crc = F_Modbus_Crc16(frame, 6U);
    frame[6] = (uint8_t) crc;
    frame[7] = (uint8_t) (crc >> 8U);

    (void) memcpy(hardware->receive_buffer, frame, sizeof(frame));
    hardware->receive_length = sizeof(frame);
    hardware->expected_length = sizeof(frame);
    hardware->frame_ready = true;
    A_GasControl_Task(context);
}

/*
 * 函数名：Test_RecordCrc16。
 * 说明：计算旧参数和通讯模式测试记录使用的Modbus多项式CRC16。
 * 输入：data为只读数据；length为参与计算的字节数。
 * 输出：返回16位CRC值。
 */
static uint16_t Test_RecordCrc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU; // 当前作用域变量，用于保存CRC校验值。
    size_t index; // 当前作用域变量，用于保存遍历索引。
    uint8_t bit; // 当前作用域变量，用于保存位掩码。

    for (index = 0U; index < length; ++index)
    {
        crc = (uint16_t) (crc ^ data[index]);
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = ((crc & 1U) != 0U) ?
                  (uint16_t) ((crc >> 1U) ^ 0xA001U) : (uint16_t) (crc >> 1U);
        }
    }
    return crc;
}

/*
 * 函数名：Test_ConfigV2Migration。
 * 说明：验证旧10项V2参数和0x0020通讯模式在首次启动时安全迁移为V4与0x0030记录。
 * 输入：无。
 * 输出：无；断言旧参数、RS485模式和三项新增默认值均被保留。
 */
static void Test_ConfigV2Migration(void)
{
    const uint16_t values[10] = {1100U, 1300U, 100U, 1500U, 25000U,
                                 1000U, 3U, 500U, 500U, 2500U};
    A_Gas_Control_Context context; // 当前作用域变量，用于保存模块上下文。
    uint8_t *record = &g_test_state.eeprom[0x0000U]; // 当前作用域变量，用于保存日志或配置记录缓冲区指针。
    uint8_t *comm = &g_test_state.eeprom[0x0020U]; // 当前作用域变量，用于保存当前处理数据指针。
    uint16_t crc; // 当前作用域变量，用于保存CRC校验值。
    uint8_t index; // 当前作用域变量，用于保存遍历索引。

    (void) memset(&g_test_state, 0, sizeof(g_test_state));
    record[0] = 'G'; record[1] = 'C'; record[2] = 'F'; record[3] = 'G';
    record[4] = 0U; record[5] = 2U; record[6] = 0U; record[7] = 20U;
    for (index = 0U; index < 10U; ++index)
    {
        record[8U + index * 2U] = (uint8_t) (values[index] >> 8U);
        record[9U + index * 2U] = (uint8_t) values[index];
    }
    crc = Test_RecordCrc16(record, 28U);
    record[28] = (uint8_t) (crc >> 8U);
    record[29] = (uint8_t) crc;

    comm[0] = 'G'; comm[1] = 'C'; comm[2] = 'O'; comm[3] = 'M';
    comm[4] = 1U; comm[5] = (uint8_t) GAS_EXTERNAL_COMM_RS485;
    crc = Test_RecordCrc16(comm, 6U);
    comm[6] = (uint8_t) crc;
    comm[7] = (uint8_t) (crc >> 8U);

    A_GasControl_Init(&context);
    assert(context.external_comm_mode == GAS_EXTERNAL_COMM_RS485);
    assert((context.config.switch_pressure_mpa > 1.099F) &&
           (context.config.switch_pressure_mpa < 1.101F));
    assert((context.config.low_warning_pressure_mpa > 1.999F) &&
           (context.config.low_warning_pressure_mpa < 2.001F));
    assert((context.config.manual_exhaust_time_ms == 5000U) &&
           (context.config.test_valve_max_time_ms == 600000U));
    assert((g_test_state.eeprom[4] == 0U) && (g_test_state.eeprom[5] == 4U));
    assert(memcmp(&g_test_state.eeprom[0x0030U], "GCOM", 4U) == 0);
}

/*
 * 函数名：Test_ConfigV3Migration。
 * 说明：验证V3测试阀秒数按相同界面数值迁移为V4分钟数并原址重写。
 * 输入：无。
 * 输出：无；断言旧45秒设置升级为45分钟，其他13项参数保持有效。
 */
static void Test_ConfigV3Migration(void)
{
    const uint16_t values[13] = {1200U, 1300U, 200U, 1500U, 25000U,
                                 1000U, 3U, 500U, 500U, 2500U,
                                 2000U, 5000U, 45000U};
    A_Gas_Control_Context context; // 当前作用域变量，用于保存模块上下文。
    Gas_Config stored; // 当前作用域变量，用于保存当前处理数据。
    uint8_t *record = &g_test_state.eeprom[0x0000U]; // 当前作用域变量，用于保存日志或配置记录缓冲区指针。
    uint16_t crc; // 当前作用域变量，用于保存CRC校验值。
    uint8_t index; // 当前作用域变量，用于保存遍历索引。

    (void) memset(&g_test_state, 0, sizeof(g_test_state));
    record[0] = 'G'; record[1] = 'C'; record[2] = 'F'; record[3] = 'G';
    record[4] = 0U; record[5] = 3U; record[6] = 0U; record[7] = 26U;
    for (index = 0U; index < 13U; ++index)
    {
        record[8U + index * 2U] = (uint8_t) (values[index] >> 8U);
        record[9U + index * 2U] = (uint8_t) values[index];
    }
    crc = Test_RecordCrc16(record, 34U);
    record[34] = (uint8_t) (crc >> 8U);
    record[35] = (uint8_t) crc;

    A_GasControl_Init(&context);
    assert(context.config.test_valve_max_time_ms == (45U * GAS_MILLISECONDS_PER_MINUTE));
    assert((g_test_state.eeprom[4] == 0U) && (g_test_state.eeprom[5] == 4U));
    assert((g_test_state.eeprom[32] == 0U) && (g_test_state.eeprom[33] == 45U));
    assert(A_GasConfig_Load(&context.storage_service, &stored));
    assert(stored.test_valve_max_time_ms == (45U * GAS_MILLISECONDS_PER_MINUTE));
}

/*
 * 函数名：Test_TestValveTimeRange。
 * 说明：验证V1.12继续支持测试阀5～60整分钟范围及10分钟默认值。
 * 输入：无。
 * 输出：无；通过参数校验结果断言分钟边界。
 */
static void Test_TestValveTimeRange(void)
{
    Gas_Config config; // 当前作用域变量，用于保存运行参数。

    A_GasConfig_LoadDefaults(&config);
    assert(config.test_valve_max_time_ms == (10U * GAS_MILLISECONDS_PER_MINUTE));
    config.test_valve_max_time_ms = 4U * GAS_MILLISECONDS_PER_MINUTE;
    assert(A_GasConfig_Validate(&config) == A_GAS_CONFIG_INVALID_RANGE);
    config.test_valve_max_time_ms = 5U * GAS_MILLISECONDS_PER_MINUTE;
    assert(A_GasConfig_Validate(&config) == A_GAS_CONFIG_VALID);
    config.test_valve_max_time_ms = 60U * GAS_MILLISECONDS_PER_MINUTE;
    assert(A_GasConfig_Validate(&config) == A_GAS_CONFIG_VALID);
    config.test_valve_max_time_ms = 61U * GAS_MILLISECONDS_PER_MINUTE;
    assert(A_GasConfig_Validate(&config) == A_GAS_CONFIG_INVALID_RANGE);
    config.test_valve_max_time_ms = (5U * GAS_MILLISECONDS_PER_MINUTE) + 1U;
    assert(A_GasConfig_Validate(&config) == A_GAS_CONFIG_INVALID_RANGE);
}

/*
 * 函数名：Test_ExternalModbusConfig。
 * 说明：验证外部SCI0 Modbus废弃启停命令、全瓶停用维护门槛、参数提交和EEPROM持久化。
 * 输入：无。
 * 输出：无；断言非维护状态提交被拒绝，全瓶停用后参数成功应用并可从EEPROM读回。
 */
static void Test_ExternalModbusConfig(void)
{
    A_Gas_Control_Context context; // 当前作用域变量，用于保存模块上下文。
    A_Gas_Control_Context rebooted; // 当前作用域变量，用于保存当前处理数据。
    Gas_Config stored; // 当前作用域变量，用于保存当前处理数据。
    uint16_t result; // 当前作用域变量，用于保存操作结果。

    (void) memset(&g_test_state, 0, sizeof(g_test_state));
    A_GasControl_Init(&context);
    assert(A_Can_IsReady(&context.external_can));
    Test_SetMaintenanceState(&context);
    assert(A_GasControl_SetExternalCommMode(&context, GAS_EXTERNAL_COMM_RS485));
    assert(A_Modbus_IsReady(&context.external_modbus));
    assert(A_MODBUS_SOFTWARE_VERSION == 0x0202U);
    assert(F_MODBUS_HOLDING_REGISTER_COUNT == 38U);
    assert(A_GAS_CONFIG_REGISTER_COUNT == 10U);
    assert(context.external_modbus.function.holding_register[A_MODBUS_HOLDING_CONFIG_VERSION] ==
           A_MODBUS_CONFIG_VERSION_VALUE);
    assert(context.external_modbus.function.holding_register[A_MODBUS_HOLDING_CONFIG_BASE] == 1200U);

    Test_ModbusWriteSingle(&context,
                           F_MODBUS_HOLDING_BASE_ADDRESS + A_MODBUS_HOLDING_COMMAND,
                           A_MODBUS_COMMAND_STOP);
    assert(F_Modbus_GetHoldingRegister(&context.external_modbus.function,
                                       A_MODBUS_HOLDING_RESULT,
                                       &result));
    assert(result == A_MODBUS_RESULT_INVALID_COMMAND);

    context.system.cylinder[0].state = GAS_CYL_INIT;

    Test_ModbusWriteSingle(&context,
                           F_MODBUS_HOLDING_BASE_ADDRESS + A_MODBUS_HOLDING_CONFIG_BASE,
                           1100U);
    Test_ModbusWriteSingle(&context,
                           F_MODBUS_HOLDING_BASE_ADDRESS + A_MODBUS_HOLDING_CONFIG_COMMIT,
                           A_MODBUS_CONFIG_COMMIT_KEY);
    assert(F_Modbus_GetHoldingRegister(&context.external_modbus.function,
                                       A_MODBUS_HOLDING_CONFIG_RESULT,
                                       &result));
    assert(result == A_MODBUS_CONFIG_RESULT_SYSTEM_BUSY);
    assert(context.external_modbus.function.holding_register[A_MODBUS_HOLDING_CONFIG_BASE] == 1200U);

    Test_SetMaintenanceState(&context);
    Test_ModbusWriteSingle(&context,
                           F_MODBUS_HOLDING_BASE_ADDRESS + A_MODBUS_HOLDING_CONFIG_BASE,
                           1100U);
    Test_ModbusWriteSingle(&context,
                           F_MODBUS_HOLDING_BASE_ADDRESS + A_MODBUS_HOLDING_CONFIG_COMMIT,
                           A_MODBUS_CONFIG_COMMIT_KEY);
    assert(F_Modbus_GetHoldingRegister(&context.external_modbus.function,
                                       A_MODBUS_HOLDING_CONFIG_RESULT,
                                       &result));
    assert(result == A_MODBUS_CONFIG_RESULT_SUCCESS);
    assert((context.config.switch_pressure_mpa > 1.099F) &&
           (context.config.switch_pressure_mpa < 1.101F));
    assert(A_GasConfig_Load(&context.storage_service, &stored));
    assert((stored.switch_pressure_mpa > 1.099F) && (stored.switch_pressure_mpa < 1.101F));

    context.system.total_pressure.pressure_mpa = 3.25F;
    context.system.total_pressure.pressure_quality = GAS_PRESSURE_VALID;
    context.system.cylinder[0].qualification_passed = true;
    A_Modbus_Refresh(&context.external_modbus, &context.system);
    assert(context.external_modbus.function.input_register[A_MODBUS_INPUT_TOTAL_PRESSURE_BASE] == 0x4050U);
    assert(context.external_modbus.function.input_register[A_MODBUS_INPUT_TOTAL_PRESSURE_BASE + 1U] == 0x0000U);
    assert(context.external_modbus.function.input_register[A_MODBUS_INPUT_TOTAL_QUALITY] == GAS_PRESSURE_VALID);
    assert((context.external_modbus.function.input_register[A_MODBUS_INPUT_QUALIFIED_MASK] & 0x0001U) != 0U);

    A_GasControl_Init(&rebooted);
    assert(rebooted.external_comm_mode == GAS_EXTERNAL_COMM_RS485);
    assert(A_Modbus_IsReady(&rebooted.external_modbus));
    // 不清空模拟EEPROM重新上电，验证通讯模式记录可以使系统直接恢复到RS485。
}

/*
 * 函数名：Test_FloatToRaw。
 * 说明：把主机测试浮点值无别名地转换为CAN协议使用的32位原始位模式。
 * 输入：value为待转换float32数值。
 * 输出：返回具有相同位模式的uint32数值。
 */
static uint32_t Test_FloatToRaw(float value)
{
    uint32_t raw = 0U; // 当前作用域变量，用于保存当前处理数据。
    (void) memcpy(&raw, &value, sizeof(raw));
    return raw;
}

/*
 * 函数名：Test_QueueCanWrite。
 * 说明：向模拟CAN接收环形队列加入一帧来自表头1号节点的合法定向写请求。
 * 输入：context为气源应用上下文；address为写地址；value为32位原始写值。
 * 输出：无；请求写入模拟CAN接收队列并推进队头。
 */
static void Test_QueueCanWrite(A_Gas_Control_Context *context,
                               uint16_t address,
                               uint32_t value)
{
    H_Can_Frame *request =
        &context->external_can.hardware.receive_queue[context->external_can.hardware.receive_head];

    (void) memset(request, 0, sizeof(*request));
    request->id = ((uint32_t) F_CAN_FUNCTION_WRITE << 24U) |
                  ((uint32_t) F_CAN_LOCAL_TYPE << 19U) |
                  ((uint32_t) F_CAN_LOCAL_ADDRESS << 12U) |
                  1U;
    request->data[0] = (uint8_t) address;
    request->data[1] = (uint8_t) (address >> 8U);
    request->data[3] = 1U;
    request->data[4] = (uint8_t) value;
    request->data[5] = (uint8_t) (value >> 8U);
    request->data[6] = (uint8_t) (value >> 16U);
    request->data[7] = (uint8_t) (value >> 24U);
    request->data[2] = F_CanProtocol_CalculateCrc(request->id, request->data);
    context->external_can.hardware.receive_head =
        (uint8_t) ((context->external_can.hardware.receive_head + 1U) % H_CAN_RX_QUEUE_CAPACITY);
}

/*
 * 函数名：Test_RunCanWrite。
 * 说明：执行一条模拟CAN写请求并轮询气源任务，直到最终功能码6响应实际发送。
 * 输入：context为气源应用上下文；address为写地址；value为32位原始写值。
 * 输出：无；断言最多八个任务周期内产生一帧写响应。
 */
static void Test_RunCanWrite(A_Gas_Control_Context *context,
                             uint16_t address,
                             uint32_t value)
{
    uint32_t previous_tx_count = g_test_state.can_tx_count; // 当前作用域变量，用于保存数量计数。
    uint8_t step; // 当前作用域变量，用于保存当前处理数据。

    Test_QueueCanWrite(context, address, value);
    for (step = 0U; (step < 8U) && (g_test_state.can_tx_count == previous_tx_count); ++step)
    {
        A_GasControl_Task(context);
    }
    assert(g_test_state.can_tx_count == (previous_tx_count + 1U));
    assert(((g_test_state.can_tx.id >> 24U) & 0x1FU) == F_CAN_FUNCTION_WRITE_RESPONSE);
    assert(g_test_state.can_tx.data[0] == (uint8_t) address);
    assert(g_test_state.can_tx.data[1] == (uint8_t) (address >> 8U));
    assert(g_test_state.can_tx.data[2] ==
           F_CanProtocol_CalculateCrc(g_test_state.can_tx.id, g_test_state.can_tx.data));
}

/*
 * 函数名：Test_DefaultCanProtocol。
 * 说明：验证无有效模式记录时默认启用CAN，并按参考29位ID和CRC响应总压力读取请求。
 * 输入：无。
 * 输出：无；断言默认模式、响应功能码、数据地址及float32小端数据均正确。
 */
static void Test_DefaultCanProtocol(void)
{
    A_Gas_Control_Context context; // 当前作用域变量，用于保存模块上下文。
    H_Can_Frame *request; // 当前作用域变量，用于保存待处理请求指针。
    uint32_t request_id; // 当前作用域变量，用于保存待处理请求。
    uint16_t address; // 当前作用域变量，用于保存存储或寄存器地址。
    uint32_t previous_tx_count; // 当前作用域变量，用于保存数量计数。

    (void) memset(&g_test_state, 0, sizeof(g_test_state));

    // 四类地址必须严格落在各自的高字节分区，防止后续新增数据破坏上位机按地址判型的规则。
    assert(A_CAN_ADDRESS_PRESSURE_BASE <= 0x00FFU);
    assert(A_CAN_ADDRESS_TOTAL_PRESSURE <= 0x00FFU);
    assert((A_CAN_ADDRESS_STATE_BASE >= 0x0100U) && (A_CAN_ADDRESS_STATE_BASE <= 0x01FFU));
    assert((A_CAN_ADDRESS_COMMAND_RESULT >= 0x0100U) && (A_CAN_ADDRESS_WRITE_SEQUENCE <= 0x01FFU));
    assert((A_CAN_ADDRESS_SWITCH_PRESSURE >= 0x0200U) && (A_CAN_ADDRESS_PRESSURE_MAX <= 0x02FFU));
    assert((A_CAN_ADDRESS_VALVE_PULL_IN_TIME >= 0x0300U) && (A_CAN_ADDRESS_LOG_INDEX <= 0x03FFU));
    assert(A_CAN_SOFTWARE_VERSION == 0x0206UL);
    assert(A_CAN_ADDRESS_COMMAND_RESULT == (A_CAN_ADDRESS_COMM_MODE + 1U));
    assert(A_CAN_ADDRESS_CONFIG_RESULT == (A_CAN_ADDRESS_COMMAND_RESULT + 1U));
    assert(A_CAN_ADDRESS_CONFIG_VERSION == (A_CAN_ADDRESS_CONFIG_RESULT + 1U));
    assert(A_CAN_ADDRESS_LOG_RESULT == (A_CAN_ADDRESS_CONFIG_VERSION + 1U));
    assert(A_CAN_ADDRESS_LOG_COUNT == (A_CAN_ADDRESS_LOG_RESULT + 1U));
    assert(A_CAN_ADDRESS_LOG_CAPACITY == (A_CAN_ADDRESS_LOG_COUNT + 1U));
    assert(A_CAN_ADDRESS_LOG_RECORD_SIZE == (A_CAN_ADDRESS_LOG_CAPACITY + 1U));
    assert(A_CAN_ADDRESS_LOG_DATA_BASE == (A_CAN_ADDRESS_LOG_RECORD_SIZE + 1U));
    assert((A_CAN_ADDRESS_LOG_DATA_BASE + (A_CAN_LOG_RECORD_SIZE / 4U) - 1U) == 0x0126U);
    assert(A_CAN_ADDRESS_LAST_WRITE_ADDRESS == 0x0127U);
    assert(A_CAN_ADDRESS_LAST_WRITE_RESULT == 0x0128U);
    assert(A_CAN_ADDRESS_LAST_WRITE_VALUE == 0x0129U);
    assert(A_CAN_ADDRESS_WRITE_SEQUENCE == 0x012AU);
    assert(A_CAN_ADDRESS_LOW_CONFIRM_TIME == (A_CAN_ADDRESS_VALVE_PULL_IN_TIME + 1U));
    assert(A_CAN_ADDRESS_LOW_CONFIRM_SAMPLES == (A_CAN_ADDRESS_LOW_CONFIRM_TIME + 1U));
    assert(A_CAN_ADDRESS_VALVE_CLOSE_WAIT == (A_CAN_ADDRESS_LOW_CONFIRM_SAMPLES + 1U));
    assert(A_CAN_ADDRESS_VALVE_OPEN_WAIT == (A_CAN_ADDRESS_VALVE_CLOSE_WAIT + 1U));
    assert(A_CAN_ADDRESS_PRESSURE_FRESH == (A_CAN_ADDRESS_VALVE_OPEN_WAIT + 1U));
    assert(A_CAN_ADDRESS_COMMAND == (A_CAN_ADDRESS_PRESSURE_FRESH + 1U));
    assert(A_CAN_ADDRESS_CONFIG_COMMIT == (A_CAN_ADDRESS_COMMAND + 1U));
    assert(A_CAN_ADDRESS_CONFIG_DEFAULT == (A_CAN_ADDRESS_CONFIG_COMMIT + 1U));
    assert(A_CAN_ADDRESS_LOG_COMMAND == (A_CAN_ADDRESS_CONFIG_DEFAULT + 1U));
    assert(A_CAN_ADDRESS_LOG_INDEX == (A_CAN_ADDRESS_LOG_COMMAND + 1U));
    assert(A_CAN_ADDRESS_LOG_INDEX == 0x030AU);
    assert(A_CAN_ADDRESS_EXHAUST_CONTROL_BASE == 0x030BU);
    assert(A_CAN_ADDRESS_TEST_CONTROL_BASE == 0x0311U);
    assert(A_CAN_ADDRESS_DISABLE_CONTROL_BASE == 0x0317U);
    assert(A_CAN_ADDRESS_QUALIFY_CONTROL_BASE == 0x031DU);
    assert((A_CAN_ADDRESS_QUALIFY_CONTROL_BASE + GAS_CYLINDER_COUNT - 1U) == 0x0322U);
    assert(F_CAN_LOCAL_TYPE == 15U);
    assert(F_CAN_LOCAL_ADDRESS == 1U);
    assert(F_CAN_CYCLE_TARGET_TYPE == 0U);
    assert(F_CAN_CYCLE_TARGET_ADDRESS == 1U);
    assert(GAS_DEFAULT_VALVE_PULL_IN_TIME_MS == 200U);

    A_GasControl_Init(&context);
    assert(context.config.valve_pull_in_time_ms == 200U);
    assert(context.external_comm_mode == GAS_EXTERNAL_COMM_CAN);
    assert(A_Can_IsReady(&context.external_can));
    context.system.total_pressure.pressure_mpa = 3.25F;
    context.system.total_pressure.pressure_quality = GAS_PRESSURE_VALID;

    request_id = ((uint32_t) F_CAN_FUNCTION_READ << 24U) |
                 ((uint32_t) F_CAN_LOCAL_TYPE << 19U) |
                 ((uint32_t) F_CAN_LOCAL_ADDRESS << 12U) |
                 ((uint32_t) 15U << 7U) | 2U;
    request = &context.external_can.hardware.receive_queue[0];
    (void) memset(request, 0, sizeof(*request));
    request->id = request_id;
    request->data[0] = (uint8_t) A_CAN_ADDRESS_TOTAL_PRESSURE;
    request->data[1] = (uint8_t) (A_CAN_ADDRESS_TOTAL_PRESSURE >> 8U);
    request->data[3] = 1U;
    request->data[2] = F_CanProtocol_CalculateCrc(request->id, request->data);
    context.external_can.hardware.receive_head = 1U;

    A_Can_Task(&context.external_can, &context.system, &context.config, context.external_comm_mode);
    A_Can_Task(&context.external_can, &context.system, &context.config, context.external_comm_mode);
    assert(g_test_state.can_tx_count == 1U);
    assert(((g_test_state.can_tx.id >> 24U) & 0x1FU) == F_CAN_FUNCTION_READ_RESPONSE);
    assert(((g_test_state.can_tx.id >> 7U) & 0x1FU) == F_CAN_LOCAL_TYPE);
    assert((g_test_state.can_tx.id & 0x7FU) == F_CAN_LOCAL_ADDRESS);
    assert(g_test_state.can_tx.data[0] == (uint8_t) A_CAN_ADDRESS_TOTAL_PRESSURE);
    assert(g_test_state.can_tx.data[3] == 1U);
    assert((g_test_state.can_tx.data[4] == 0x00U) &&
           (g_test_state.can_tx.data[5] == 0x00U) &&
           (g_test_state.can_tx.data[6] == 0x50U) &&
           (g_test_state.can_tx.data[7] == 0x40U));
    assert(g_test_state.can_tx.data[2] ==
           F_CanProtocol_CalculateCrc(g_test_state.can_tx.id, g_test_state.can_tx.data));

    for (address = A_CAN_ADDRESS_STATE_BASE;
         address <= A_CAN_ADDRESS_WRITE_SEQUENCE;
         ++address)
    {
        uint8_t receive_head = context.external_can.hardware.receive_head; // 当前作用域变量，用于保存队列头位置。

        request = &context.external_can.hardware.receive_queue[receive_head];
        (void) memset(request, 0, sizeof(*request));
        request->id = request_id;
        request->data[0] = (uint8_t) address;
        request->data[1] = (uint8_t) (address >> 8U);
        request->data[3] = 1U;
        request->data[2] = F_CanProtocol_CalculateCrc(request->id, request->data);
        context.external_can.hardware.receive_head =
            (uint8_t) ((receive_head + 1U) % H_CAN_RX_QUEUE_CAPACITY);

        previous_tx_count = g_test_state.can_tx_count; // 当前作用域变量，用于保存数量计数。
        A_Can_Task(&context.external_can, &context.system, &context.config, context.external_comm_mode);
        A_Can_Task(&context.external_can, &context.system, &context.config, context.external_comm_mode);
        assert(g_test_state.can_tx_count == (previous_tx_count + 1U));
        assert(g_test_state.can_tx.data[0] == (uint8_t) address);
        assert(g_test_state.can_tx.data[1] == (uint8_t) (address >> 8U));
        assert(g_test_state.can_tx.data[3] == 1U);
        assert(g_test_state.can_tx.data[2] ==
               F_CanProtocol_CalculateCrc(g_test_state.can_tx.id, g_test_state.can_tx.data));
        // 逐个构造真实读请求，保证0x0100～0x012A每个只读整数地址都产生合法响应帧。
    }

    assert((g_test_state.can_tx.data[4] != 0xFFU) ||
           (g_test_state.can_tx.data[5] != 0xFFU) ||
           (g_test_state.can_tx.data[6] != 0xFFU) ||
           (g_test_state.can_tx.data[7] != 0xFFU));

    request = &context.external_can.hardware.receive_queue[context.external_can.hardware.receive_head];
    (void) memset(request, 0, sizeof(*request));
    request->id = request_id;
    request->data[0] = (uint8_t) A_CAN_ADDRESS_QUALITY_BASE;
    request->data[1] = (uint8_t) (A_CAN_ADDRESS_QUALITY_BASE >> 8U);
    request->data[3] = (uint8_t) ((A_CAN_ADDRESS_WRITE_SEQUENCE + 1U) -
                                 A_CAN_ADDRESS_QUALITY_BASE);
    request->data[2] = F_CanProtocol_CalculateCrc(request->id, request->data);
    context.external_can.hardware.receive_head =
        (uint8_t) ((context.external_can.hardware.receive_head + 1U) % H_CAN_RX_QUEUE_CAPACITY);
    previous_tx_count = g_test_state.can_tx_count; // 当前作用域变量，用于保存数量计数。
    A_Can_Task(&context.external_can, &context.system, &context.config, context.external_comm_mode);

    for (address = A_CAN_ADDRESS_QUALITY_BASE;
         address <= A_CAN_ADDRESS_WRITE_SEQUENCE;
         ++address)
    {
        A_Can_Task(&context.external_can, &context.system, &context.config, context.external_comm_mode);
        assert(g_test_state.can_tx_count == ++previous_tx_count);
        assert(g_test_state.can_tx.data[0] == (uint8_t) address);
        assert(g_test_state.can_tx.data[1] == (uint8_t) (address >> 8U));
        // 模拟上位机用一帧请求连续读取0x0106～0x012A，验证37帧响应能够分批完整发出。
    }
}

/*
 * 函数名：Test_CanDirectWriteAndControl。
 * 说明：验证V1.08 CAN单参数保存、参数关系拒绝、废弃整机启停地址以及单瓶控制最终结果应答。
 * 输入：无。
 * 输出：无；断言功能码6基础码、详细码、EEPROM生效值和阀门实际命令均正确。
 */
static void Test_CanDirectWriteAndControl(void)
{
    A_Gas_Control_Context context; // 当前作用域变量，用于保存模块上下文。
    float loaded_pressure_max; // 当前作用域变量，用于保存压力值。

    (void) memset(&g_test_state, 0, sizeof(g_test_state));
    A_GasControl_Init(&context);

    Test_RunCanWrite(&context,
                     A_CAN_ADDRESS_PRESSURE_MAX,
                     Test_FloatToRaw(29.0F));
    assert(g_test_state.can_tx.data[4] == A_CAN_WRITE_SUCCESS);
    assert(g_test_state.can_tx.data[5] == A_CAN_WRITE_DETAIL_NONE);
    assert(context.config.pressure_max_mpa == 29.0F);
    assert(context.external_can.last_write_result == 0U);
    assert(A_GasConfig_Load(&context.storage_service, &context.external_can.pending_config));
    loaded_pressure_max = context.external_can.pending_config.pressure_max_mpa;
    assert(loaded_pressure_max == 29.0F);
    // 成功响应到达时，参数已经写入EEPROM、读回校验并同步到正式运行结构。

    Test_RunCanWrite(&context,
                     A_CAN_ADDRESS_PRESSURE_MAX,
                     Test_FloatToRaw(1.0F));
    assert(g_test_state.can_tx.data[4] == A_CAN_WRITE_EXECUTION_ERROR);
    assert(g_test_state.can_tx.data[5] == A_CAN_WRITE_DETAIL_RELATION_CONFLICT);
    assert(context.config.pressure_max_mpa == 29.0F);

    Test_RunCanWrite(&context, A_CAN_ADDRESS_CONFIG_COMMIT, 0x0000A55AUL);
    assert(g_test_state.can_tx.data[4] == A_CAN_WRITE_ADDRESS_ERROR);
    assert(g_test_state.can_tx.data[5] == A_CAN_WRITE_DETAIL_DEPRECATED);

    Test_RunCanWrite(&context, A_CAN_ADDRESS_COMMAND, GAS_EXTERNAL_COMMAND_STOP);
    assert(g_test_state.can_tx.data[4] == A_CAN_WRITE_ADDRESS_ERROR);
    assert(g_test_state.can_tx.data[5] == A_CAN_WRITE_DETAIL_DEPRECATED);
    assert(context.system.mode == GAS_MODE_AUTO);
    // V1.08保留0x0306地址兼容，但任何人工启动或停止请求都不得改变系统运行状态。

    context.system.mode = GAS_MODE_AUTO;
    context.system.switch_state = GAS_SWITCH_IDLE;
    context.system.cylinder[0].state = GAS_CYL_READY;
    context.system.cylinder[0].qualification_passed = true;
    context.system.cylinder[0].pressure_mpa = 3.0F;
    context.system.cylinder[0].pressure_quality = GAS_PRESSURE_VALID;
    context.system.cylinder[0].pressure_timestamp_ms =
        context.platform.millis;
    context.system.active_index = 1U;
    context.system.cylinder[1].state = GAS_CYL_ACTIVE;
    context.system.cylinder[1].qualification_passed = true;
    context.system.cylinder[1].pressure_mpa = 3.0F;
    context.system.cylinder[1].pressure_quality = GAS_PRESSURE_VALID;
    context.system.cylinder[1].pressure_timestamp_ms =
        context.platform.millis;
    context.system.cylinder[1].supply_cmd = true;
    context.platform.supply_state[1] = true;
    // 固定2号瓶为当前工作瓶，避免自动选择逻辑在测试CAN人工阀请求前占用1号瓶供气阀。

    Test_RunCanWrite(&context, A_CAN_ADDRESS_EXHAUST_CONTROL_BASE, 1U);
    assert(g_test_state.can_tx.data[4] == A_CAN_WRITE_SUCCESS);
    assert(context.system.cylinder[0].exhaust_cmd);

    Test_RunCanWrite(&context, A_CAN_ADDRESS_TEST_CONTROL_BASE, 1U);
    assert(g_test_state.can_tx.data[4] == A_CAN_WRITE_SUCCESS);
    assert(!context.system.cylinder[0].test_cmd);
    assert((context.total_test_pending_open_mask & 0x01U) != 0U);
    Test_Advance(&context, GAS_VALVE_BOOST_MIN_INTERVAL_MS);
    assert(context.system.total_test_cmd);
    Test_Advance(&context, GAS_VALVE_BOOST_MIN_INTERVAL_MS);
    assert(context.system.cylinder[0].exhaust_cmd);
    assert(context.system.cylinder[0].test_cmd);
    // 总阀等待共享VALP1+可用并先吸合，随后分路测试阀才与排气阀并行开启。

    Test_RunCanWrite(&context, A_CAN_ADDRESS_EXHAUST_CONTROL_BASE, 0U);
    assert(!context.system.cylinder[0].exhaust_cmd);
    assert(context.system.cylinder[0].test_cmd);
    Test_RunCanWrite(&context, A_CAN_ADDRESS_TEST_CONTROL_BASE, 0U);
    assert(!context.system.cylinder[0].test_cmd);
    assert(!context.system.total_test_cmd);

    assert(context.external_can.last_write_address == A_CAN_ADDRESS_TEST_CONTROL_BASE);
    assert(context.external_can.last_write_value == 0U);
    assert(context.external_can.write_sequence == 8U);
}

/*
 * 函数名：Test_GasLog。
 * 说明：验证半小时常规记录、状态变化事件、32字节记录读取和双管理头掉电恢复。
 * 输入：无。
 * 输出：无；断言日志数量、关键字段及重新初始化后的循环索引均正确。
 */
static void Test_GasLog(void)
{
    A_Gas_Control_Context context; // 当前作用域变量，用于保存模块上下文。
    A_Gas_Log_Context recovered; // 当前作用域变量，用于保存当前处理数据。
    uint8_t record[A_GAS_LOG_RECORD_SIZE]; // 当前作用域变量，用于保存日志或配置记录缓冲区。
    uint8_t protected_data[A_GAS_LOG_HEADER_A_ADDRESS]; // 当前作用域变量，用于保存业务数据数组。
    uint8_t index; // 当前作用域变量，用于保存遍历索引。
    uint16_t log_index; // 当前作用域变量，用于保存遍历索引。
    uint16_t result; // 当前作用域变量，用于保存操作结果。
    uint16_t clear_step; // 当前作用域变量，用于保存当前处理数据。
    uint16_t erased_header_address; // 当前作用域变量，用于保存存储或寄存器地址。
    size_t byte_index; // 当前作用域变量，用于保存遍历索引。

    (void) memset(&g_test_state, 0, sizeof(g_test_state));
    A_GasControl_Init(&context);
    Test_SetMaintenanceState(&context);
    assert(A_GasControl_SetExternalCommMode(&context, GAS_EXTERNAL_COMM_RS485));
    assert(A_GasLog_IsReady(&context.log_service));

    context.system.date_time.year = 2026U;
    context.system.date_time.month = 8U;
    context.system.date_time.day = 18U;
    context.system.date_time.hour = 15U;
    context.system.date_time.minute = 12U;
    context.system.date_time.second = 30U;
    context.system.date_time.valid = true;
    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        context.system.cylinder[index].pressure_mpa = 10.0F + (float) index;
        context.system.cylinder[index].pressure_quality = GAS_PRESSURE_VALID;
        context.system.cylinder[index].state = GAS_CYL_WAIT_TEST;
        context.log_service.previous_state[index] = GAS_CYL_WAIT_TEST;
        // 本用例只验证日志格式，先把状态快照对齐，避免状态机新增的初始化→待测试事件混入计数。
    }
    context.system.total_pressure.pressure_mpa = 0.625F;
    context.system.total_pressure.pressure_quality = GAS_PRESSURE_VALID;

    assert(A_GasLog_Task(&context.log_service, &context.system));
    assert(A_GasLog_GetCount(&context.log_service) == 1U);
    assert(A_GasLog_ReadRecord(&context.log_service, 0U, record));
    assert((record[0] == A_GAS_LOG_TYPE_REGULAR) &&
           (record[1] == A_GAS_LOG_FORMAT_VERSION));
    assert((record[6] == 26U) && (record[7] == 8U) && (record[8] == 18U));
    assert((record[12] == 0x27U) && (record[13] == 0x10U)); // 10.000 MPa编码为10000。

    Test_ModbusWriteSingle(&context,
                           F_MODBUS_HOLDING_BASE_ADDRESS + A_MODBUS_HOLDING_LOG_INDEX,
                           0U);
    Test_ModbusWriteSingle(&context,
                           F_MODBUS_HOLDING_BASE_ADDRESS + A_MODBUS_HOLDING_LOG_COMMAND,
                           A_MODBUS_LOG_COMMAND_READ);
    assert(F_Modbus_GetHoldingRegister(&context.external_modbus.function,
                                       A_MODBUS_HOLDING_LOG_RESULT,
                                       &result));
    assert(result == A_MODBUS_LOG_RESULT_SUCCESS);
    assert(context.external_modbus.function.holding_register[A_MODBUS_HOLDING_LOG_COUNT] == 1U);
    assert(context.external_modbus.function.holding_register[A_MODBUS_HOLDING_LOG_CAPACITY] ==
           A_GAS_LOG_RECORD_CAPACITY);
    assert(context.external_modbus.function.holding_register[A_MODBUS_HOLDING_LOG_RECORD_SIZE] ==
           A_GAS_LOG_RECORD_SIZE);
    assert(context.external_modbus.function.holding_register[A_MODBUS_HOLDING_LOG_DATA_BASE] ==
           0x0101U);
    assert(context.external_modbus.function.holding_register[A_MODBUS_HOLDING_LOG_DATA_BASE + 1U] ==
           0x0000U);
    assert(context.external_modbus.function.holding_register[A_MODBUS_HOLDING_LOG_DATA_BASE + 2U] ==
           0x0001U);

    Test_ModbusWriteSingle(&context,
                           F_MODBUS_HOLDING_BASE_ADDRESS + A_MODBUS_HOLDING_LOG_INDEX,
                           1U);
    Test_ModbusWriteSingle(&context,
                           F_MODBUS_HOLDING_BASE_ADDRESS + A_MODBUS_HOLDING_LOG_COMMAND,
                           A_MODBUS_LOG_COMMAND_READ);
    assert(F_Modbus_GetHoldingRegister(&context.external_modbus.function,
                                       A_MODBUS_HOLDING_LOG_RESULT,
                                       &result));
    assert(result == A_MODBUS_LOG_RESULT_INVALID_INDEX);
    assert(context.external_modbus.function.holding_register[A_MODBUS_HOLDING_LOG_DATA_BASE] == 0U);

    assert(A_GasLog_Task(&context.log_service, &context.system));
    assert(A_GasLog_GetCount(&context.log_service) == 1U);
    context.system.date_time.minute = 30U;
    assert(A_GasLog_Task(&context.log_service, &context.system));
    assert(A_GasLog_GetCount(&context.log_service) == 2U);

    context.system.cylinder[0].state = GAS_CYL_READY;
    assert(A_GasLog_Task(&context.log_service, &context.system));
    assert(A_GasLog_GetCount(&context.log_service) == 3U);
    assert(A_GasLog_ReadRecord(&context.log_service, 2U, record));
    assert((record[0] == A_GAS_LOG_TYPE_EVENT) &&
           (record[12] == 1U) &&
           (record[13] == GAS_CYL_WAIT_TEST) &&
           (record[14] == GAS_CYL_READY));

    assert(A_GasLog_Init(&recovered, &context.storage_service, &context.system));
    assert(A_GasLog_GetCount(&recovered) == 3U);
    assert(A_GasLog_ReadRecord(&recovered, 0U, record));
    assert(record[0] == A_GAS_LOG_TYPE_REGULAR);

    for (log_index = 0U; log_index < 1015U; ++log_index)
    {
        context.system.cylinder[0].state =
            (context.system.cylinder[0].state == GAS_CYL_READY) ? GAS_CYL_INIT : GAS_CYL_READY;
        assert(A_GasLog_Task(&context.log_service, &context.system));
    }
    assert(A_GasLog_GetCount(&context.log_service) == A_GAS_LOG_RECORD_CAPACITY);
    assert(A_GasLog_ReadRecord(&context.log_service, 0U, record));
    assert((((uint32_t) record[2] << 24U) |
            ((uint32_t) record[3] << 16U) |
            ((uint32_t) record[4] << 8U) |
            (uint32_t) record[5]) == 2UL);
    assert(A_GasLog_ReadRecord(&context.log_service,
                              (uint16_t) (A_GAS_LOG_RECORD_CAPACITY - 1U),
                              record));
    assert((((uint32_t) record[2] << 24U) |
            ((uint32_t) record[3] << 16U) |
            ((uint32_t) record[4] << 8U) |
            (uint32_t) record[5]) == 1018UL);
    assert(A_GasLog_Init(&recovered, &context.storage_service, &context.system));
    assert(A_GasLog_GetCount(&recovered) == A_GAS_LOG_RECORD_CAPACITY);

    (void) memcpy(protected_data, g_test_state.eeprom, sizeof(protected_data));
    assert(A_GasLog_RequestClear(&context.log_service));
    assert(A_GasLog_IsClearBusy(&context.log_service));
    assert(!A_GasLog_IsReady(&context.log_service));
    assert(A_GasLog_GetCount(&context.log_service) == 0U);
    assert(!A_GasLog_ReadRecord(&context.log_service, 0U, record));
    for (clear_step = 0U;
         (clear_step < 1100U) && A_GasLog_IsClearBusy(&context.log_service);
         ++clear_step)
    {
        A_GasLog_ClearTask(&context.log_service, &context.system);
    }
    assert(clear_step <= 1021U);
    assert(A_GasLog_GetClearResult(&context.log_service) ==
           A_GAS_LOG_CLEAR_RESULT_SUCCESS);
    assert(A_GasLog_GetClearProgress(&context.log_service) == 100U);
    assert(A_GasLog_IsReady(&context.log_service));
    assert(A_GasLog_GetCount(&context.log_service) == 0U);
    assert(memcmp(protected_data, g_test_state.eeprom, sizeof(protected_data)) == 0);

    erased_header_address = (context.log_service.active_header_copy == 0U) ?
                            A_GAS_LOG_HEADER_B_ADDRESS : A_GAS_LOG_HEADER_A_ADDRESS;
    for (byte_index = erased_header_address;
         byte_index < ((size_t) erased_header_address + AT24C256_PAGE_SIZE_BYTES);
         ++byte_index)
    {
        assert(g_test_state.eeprom[byte_index] == 0xFFU);
    }
    for (byte_index = A_GAS_LOG_DATA_START_ADDRESS;
         byte_index < sizeof(g_test_state.eeprom);
         ++byte_index)
    {
        assert(g_test_state.eeprom[byte_index] == 0xFFU);
    }
    // V1.09只清除日志区：参数及通讯模式页保持不变，旧管理头和全部日志槽位均物理写成0xFF。

    assert(A_GasLog_Init(&recovered, &context.storage_service, &context.system));
    assert(A_GasLog_GetCount(&recovered) == 0U);
    assert(A_GasLog_Task(&recovered, &context.system));
    assert(A_GasLog_GetCount(&recovered) == 0U);
    context.system.cylinder[0].state =
        (context.system.cylinder[0].state == GAS_CYL_READY) ? GAS_CYL_INIT : GAS_CYL_READY;
    assert(A_GasLog_Task(&recovered, &context.system));
    assert(A_GasLog_GetCount(&recovered) == 1U);
    // 清除成功后当前半小时不立即补记常规日志，下一次真实状态变化从流水号1重新记录。
}

/*
 * 函数名：Test_TotalPressurePoll。
 * 说明：验证地址7响应写入独立总压力数据，并在本次处理后把轮询索引回绕到地址1。
 * 输入：无。
 * 输出：无。
 */
static void Test_TotalPressurePoll(void)
{
    A_Gas_Control_Context context; // 当前作用域变量，用于保存模块上下文。

    (void) memset(&g_test_state, 0, sizeof(g_test_state));
    A_GasControl_Init(&context);
    context.sensor_poll.pending_index = GAS_TOTAL_PRESSURE_SENSOR_INDEX;
    context.sensor_poll.master.response[2] = 4U;
    context.sensor_poll.master.response[3] = 0x40U;
    context.sensor_poll.master.response[4] = 0x50U;
    context.sensor_poll.master.response[5] = 0x00U;
    context.sensor_poll.master.response[6] = 0x00U;
    context.sensor_poll.master.result = MODBUS_POLL_RESULT_OK;
    context.sensor_poll.master.result_pending = true;

    A_ModbusPoll_Task(&context.sensor_poll, &context.system, &context.config, 100U);

    assert(context.system.total_pressure.pressure_quality == GAS_PRESSURE_VALID);
    assert(context.system.total_pressure.pressure_mpa == 3.25F);
    assert(context.system.total_pressure.pressure_timestamp_ms == 100U);
    assert(context.sensor_poll.poll_index == 0U);
}

/*
 * 函数名：Test_QualificationGate。
 * 说明：验证待测试门槛、低压待换自动撤销合格结论、三样本恢复确认及人工阀门权限。
 * 输入：无。
 * 输出：无。
 */
static void Test_QualificationGate(void)
{
    A_Gas_Control_Context context; // 当前作用域变量，用于保存模块上下文。

    (void) memset(&g_test_state, 0, sizeof(g_test_state));
    A_GasControl_Init(&context);
    context.config.pressure_fresh_ms = 65535U;
    context.system.mode = GAS_MODE_AUTO;
    Test_SeedPressure(&context, 0U, 5.0F);
    A_GasControl_Task(&context);
    assert(context.system.cylinder[0].state == GAS_CYL_WAIT_TEST);
    assert(!context.system.cylinder[0].qualification_passed);
    assert(A_GasControl_SetTestValve(&context, 0U, true));
    assert((context.total_test_pending_open_mask & 0x01U) != 0U);
    assert(A_GasControl_SetTestValve(&context, 0U, false));
    assert((context.total_test_pending_open_mask == 0U) &&
           !context.system.total_test_cmd &&
           !context.system.cylinder[0].test_cmd);
    // 分路尚未实际开启前收到关闭请求，必须撤销等待且不能残留总测试阀命令。
    context.system.mode = GAS_MODE_STOPPED;
    // 本用例只验证单瓶状态门槛，停止自动选瓶，避免刚进入待用便被选择为使用瓶。

    assert(A_GasControl_SetQualificationPassed(&context, 0U, true));
    A_GasControl_Task(&context);
    assert(context.system.cylinder[0].state == GAS_CYL_READY);

    assert(A_GasControl_SetQualificationPassed(&context, 0U, false));
    A_GasControl_Task(&context);
    assert(context.system.cylinder[0].state == GAS_CYL_WAIT_TEST);

    assert(A_GasControl_SetQualificationPassed(&context, 0U, true));
    A_GasControl_Task(&context);
    assert(context.system.cylinder[0].state == GAS_CYL_READY);

    Test_SeedPressure(&context, 0U, 1.0F);
    A_GasControl_Task(&context);
    assert(context.system.cylinder[0].state == GAS_CYL_LOW_REPLACE);
    assert(!context.system.cylinder[0].qualification_passed);
    assert(!A_GasControl_SetQualificationPassed(&context, 0U, true));

    context.platform.millis++;
    Test_SeedPressure(&context, 0U, 5.0F);
    A_GasControl_Task(&context);
    assert(context.system.cylinder[0].state == GAS_CYL_LOW_REPLACE);
    context.platform.millis++;
    Test_SeedPressure(&context, 0U, 5.0F);
    A_GasControl_Task(&context);
    assert(context.system.cylinder[0].state == GAS_CYL_LOW_REPLACE);
    context.platform.millis++;
    Test_SeedPressure(&context, 0U, 5.0F);
    A_GasControl_Task(&context);
    assert(context.system.cylinder[0].state == GAS_CYL_WAIT_TEST);
    assert(A_GasControl_SetQualificationPassed(&context, 0U, true));
    A_GasControl_Task(&context);
    assert(context.system.cylinder[0].state == GAS_CYL_READY);
}

/*
 * 函数名：Test_StateAndSwitch。
 * 说明：验证初始化、低压警告和顺序自动切换。
 * 输入：无。
 * 输出：无。
 */
static void Test_StateAndSwitch(void)
{
    A_Gas_Control_Context context; // 当前作用域变量，用于保存模块上下文。
    Test_Prepare(&context);
    assert(context.sensor_poll.sensor_addresses[GAS_TOTAL_PRESSURE_SENSOR_INDEX] == GAS_SENSOR_ADDRESS_TOTAL);
    assert(context.system.total_pressure.modbus_address == GAS_SENSOR_ADDRESS_TOTAL);
    assert(context.system.cylinder[0].state == GAS_CYL_LOW_REPLACE);
    assert(context.system.active_index == 1U);
    Test_SeedPressure(&context, 1U, 1.8F);
    A_GasControl_Task(&context);
    assert(context.system.cylinder[1].state == GAS_CYL_LOW_WARNING);
    Test_SeedPressure(&context, 1U, 1.0F);
    A_GasControl_Task(&context);
    context.platform.millis += 100U;
    Test_SeedPressure(&context, 1U, 1.0F);
    A_GasControl_Task(&context);
    A_GasControl_Task(&context);
    A_GasControl_Task(&context);
    A_GasControl_Task(&context);
    A_GasControl_Task(&context);
    A_GasControl_Task(&context);
    assert(context.system.active_index == 2U);
    assert(context.system.cylinder[2].supply_cmd);
    assert((context.system.cylinder[1].state == GAS_CYL_LOW_REPLACE) &&
           !context.system.cylinder[1].qualification_passed);
    // 自动切瓶完成后，原工作瓶必须进入低压待换并撤销上一轮测试通过结论。
}

/*
 * 函数名：Test_ManualAndDisabled。
 * 说明：验证排气与测试阀并行、独立超时、工作瓶互锁、停止态拒绝开阀和停用重启判断。
 * 输入：无。
 * 输出：无。
 */
static void Test_ManualAndDisabled(void)
{
    A_Gas_Control_Context context; // 当前作用域变量，用于保存模块上下文。
    uint32_t exhaust_deadline_ms; // 当前作用域变量，用于保存操作截止时间。

    Test_Prepare(&context);
    assert(A_GasControl_StartExhaust(&context, 0U));
    exhaust_deadline_ms = context.system.cylinder[0].exhaust_deadline_ms;
    Test_Advance(&context, 100U);
    assert(A_GasControl_StartExhaust(&context, 0U));
    assert(context.system.cylinder[0].exhaust_deadline_ms == exhaust_deadline_ms);
    // 排气中再次请求只返回当前成功状态，不得把原定时截止点向后延长。
    assert(A_GasControl_SetTestValve(&context, 0U, true));
    assert(!context.system.cylinder[0].test_cmd);
    Test_Advance(&context, 400U);
    assert(context.system.total_test_cmd && !context.system.cylinder[0].test_cmd);
    Test_Advance(&context, GAS_VALVE_BOOST_MIN_INTERVAL_MS);
    assert(context.system.cylinder[0].exhaust_cmd &&
           context.system.cylinder[0].test_cmd);
    Test_Advance(&context, 3999U);
    assert(context.system.cylinder[0].exhaust_cmd &&
           context.system.cylinder[0].test_cmd);
    Test_Advance(&context, 1U);
    assert(!context.system.cylinder[0].exhaust_cmd);
    assert(context.system.cylinder[0].test_cmd);
    Test_Advance(&context, 596000U);
    assert(!context.system.cylinder[0].test_cmd);
    assert(!context.system.total_test_cmd);

    Test_Prepare(&context);
    // 十分钟超时会使模拟压力样本自然过期；后续多路联动和停用场景使用独立初始状态。
    assert(A_GasControl_SetTestValve(&context, 0U, true));
    Test_Advance(&context, 0U);
    assert(context.system.total_test_cmd);
    assert(A_GasControl_SetTestValve(&context, 2U, true));
    Test_Advance(&context, GAS_VALVE_BOOST_MIN_INTERVAL_MS);
    assert(context.system.cylinder[0].test_cmd &&
           context.system.cylinder[2].test_cmd &&
           context.system.total_test_cmd);
    assert(A_GasControl_SetTestValve(&context, 0U, false));
    assert(!context.system.cylinder[0].test_cmd &&
           context.system.cylinder[2].test_cmd &&
           context.system.total_test_cmd);
    assert(A_GasControl_SetTestValve(&context, 2U, false));
    assert(!context.system.cylinder[2].test_cmd &&
           !context.system.total_test_cmd);
    // 多路测试共用总阀；关闭其中一路不影响其余分路，最后一路关闭后总阀才关闭。

    assert(!A_GasControl_StartExhaust(&context, context.system.active_index));
    assert(A_GasControl_SetCylinderDisabled(&context, 1U, true));
    assert(context.system.cylinder[1].state == GAS_CYL_DISABLED);
    assert(!context.system.cylinder[1].qualification_passed);
    A_GasControl_Task(&context);
    assert(context.system.active_index == 2U);
    assert(A_GasControl_SetCylinderDisabled(&context, 1U, false));
    assert(context.system.cylinder[1].state == GAS_CYL_INIT);
    F_ValveControl_AllOff(&context.platform, &context.system);
    context.system.mode = GAS_MODE_STOPPED;
    // V1.08删除人工整机停止入口，但内部故障锁定仍必须拒绝任何人工开阀。
    assert(!A_GasControl_StartExhaust(&context, 0U));
    assert(!A_GasControl_SetTestValve(&context, 0U, true));
}

/*
 * 函数名：Test_PlatformReadyValveGate。
 * 说明：验证平台未就绪时串口屏和业务接口都无法开启排气阀或测试阀，但仍可安全关阀。
 * 输入：无。
 * 输出：无；通过阀门命令、待开掩码和总测试阀状态断言保护生效。
 */
static void Test_PlatformReadyValveGate(void)
{
    const uint8_t exhaust_on_frame[] = {0xEEU,0xB1U,0x11U,0U,0U,0U,1U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU}; // 1号瓶排气阀开启事件。
    const uint8_t test_on_frame[] = {0xEEU,0xB1U,0x11U,0U,0U,0U,7U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU}; // 1号瓶测试阀开启事件。
    A_Gas_Control_Context context; // 当前作用域变量，用于保存气源控制上下文。

    Test_Prepare(&context);
    context.system.platform_ready = false;
    assert(!A_GasControl_StartExhaust(&context, 0U));
    assert(!A_GasControl_SetTestValve(&context, 0U, true));
    assert(!context.system.cylinder[0].exhaust_cmd &&
           !context.system.cylinder[0].test_cmd &&
           !context.system.total_test_cmd &&
           (context.total_test_pending_open_mask == 0U));

    Test_PushHmiFrame(&context, exhaust_on_frame, sizeof(exhaust_on_frame));
    A_GasControl_Task(&context);
    Test_PushHmiFrame(&context, test_on_frame, sizeof(test_on_frame));
    A_GasControl_Task(&context);
    assert(!context.system.cylinder[0].exhaust_cmd &&
           !context.system.cylinder[0].test_cmd &&
           !context.system.total_test_cmd &&
           (context.total_test_pending_open_mask == 0U));
    // 串口屏开阀事件必须在业务层被拒绝，不得留下延时开阀请求。

    Test_Prepare(&context);
    assert(A_GasControl_StartExhaust(&context, 0U));
    assert(A_GasControl_SetTestValve(&context, 0U, true));
    Test_Advance(&context, GAS_VALVE_BOOST_MIN_INTERVAL_MS);
    assert(context.system.total_test_cmd && !context.system.cylinder[0].test_cmd);
    Test_Advance(&context, GAS_VALVE_BOOST_MIN_INTERVAL_MS);
    assert(context.system.cylinder[0].exhaust_cmd &&
           context.system.cylinder[0].test_cmd &&
           context.system.total_test_cmd);
    context.system.platform_ready = false;
    assert(A_GasControl_StopExhaust(&context, 0U));
    assert(A_GasControl_SetTestValve(&context, 0U, false));
    assert(!context.system.cylinder[0].exhaust_cmd &&
           !context.system.cylinder[0].test_cmd &&
           !context.system.total_test_cmd);
    // 平台就绪标志丢失后仍必须能关闭已开阀门，避免保护反而阻断安全收敛。
}

/*
 * 函数名：Test_EmergencyAllOffRetry。
 * 说明：验证紧急全关的GPIO写入失败不会清除软件阀位，并在后续周期持续重试直到成功。
 * 输入：无。
 * 输出：无；通过业务命令、硬件镜像、重试标志、报警位和全关调用次数断言。
 */
static void Test_EmergencyAllOffRetry(void)
{
    A_Gas_Control_Context context; // 当前作用域变量，用于保存气源控制上下文。
    uint32_t close_count; // 故障注入前的全关调用次数，用于判断首次尝试和周期重试。

    Test_Prepare(&context);
    context.system.mode = GAS_MODE_STOPPED;
    context.system.active_index = 0U;
    context.switch_new_index = 1U;
    context.system.switch_state = GAS_SWITCH_VERIFY_NEW;
    context.system.cylinder[0].supply_cmd = true;
    context.system.cylinder[1].supply_cmd = true;
    context.platform.supply_state[0] = true;
    context.platform.supply_state[1] = true;
    close_count = g_test_state.all_valves_off_count;
    g_test_state.all_valves_off_fail = true;

    A_GasControl_Task(&context);
    assert(context.emergency_close_pending);
    assert(!context.system.platform_ready);
    assert((context.system.alarm_bits & GAS_ALARM_PLATFORM_NOT_READY) != 0U);
    assert(context.system.cylinder[0].supply_cmd &&
           context.system.cylinder[1].supply_cmd);
    assert(context.platform.supply_state[0] &&
           context.platform.supply_state[1]);
    assert(g_test_state.all_valves_off_count == (close_count + 1U));
    // 全关失败时业务和硬件镜像均保留“可能仍开启”，不得误报已关闭。

    g_test_state.all_valves_off_fail = false;
    A_GasControl_Task(&context);
    assert(!context.emergency_close_pending);
    assert(!context.system.cylinder[0].supply_cmd &&
           !context.system.cylinder[1].supply_cmd);
    assert(!context.platform.supply_state[0] &&
           !context.platform.supply_state[1]);
    assert(g_test_state.all_valves_off_count == (close_count + 2U));
    // 故障解除后的下一周期必须自动重试成功，然后才同步清零两层镜像。
}

/*
 * 函数名：Test_Hmi。
 * 说明：验证按钮业务映射、中文状态、双色阀位图标、状态高亮、总压力和RTC时间。
 * 输入：无。
 * 输出：无。
 */
static void Test_Hmi(void)
{
    const uint8_t exhaust_frame[] = {0xEEU,0xB1U,0x11U,0U,0U,0U,1U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU}; // 当前作用域变量，用于保存通信帧缓冲区或长度。
    const uint8_t test_frame[] = {0xEEU,0xB1U,0x11U,0U,0U,0U,7U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU}; // 当前作用域变量，用于保存通信帧缓冲区或长度。
    const uint8_t qualification_on_frame[] = {0xEEU,0xB1U,0x11U,0U,0U,0U,51U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU}; // 当前作用域变量，用于保存通信帧缓冲区或长度。
    const uint8_t qualification_off_frame[] = {0xEEU,0xB1U,0x11U,0U,0U,0U,51U,0x10U,1U,0U,0xFFU,0xFCU,0xFFU,0xFFU}; // 当前作用域变量，用于保存通信帧缓冲区或长度。
    const uint8_t event_log_frame[] = {0xEEU,0xB1U,0x11U,0U,2U,0U,61U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU}; // 当前作用域变量，用于保存通信帧缓冲区或长度。
    const uint8_t regular_log_frame[] = {0xEEU,0xB1U,0x11U,0U,3U,0U,65U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU}; // 当前作用域变量，用于保存通信帧缓冲区或长度。
    const uint8_t filter_time_on_frame[] = {0xEEU,0xB1U,0x11U,0U,6U,0U,131U,0x10U,1U,0U,0xFFU,0xFCU,0xFFU,0xFFU}; // 当前作用域变量，用于保存通信帧缓冲区或长度。
    const uint8_t filter_cylinder_trigger_frame[] = {0xEEU,0xB1U,0x11U,0U,6U,0U,132U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU}; // 当前作用域变量，用于保存气瓶对象或编号数组。
    const uint8_t filter_state_trigger_frame[] = {0xEEU,0xB1U,0x11U,0U,6U,0U,134U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU}; // 当前作用域变量，用于保存业务状态数组。
    const uint8_t filter_cylinder_menu_frame[] = {0xEEU,0xB1U,0x14U,0U,6U,0U,149U,0x1AU,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU}; // 当前作用域变量，用于保存气瓶对象或编号数组。
    const uint8_t filter_state_menu_frame[] = {0xEEU,0xB1U,0x14U,0U,6U,0U,150U,0x1AU,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU}; // 当前作用域变量，用于保存业务状态数组。
    const uint8_t filter_date_frame[] = {0xEEU,0xB1U,0x11U,0U,6U,0U,127U,0x11U,
                                         '2','0','2','6','0','8','2','2',0U,
                                         0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t filter_end_date_frame[] = {0xEEU,0xB1U,0x11U,0U,6U,0U,129U,0x11U,
                                             '2','0','2','6','0','8','2','3',0U,
                                             0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t filter_range_start_frame[] = {0xEEU,0xB1U,0x11U,0U,6U,0U,127U,0x11U,
                                                '2','0','2','6','0','8','2','9',0U,
                                                0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t filter_range_end_frame[] = {0xEEU,0xB1U,0x11U,0U,6U,0U,129U,0x11U,
                                              '2','0','2','6','0','8','3','0',0U,
                                              0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t filter_event_query_frame[] = {0xEEU,0xB1U,0x11U,0U,6U,0U,136U,0x10U,
                                                1U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t rtc_frame[] = {0xEEU,0xF7U,0x26U,0x08U,0x02U,0x18U,0x14U,0x35U,0x42U,0xFFU,0xFCU,0xFFU,0xFFU}; // 当前作用域变量，用于保存通信帧缓冲区或长度。
    const uint8_t low_warning_text[] = {0xB5U,0xCDU,0xD1U,0xB9U,0xBEU,0xAFU,0xB8U,0xE6U}; // 当前作用域变量，用于保存显示文本缓冲区或长度。
    const uint8_t wait_test_text[] = {0xB4U,0xFDU,0xB2U,0xE2U,0xCAU,0xD4U}; // 当前作用域变量，用于保存显示文本缓冲区或长度。
    const char sample_record[] = "2026-08-22 10:00:00;TEST;1;OK;";
    A_Gas_Control_Context gas; // 当前作用域变量，用于保存当前处理数据。
    A_Hmi_Context display; // 当前作用域变量，用于保存当前处理数据。
    F_Hmi_Context hmi; // 当前作用域变量，用于保存当前处理数据。
    H_Hmi_Context hmi_hardware; // 独立SCI9硬件测试实例，由协议函数显式接收。
    uint16_t id; // 当前作用域变量，用于保存当前处理数据。
    uint8_t value; // 当前作用域变量，用于保存当前处理值。
    size_t index; // 当前作用域变量，用于保存遍历索引。

    Test_Prepare(&gas);
    Test_PushHmiFrame(&gas, filter_time_on_frame, sizeof(filter_time_on_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.edit_filter.time_enabled);
    Test_PushHmiFrame(&gas, filter_cylinder_trigger_frame, sizeof(filter_cylinder_trigger_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.edit_filter.cylinder_number == 0U);
    Test_PushHmiFrame(&gas, filter_state_trigger_frame, sizeof(filter_state_trigger_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.edit_filter.target_state == 0U);
    // V1.10触发按钮只弹出下拉菜单，自身不改变筛选条件。
    Test_PushHmiFrame(&gas, filter_cylinder_menu_frame, sizeof(filter_cylinder_menu_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.edit_filter.cylinder_number == 1U);
    Test_PushHmiFrame(&gas, filter_state_menu_frame, sizeof(filter_state_menu_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.edit_filter.target_state == (uint8_t) GAS_CYL_INIT);
    gas.hmi_log.edit_filter.time_enabled = false;
    Test_PushHmiFrame(&gas, filter_date_frame, sizeof(filter_date_frame));
    A_GasControl_Task(&gas);
    assert((gas.hmi_log.edit_filter.start_year == 2026U) &&
           (gas.hmi_log.edit_filter.start_month == 8U) &&
           (gas.hmi_log.edit_filter.start_day == 22U) &&
           gas.hmi_log.edit_filter.time_enabled);
    // 输入任一合法日期或时间后必须自动启用限定时间，避免已输入的范围被“全部时间”忽略。
    Test_PushHmiFrame(&gas, filter_end_date_frame, sizeof(filter_end_date_frame));
    Test_PushHmiFrame(&gas, filter_event_query_frame, sizeof(filter_event_query_frame));
    A_GasControl_Task(&gas);
    assert((gas.hmi_log.active_filter.end_year == 2026U) &&
           (gas.hmi_log.active_filter.end_month == 8U) &&
           (gas.hmi_log.active_filter.end_day == 23U));
    // 输入框失焦帧和查询按钮同批到达时，查询快照必须采用本批刚提交的最后一个输入值。
    Test_PushHmiFrame(&gas, filter_range_start_frame, sizeof(filter_range_start_frame));
    Test_PushHmiFrame(&gas, filter_range_end_frame, sizeof(filter_range_end_frame));
    Test_PushHmiFrame(&gas, filter_event_query_frame, sizeof(filter_event_query_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.active_filter.time_enabled &&
           (gas.hmi_log.active_filter.start_year == 2026U) &&
           (gas.hmi_log.active_filter.start_month == 8U) &&
           (gas.hmi_log.active_filter.start_day == 29U) &&
           (gas.hmi_log.active_filter.end_year == 2026U) &&
           (gas.hmi_log.active_filter.end_month == 8U) &&
           (gas.hmi_log.active_filter.end_day == 30U));
    // 开始、结束日期与查询按钮连续到达时必须完整入队，查询范围不能退回旧日期而包含8月28日记录。
    // Screen6的开关、下拉菜单选择和YYYYMMDD文本由MCU条件结构统一保存。
    Test_PushHmiFrame(&gas, exhaust_frame, sizeof(exhaust_frame));
    A_GasControl_Task(&gas);
    assert(gas.system.cylinder[0].exhaust_cmd);

    gas.system.cylinder[0].state = GAS_CYL_WAIT_TEST;
    gas.system.cylinder[0].qualification_passed = false;
    Test_SeedPressure(&gas, 0U, 5.0F);
    Test_PushHmiFrame(&gas, qualification_on_frame, sizeof(qualification_on_frame));
    A_GasControl_Task(&gas);
    assert(gas.system.cylinder[0].qualification_passed);
    Test_PushHmiFrame(&gas, qualification_off_frame, sizeof(qualification_off_frame));
    A_GasControl_Task(&gas);
    assert(!gas.system.cylinder[0].qualification_passed);

    Test_PushHmiFrame(&gas, event_log_frame, sizeof(event_log_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.query_type == A_HMI_LOG_QUERY_EVENT);
    Test_PushHmiFrame(&gas, regular_log_frame, sizeof(regular_log_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.query_type == A_HMI_LOG_QUERY_REGULAR);

    assert(F_Hmi_Init(&hmi, &hmi_hardware));
    for (index = 0U; index < sizeof(test_frame); ++index) hmi_hardware.rx_buffer[hmi_hardware.rx_head++] = test_frame[index];
    F_Hmi_Task(&hmi, &hmi_hardware);
    assert(F_Hmi_TakeButtonEvent(&hmi, &id, &value));
    assert((id == 7U) && (value == 1U));
    assert(F_Hmi_SendText(&hmi, &hmi_hardware, 0U, 19U, "1.500", 5U));
    assert((g_test_state.hmi_tx_length == 16U) && (g_test_state.hmi_tx[2] == 0x10U));
    assert(F_Hmi_SendIconFrame(&hmi, &hmi_hardware, 1U, 72U, 2U));
    assert((g_test_state.hmi_tx_length == 12U) &&
           (g_test_state.hmi_tx[0] == 0xEEU) &&
           (g_test_state.hmi_tx[1] == 0xB1U) &&
           (g_test_state.hmi_tx[2] == 0x23U) &&
           (g_test_state.hmi_tx[4] == 1U) &&
           (g_test_state.hmi_tx[6] == 72U) &&
           (g_test_state.hmi_tx[7] == 2U));
    assert(F_Hmi_SendRecordClear(&hmi,
                                 &hmi_hardware,
                                 A_HMI_EVENT_LOG_PAGE_ID,
                                 A_HMI_EVENT_LOG_RECORD_CONTROL_ID));
    assert((g_test_state.hmi_tx_length == 11U) &&
           (g_test_state.hmi_tx[2] == 0x53U) &&
           (g_test_state.hmi_tx[4] == A_HMI_EVENT_LOG_PAGE_ID) &&
           (g_test_state.hmi_tx[6] == A_HMI_EVENT_LOG_RECORD_CONTROL_ID));
    assert(F_Hmi_SendRecordAdd(&hmi,
                               &hmi_hardware,
                               A_HMI_EVENT_LOG_PAGE_ID,
                               A_HMI_EVENT_LOG_RECORD_CONTROL_ID,
                               sample_record,
                               sizeof(sample_record) - 1U));
    assert((g_test_state.hmi_tx_length == (sizeof(sample_record) + 10U)) &&
           (g_test_state.hmi_tx[2] == 0x52U));

    gas.system.total_pressure.pressure_mpa = 4.321F;
    gas.system.total_pressure.pressure_quality = GAS_PRESSURE_VALID;
    assert(A_Hmi_Init(&display));
    assert(A_HMI_RTC_CONTROL_ID == 50U);
    assert((A_HMI_HIGHLIGHT_ICON_BASE == 72U) && (A_HMI_REFRESH_SLOT_COUNT == 68U));
    A_Hmi_Task(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx_length == 6U) &&
           (g_test_state.hmi_tx[0] == 0xEEU) &&
           (g_test_state.hmi_tx[1] == 0x82U));
    for (index = 0U; index < sizeof(rtc_frame); ++index)
    {
        display.hardware.rx_buffer[display.hardware.rx_head++] = rtc_frame[index];
    }
    A_Hmi_Task(&display, &gas.system, 1000U);
    assert(gas.system.date_time.valid);
    assert((gas.system.date_time.year == 2026U) &&
           (gas.system.date_time.month == 8U) &&
           (gas.system.date_time.day == 18U) &&
           (gas.system.date_time.week == 2U));
    assert((gas.system.date_time.hour == 14U) &&
           (gas.system.date_time.minute == 35U) &&
           (gas.system.date_time.second == 42U));

    F_ValveControl_AllOff(&gas.platform, &gas.system);
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx_length == 12U) &&
           (g_test_state.hmi_tx[6] == A_HMI_EXHAUST_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 0U));
    display.exhaust_refresh_pending_bits = 0U;
    A_Hmi_Refresh(&display, &gas.system, A_HMI_REFRESH_GAP_MS);
    assert((g_test_state.hmi_tx_length == 12U) &&
           (g_test_state.hmi_tx[6] == A_HMI_TEST_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 0U));
    display.test_refresh_pending_bits = 0U;
    A_Hmi_Refresh(&display, &gas.system, A_HMI_REFRESH_GAP_MS * 2U);
    assert((g_test_state.hmi_tx_length == 12U) &&
           (g_test_state.hmi_tx[6] == A_HMI_DISABLE_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 0U));
    display.disable_refresh_pending_bits = 0U;
    A_Hmi_Refresh(&display, &gas.system, A_HMI_REFRESH_GAP_MS * 3U);
    assert((g_test_state.hmi_tx_length == 12U) &&
           (g_test_state.hmi_tx[6] == A_HMI_QUALIFIED_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 0U));
    display.qualification_refresh_pending_bits = 0U;
    // 首次建立快照后依次优先回写四组1号按钮，测试中清空每组其余初始队列。

    gas.system.cylinder[0].exhaust_cmd = true;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_EXHAUST_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 1U));
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[2] == 0x23U) &&
           (g_test_state.hmi_tx[6] == A_HMI_EXHAUST_ICON_BASE) &&
           (g_test_state.hmi_tx[7] == A_HMI_VALVE_FRAME_OPEN));
    gas.system.cylinder[0].exhaust_cmd = false;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_EXHAUST_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 0U));
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[2] == 0x23U) &&
           (g_test_state.hmi_tx[6] == A_HMI_EXHAUST_ICON_BASE) &&
           (g_test_state.hmi_tx[7] == A_HMI_VALVE_FRAME_CLOSED));
    display.refresh_slot = 44U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_EXHAUST_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 0U));
    // 实际排气命令正反变化触发优先回写，普通刷新槽也会周期重申MCU最终状态。

    gas.system.cylinder[0].test_cmd = true;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_TEST_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 1U));
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[2] == 0x23U) &&
           (g_test_state.hmi_tx[6] == A_HMI_TEST_ICON_BASE) &&
           (g_test_state.hmi_tx[7] == A_HMI_VALVE_FRAME_OPEN));
    gas.system.cylinder[0].test_cmd = false;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_TEST_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 0U));
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[2] == 0x23U) &&
           (g_test_state.hmi_tx[6] == A_HMI_TEST_ICON_BASE) &&
           (g_test_state.hmi_tx[7] == A_HMI_VALVE_FRAME_CLOSED));
    display.refresh_slot = 50U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_TEST_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 0U));
    // 测试阀命令正反变化触发优先回写，50～55号刷新槽负责周期校正。

    gas.system.cylinder[0].state = GAS_CYL_DISABLED;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_DISABLE_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 1U));
    gas.system.cylinder[0].state = GAS_CYL_INIT;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_DISABLE_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 0U));
    display.refresh_slot = 56U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_DISABLE_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 0U));
    // 停用状态正反变化触发优先回写，56～61号刷新槽负责周期校正。

    gas.system.cylinder[0].qualification_passed = true;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_QUALIFIED_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 1U));
    gas.system.cylinder[0].qualification_passed = false;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_QUALIFIED_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 0U));
    display.refresh_slot = 62U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_QUALIFIED_BUTTON_BASE) &&
           (g_test_state.hmi_tx[7] == 0U));
    // 标志正反变化触发优先回写，普通刷新槽也会周期重申MCU实际状态。

    gas.system.cylinder[0].state = GAS_CYL_LOW_WARNING;
    display.refresh_slot = 12U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx_length == 19U) &&
           (g_test_state.hmi_tx[4] == A_HMI_MONITOR_PAGE_ID) &&
           (g_test_state.hmi_tx[5] == 0U) &&
           (g_test_state.hmi_tx[6] == A_HMI_STATE_TEXT_BASE));
    assert(memcmp(&g_test_state.hmi_tx[7], low_warning_text, sizeof(low_warning_text)) == 0);

    display.refresh_slot = 38U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx_length == 12U) &&
           (g_test_state.hmi_tx[2] == 0x23U) &&
           (g_test_state.hmi_tx[6] == A_HMI_HIGHLIGHT_ICON_BASE) &&
           (g_test_state.hmi_tx[7] == A_HMI_HIGHLIGHT_FRAME_WARNING));

    gas.system.cylinder[0].state = GAS_CYL_WAIT_TEST;
    display.refresh_slot = 12U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx_length == 17U) &&
           (g_test_state.hmi_tx[6] == A_HMI_STATE_TEXT_BASE));
    assert(memcmp(&g_test_state.hmi_tx[7], wait_test_text, sizeof(wait_test_text)) == 0);

    gas.system.cylinder[0].state = GAS_CYL_ACTIVE;
    display.refresh_slot = 38U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert(g_test_state.hmi_tx[7] == A_HMI_HIGHLIGHT_FRAME_ACTIVE);

    gas.system.cylinder[0].state = GAS_CYL_READY;
    display.refresh_slot = 38U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert(g_test_state.hmi_tx[7] == A_HMI_HIGHLIGHT_FRAME_NORMAL);

    gas.system.cylinder[5].state = GAS_CYL_LOW_WARNING;
    display.refresh_slot = 43U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == (A_HMI_HIGHLIGHT_ICON_BASE + 5U)) &&
           (g_test_state.hmi_tx[7] == A_HMI_HIGHLIGHT_FRAME_WARNING));

    gas.system.cylinder[0].supply_cmd = true;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx_length == 12U) &&
           (g_test_state.hmi_tx[2] == 0x23U) &&
           (g_test_state.hmi_tx[6] == A_HMI_SUPPLY_ICON_BASE) &&
           (g_test_state.hmi_tx[7] == A_HMI_VALVE_FRAME_OPEN));
    gas.system.cylinder[0].supply_cmd = false;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[6] == A_HMI_SUPPLY_ICON_BASE) &&
           (g_test_state.hmi_tx[7] == A_HMI_VALVE_FRAME_CLOSED));

    gas.system.cylinder[0].exhaust_cmd = false;
    display.refresh_slot = 24U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx_length == 12U) &&
           (g_test_state.hmi_tx[2] == 0x23U) &&
           (g_test_state.hmi_tx[6] == A_HMI_EXHAUST_ICON_BASE) &&
           (g_test_state.hmi_tx[7] == A_HMI_VALVE_FRAME_CLOSED));

    display.refresh_slot = 36U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    display.refresh_slot = 37U;
    display.next_refresh_ms = 0U;
    A_Hmi_Refresh(&display, &gas.system, 0U);
    assert((g_test_state.hmi_tx[5] == 0U) &&
           (g_test_state.hmi_tx[6] == A_HMI_TOTAL_PRESSURE_TEXT));
    assert(memcmp(&g_test_state.hmi_tx[7], "4.321", 5U) == 0);

    A_Hmi_Task(&display, &gas.system, 6000U);
    assert(!gas.system.date_time.valid);
}

/*
 * 函数名：Test_PushHmiFrame。
 * 说明：把一帧串口屏上传数据写入模拟SCI9接收环形缓冲区。
 * 输入：context为气源应用上下文；frame为只读帧；length为帧长度。
 * 输出：无；数据写入HMI硬件层接收缓存。
 */
static void Test_PushHmiFrame(A_Gas_Control_Context *context,
                              const uint8_t *frame,
                              size_t length)
{
    size_t index; // 当前作用域变量，用于保存遍历索引。

    assert((context != NULL) && (frame != NULL));
    for (index = 0U; index < length; ++index)
    {
        H_Hmi_Context *hardware = &context->hmi.hardware; // 当前作用域变量，用于保存当前处理数据指针。
        hardware->rx_buffer[hardware->rx_head] = frame[index];
        hardware->rx_head = (uint16_t) ((hardware->rx_head + 1U) % H_HMI_RX_BUFFER_SIZE);
    }
}

/*
 * 函数名：Test_HmiLogMenuSelect。
 * 说明：验证Screen6气瓶和进入状态下拉菜单选中值、越界拒绝、触发按钮忽略和清除条件恢复。
 * 输入：无。
 * 输出：无；通过断言检查B1 14下拉菜单帧到筛选条件的映射。
 */
static void Test_HmiLogMenuSelect(void)
{
    const uint8_t cylinder_menu_six_frame[] =
        {0xEEU,0xB1U,0x14U,0U,6U,0U,149U,0x1AU,6U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t state_menu_seven_frame[] =
        {0xEEU,0xB1U,0x14U,0U,6U,0U,150U,0x1AU,7U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t state_menu_release_frame[] =
        {0xEEU,0xB1U,0x14U,0U,6U,0U,150U,0x1AU,2U,0U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t cylinder_menu_invalid_frame[] =
        {0xEEU,0xB1U,0x14U,0U,6U,0U,149U,0x1AU,7U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t state_menu_invalid_frame[] =
        {0xEEU,0xB1U,0x14U,0U,6U,0U,150U,0x1AU,8U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t cylinder_menu_all_frame[] =
        {0xEEU,0xB1U,0x14U,0U,6U,0U,149U,0x1AU,0U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t state_menu_all_frame[] =
        {0xEEU,0xB1U,0x14U,0U,6U,0U,150U,0x1AU,0U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t reset_frame[] =
        {0xEEU,0xB1U,0x11U,0U,6U,0U,138U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t direct_menu_frame[] =
        {0xEEU,0xB1U,0x14U,0U,6U,0U,149U,0x1AU,4U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    A_Gas_Control_Context gas; // 当前作用域变量，用于保存当前处理数据。
    F_Hmi_Context hmi; // 当前作用域变量，用于保存当前处理数据。
    H_Hmi_Context hmi_hardware; // 独立SCI9硬件测试实例，由协议函数显式接收。
    uint16_t id; // 当前作用域变量，用于保存当前处理数据。
    uint8_t value; // 当前作用域变量，用于保存当前处理值。
    size_t index; // 当前作用域变量，用于保存遍历索引。

    Test_Prepare(&gas);
    Test_PushHmiFrame(&gas, cylinder_menu_six_frame, sizeof(cylinder_menu_six_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.edit_filter.cylinder_number == 6U);
    Test_PushHmiFrame(&gas, state_menu_seven_frame, sizeof(state_menu_seven_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.edit_filter.target_state == (uint8_t) GAS_CYL_WAIT_TEST);
    // 弹起状态同样携带选中索引，重复锁存同一值对筛选条件幂等。
    Test_PushHmiFrame(&gas, state_menu_release_frame, sizeof(state_menu_release_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.edit_filter.target_state == (uint8_t) GAS_CYL_READY);
    // 超出菜单定义范围的索引必须被拒绝，防止脏数据污染查询条件。
    Test_PushHmiFrame(&gas, cylinder_menu_invalid_frame, sizeof(cylinder_menu_invalid_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.edit_filter.cylinder_number == 6U);
    Test_PushHmiFrame(&gas, state_menu_invalid_frame, sizeof(state_menu_invalid_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.edit_filter.target_state == (uint8_t) GAS_CYL_READY);
    // 选中索引0对应“全部”，与业务层默认值保持一致。
    Test_PushHmiFrame(&gas, cylinder_menu_all_frame, sizeof(cylinder_menu_all_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.edit_filter.cylinder_number == 0U);
    Test_PushHmiFrame(&gas, state_menu_all_frame, sizeof(state_menu_all_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_log.edit_filter.target_state == 0U);
    Test_PushHmiFrame(&gas, reset_frame, sizeof(reset_frame));
    A_GasControl_Task(&gas);
    assert((gas.hmi_log.edit_filter.cylinder_number == 0U) &&
           (gas.hmi_log.edit_filter.target_state == 0U) &&
           (gas.hmi_log.filter_status == A_HMI_LOG_FILTER_STATUS_RESET));
    assert((gas.system.alarm_bits & GAS_ALARM_MANUAL_VALVE_ABORTED) == 0UL);

    assert(F_Hmi_Init(&hmi, &hmi_hardware));
    for (index = 0U; index < sizeof(direct_menu_frame); ++index)
    {
        hmi_hardware.rx_buffer[hmi_hardware.rx_head++] = direct_menu_frame[index];
    }
    F_Hmi_Task(&hmi, &hmi_hardware);
    assert(F_Hmi_TakeButtonEvent(&hmi, &id, &value));
    assert((id == A_HMI_LOG_FILTER_CYLINDER_MENU_ID) && (value == 4U));
    // B1 14下拉菜单帧解析为“控件ID＋选中项索引”，由应用层按控件ID分流处理。
}

/*
 * 函数名：Test_HmiConfig。
 * 说明：验证B1文本解析、单字段自动确认、取消、运行中持久化和计时参数立即应用。
 * 输入：无。
 * 输出：无；通过断言检查HMI专属三项和EEPROM V4记录。
 */
static void Test_HmiConfig(void)
{
    const uint8_t open_frame[] =
        {0xEEU,0xB1U,0x11U,0U,0U,0U,78U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t warning_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,82U,0x11U,'3','.','5','0','0',0U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t pressure_max_integer_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,83U,0x11U,'2','0',0U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t pressure_max_decimal1_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,83U,0x11U,'2','0','.', '0',0U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t pressure_max_decimal2_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,83U,0x11U,'2','0','.', '0','0',0U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t pressure_max_space_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,83U,0x11U,' ','2','0','.', '0','0','\r','\n',0U,
         0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t exhaust_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,89U,0x11U,'6','5','.','5','3','5',0U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t invalid_exhaust_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,89U,0x11U,'6','5','.','5','3','6',0U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t test_time_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,90U,0x11U,'4','5',0U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t invalid_test_time_low_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,90U,0x11U,'4',0U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t invalid_test_time_high_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,90U,0x11U,'6','1',0U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t confirm_frame[] =
        {0xEEU,0xB1U,0x11U,0U,5U,0U,108U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t cancel_frame[] =
        {0xEEU,0xB1U,0x11U,0U,5U,0U,109U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t default_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,97U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t log_clear_open_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,142U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t log_clear_confirm_frame[] =
        {0xEEU,0xB1U,0x11U,0U,7U,0U,146U,0x10U,1U,1U,0xFFU,0xFCU,0xFFU,0xFFU};
    const uint8_t protocol_text_frame[] =
        {0xEEU,0xB1U,0x11U,0U,4U,0U,82U,0x11U,'2','.','5',0U,0xFFU,0xFCU,0xFFU,0xFFU};
    A_Gas_Control_Context gas; // 当前作用域变量，用于保存当前处理数据。
    F_Hmi_Context protocol; // 当前作用域变量，用于保存当前处理数据。
    H_Hmi_Context protocol_hardware; // 独立SCI9硬件测试实例，由协议函数显式接收。
    Gas_Config stored; // 当前作用域变量，用于保存当前处理数据。
    char text[8]; // 当前作用域变量，用于保存显示文本缓冲区或长度。
    uint16_t page_id; // 当前作用域变量，用于保存串口屏画面标识。
    uint16_t control_id; // 当前作用域变量，用于保存串口屏控件标识。
    size_t length; // 当前作用域变量，用于保存有效数据长度。
    size_t index; // 当前作用域变量，用于保存遍历索引。
    uint16_t clear_step; // 当前作用域变量，用于保存当前处理数据。

    assert(F_Hmi_Init(&protocol, &protocol_hardware));
    for (index = 0U; index < sizeof(protocol_text_frame); ++index)
    {
        protocol_hardware.rx_buffer[protocol_hardware.rx_head++] = protocol_text_frame[index];
    }
    F_Hmi_Task(&protocol, &protocol_hardware);
    length = F_Hmi_TakeTextEvent(&protocol, &page_id, &control_id, text, sizeof(text));
    assert((length == 3U) && (page_id == 4U) && (control_id == 82U));
    assert(memcmp(text, "2.5", 4U) == 0);

    Test_Prepare(&gas);
    Test_PushHmiFrame(&gas, open_frame, sizeof(open_frame));
    A_GasControl_Task(&gas);
    assert(A_HmiConfig_IsActive(&gas.hmi_config));
    assert((gas.hmi_config.edit_config.switch_release_mpa > 1.299F) &&
           (gas.hmi_config.edit_config.switch_release_mpa < 1.301F) &&
           (gas.hmi_config.edit_config.pressure_fresh_ms == 2500U));

    Test_PushHmiFrame(&gas, pressure_max_integer_frame, sizeof(pressure_max_integer_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_config.confirm_pending &&
           (gas.hmi_config.edit_config.pressure_max_mpa > 19.999F) &&
           (gas.hmi_config.edit_config.pressure_max_mpa < 20.001F));
    Test_PushHmiFrame(&gas, cancel_frame, sizeof(cancel_frame));
    A_GasControl_Task(&gas);
    assert(!gas.hmi_config.confirm_pending &&
           (gas.hmi_config.edit_config.pressure_max_mpa > 24.999F));

    Test_PushHmiFrame(&gas, pressure_max_decimal1_frame, sizeof(pressure_max_decimal1_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_config.confirm_pending &&
           (gas.hmi_config.edit_config.pressure_max_mpa > 19.999F));
    Test_PushHmiFrame(&gas, cancel_frame, sizeof(cancel_frame));
    A_GasControl_Task(&gas);
    Test_PushHmiFrame(&gas, pressure_max_decimal2_frame, sizeof(pressure_max_decimal2_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_config.confirm_pending &&
           (gas.hmi_config.edit_config.pressure_max_mpa > 19.999F));
    Test_PushHmiFrame(&gas, cancel_frame, sizeof(cancel_frame));
    A_GasControl_Task(&gas);
    Test_PushHmiFrame(&gas, pressure_max_space_frame, sizeof(pressure_max_space_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_config.confirm_pending &&
           (gas.hmi_config.edit_config.pressure_max_mpa > 19.999F));
    Test_PushHmiFrame(&gas, cancel_frame, sizeof(cancel_frame));
    A_GasControl_Task(&gas);
    assert((gas.config.pressure_max_mpa > 24.999F) &&
           (gas.config.pressure_max_mpa < 25.001F));
    // 整数、一位小数、两位小数和带首尾空白的输入都必须被规范为20.000候选值。

    Test_PushHmiFrame(&gas, warning_frame, sizeof(warning_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_config.confirm_pending &&
           (gas.config.low_warning_pressure_mpa > 1.999F));
    gas.system.switch_state = GAS_SWITCH_LOW_CONFIRM;
    gas.switch_low_sample_count = 2U;
    gas.switch_low_start_ms = 10U;
    Test_PushHmiFrame(&gas, confirm_frame, sizeof(confirm_frame));
    A_GasControl_Task(&gas);
    assert(!gas.hmi_config.confirm_pending && !gas.hmi_config.save_pending);
    assert(gas.system.mode == GAS_MODE_AUTO);
    assert((gas.config.low_warning_pressure_mpa > 3.499F) &&
           (gas.config.low_warning_pressure_mpa < 3.501F));
    assert((gas.system.switch_state == GAS_SWITCH_IDLE) &&
           (gas.switch_low_sample_count == 0U));

    Test_PushHmiFrame(&gas, exhaust_frame, sizeof(exhaust_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_config.confirm_pending &&
           (gas.hmi_config.edit_config.manual_exhaust_time_ms == 65535U));
    Test_PushHmiFrame(&gas, confirm_frame, sizeof(confirm_frame));
    A_GasControl_Task(&gas);
    assert(gas.config.manual_exhaust_time_ms == 65535U);

    Test_PushHmiFrame(&gas, invalid_exhaust_frame, sizeof(invalid_exhaust_frame));
    A_GasControl_Task(&gas);
    assert(!gas.hmi_config.confirm_pending &&
           (gas.hmi_config.edit_config.manual_exhaust_time_ms == 65535U));
    Test_PushHmiFrame(&gas, cancel_frame, sizeof(cancel_frame));
    A_GasControl_Task(&gas);

    Test_PushHmiFrame(&gas, invalid_test_time_low_frame, sizeof(invalid_test_time_low_frame));
    A_GasControl_Task(&gas);
    assert(!gas.hmi_config.confirm_pending &&
           (gas.config.test_valve_max_time_ms == GAS_DEFAULT_TEST_VALVE_MAX_TIME_MS));
    Test_PushHmiFrame(&gas, invalid_test_time_high_frame, sizeof(invalid_test_time_high_frame));
    A_GasControl_Task(&gas);
    assert(!gas.hmi_config.confirm_pending &&
           (gas.config.test_valve_max_time_ms == GAS_DEFAULT_TEST_VALVE_MAX_TIME_MS));

    Test_PushHmiFrame(&gas, test_time_frame, sizeof(test_time_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_config.confirm_pending &&
           (gas.hmi_config.edit_config.test_valve_max_time_ms ==
            (45U * GAS_MILLISECONDS_PER_MINUTE)));
    Test_PushHmiFrame(&gas, confirm_frame, sizeof(confirm_frame));
    A_GasControl_Task(&gas);
    assert(gas.config.test_valve_max_time_ms == (45U * GAS_MILLISECONDS_PER_MINUTE));

    Test_PushHmiFrame(&gas, default_frame, sizeof(default_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_config.confirm_pending &&
           (gas.hmi_config.edit_config.manual_exhaust_time_ms == GAS_DEFAULT_MANUAL_EXHAUST_TIME_MS));
    Test_PushHmiFrame(&gas, cancel_frame, sizeof(cancel_frame));
    A_GasControl_Task(&gas);
    assert(!gas.hmi_config.confirm_pending &&
           (gas.config.manual_exhaust_time_ms == 65535U));
    // 恢复默认也只生成完整候选值，返回修改后必须保留当前运行参数。

    assert((gas.config.switch_release_mpa > 1.299F) &&
           (gas.config.switch_release_mpa < 1.301F) &&
           (gas.config.pressure_fresh_ms == 2500U));
    assert(A_GasConfig_Load(&gas.storage_service, &stored));
    assert((stored.low_warning_pressure_mpa > 3.499F) &&
           (stored.low_warning_pressure_mpa < 3.501F));
    assert((stored.manual_exhaust_time_ms == 65535U) &&
           (stored.test_valve_max_time_ms == (45U * GAS_MILLISECONDS_PER_MINUTE)) &&
           (stored.pressure_fresh_ms == 2500U));

    gas.system.cylinder[0].state = GAS_CYL_READY;
    assert(A_GasControl_StartExhaust(&gas, 0U));
    assert((gas.system.cylinder[0].exhaust_deadline_ms -
            gas.platform.millis) == 65535U);
    Test_Advance(&gas, 65535U);
    assert(!gas.system.cylinder[0].exhaust_cmd);
    gas.system.cylinder[0].state = GAS_CYL_READY;
    assert(A_GasControl_SetTestValve(&gas, 0U, true));
    assert(gas.system.cylinder[0].test_deadline_ms == 0U);
    Test_Advance(&gas, 0U);
    assert(gas.system.total_test_cmd && !gas.system.cylinder[0].test_cmd);
    Test_Advance(&gas, GAS_VALVE_BOOST_MIN_INTERVAL_MS);
    assert((gas.system.cylinder[0].test_deadline_ms -
            gas.platform.millis) == (45U * GAS_MILLISECONDS_PER_MINUTE));
    // 测试阀实际打开后才启动保存的45分钟上限，不能把总阀预开启等待计入测试时间。
    Test_Advance(&gas, (45U * GAS_MILLISECONDS_PER_MINUTE) - 1U);
    assert(gas.system.cylinder[0].test_cmd && gas.system.total_test_cmd);
    Test_Advance(&gas, 1U);
    assert(!gas.system.cylinder[0].test_cmd && !gas.system.total_test_cmd);

    gas.system.date_time.year = 2026U;
    gas.system.date_time.month = 8U;
    gas.system.date_time.day = 28U;
    gas.system.date_time.hour = 16U;
    gas.system.date_time.minute = 10U;
    gas.system.date_time.second = 0U;
    gas.system.date_time.valid = true;
    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        gas.log_service.previous_state[index] = gas.system.cylinder[index].state;
    }
    assert(A_GasLog_Task(&gas.log_service, &gas.system));
    assert(A_GasLog_GetCount(&gas.log_service) > 0U);

    Test_PushHmiFrame(&gas, log_clear_open_frame, sizeof(log_clear_open_frame));
    A_GasControl_Task(&gas);
    assert(gas.hmi_config.log_clear_dialog_active);
    assert(gas.hmi_config.log_clear_status == A_HMI_LOG_CLEAR_WAIT_CONFIRM);
    assert(!A_GasLog_IsClearBusy(&gas.log_service));
    Test_PushHmiFrame(&gas, log_clear_confirm_frame, sizeof(log_clear_confirm_frame));
    A_GasControl_Task(&gas);
    assert(A_GasLog_IsClearBusy(&gas.log_service));
    assert(gas.hmi_config.log_clear_status == A_HMI_LOG_CLEAR_BUSY);
    assert(!A_HmiLog_Request(&gas.hmi_log, A_HMI_LOG_QUERY_EVENT));
    for (clear_step = 0U;
         (clear_step < 1100U) && A_GasLog_IsClearBusy(&gas.log_service);
         ++clear_step)
    {
        A_GasControl_Task(&gas);
    }
    assert(A_GasLog_GetClearResult(&gas.log_service) ==
           A_GAS_LOG_CLEAR_RESULT_SUCCESS);
    assert(gas.hmi_config.log_clear_status == A_HMI_LOG_CLEAR_SUCCESS);
    assert(gas.hmi_config.log_clear_progress == 100U);
    assert(gas.hmi_config.log_clear_count == 0U);
    assert(A_GasLog_GetCount(&gas.log_service) == 0U);
    assert(A_GasConfig_Load(&gas.storage_service, &stored));
    assert((stored.manual_exhaust_time_ms == 65535U) &&
           (stored.test_valve_max_time_ms == (45U * GAS_MILLISECONDS_PER_MINUTE)));
    // Screen4密码入口、Screen7二次确认和后台进度联动不得清除或改写运行参数。
}

/*
 * 函数名：Test_HmiLogQuery。
 * 说明：验证事件和常规日志分页索引、第一页优先显示、上一页和下一页，以及常规记录双行输出。
 * 输入：无。
 * 输出：无；通过断言校验控件ID、每页数量、翻页方向、索引总数、快照变化提示和中文状态。
 */
static void Test_HmiLogQuery(void)
{
    A_Gas_Control_Context gas; // 当前作用域变量，用于保存当前处理数据。
    uint8_t record[A_GAS_LOG_RECORD_SIZE]; // 当前作用域变量，用于保存日志或配置记录缓冲区。
    uint16_t log_count; // 当前作用域变量，用于保存数量计数。
    uint16_t event_count = 0U; // 当前作用域变量，用于保存数量计数。
    uint16_t regular_count = 0U; // 当前作用域变量，用于保存数量计数。
    uint16_t filtered_event_count = 0U; // 当前作用域变量，用于保存数量计数。
    uint16_t filtered_regular_count = 0U; // 当前作用域变量，用于保存数量计数。
    uint16_t sent_rows; // 当前作用域变量，用于保存显示文本缓冲区或长度。
    uint16_t clear_count; // 当前作用域变量，用于保存数量计数。
    uint16_t status_count; // 当前作用域变量，用于保存数量计数。
    uint16_t page_info_count; // 当前作用域变量，用于保存数量计数。
    uint16_t step; // 当前作用域变量，用于保存当前处理数据。
    uint32_t previous_tx_count; // 当前作用域变量，用于保存数量计数。
    uint32_t saved_next_sequence; // 当前作用域变量，用于保存日志流水号。
    uint8_t first_event_text[A_HMI_LOG_ROW_MAX_SIZE]; // 当前作用域变量，用于保存显示文本缓冲区或长度。
    size_t first_event_length = 0U; // 当前作用域变量，用于保存有效数据长度。
    uint8_t regular_send_phase; // 当前作用域变量，用于保存结束边界。
    bool regular_content_cleared; // regular_content_cleared 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未清除，true表示已清除。
    bool first_event_saved; // first_event_saved 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示首条事件尚未保存，true表示首条事件已经保存。

    Test_Prepare(&gas);
    gas.system.date_time.year = 2026U;
    gas.system.date_time.month = 8U;
    gas.system.date_time.day = 22U;
    gas.system.date_time.hour = 10U;
    gas.system.date_time.minute = 30U;
    gas.system.date_time.second = 0U;
    gas.system.date_time.valid = true;
    for (step = 0U; step < 12U; ++step)
    {
        assert(A_GasLog_Task(&gas.log_service, &gas.system));
        // 首次有效时间会依次补记状态事件并生成当前半小时常规快照。
    }

    for (step = 0U; step < 20U; ++step)
    {
        gas.system.cylinder[0].state = ((step & 1U) == 0U) ? GAS_CYL_READY : GAS_CYL_INIT;
        gas.system.date_time.second = (uint8_t) step;
        assert(A_GasLog_Task(&gas.log_service, &gas.system));
        // 交替状态产生多条事件，用于验证全量扫描与最新优先发送。
    }
    for (step = 0U; step < 12U; ++step)
    {
        gas.system.date_time.day = 23U;
        gas.system.date_time.hour = (uint8_t) (step / 2U);
        gas.system.date_time.minute = ((step & 1U) == 0U) ? 0U : 30U;
        gas.system.date_time.second = 0U;
        assert(A_GasLog_Task(&gas.log_service, &gas.system));
        // 改变半小时时段产生多条常规记录，用于验证统一两列表格的双行格式。
    }

    log_count = A_GasLog_GetCount(&gas.log_service);
    assert(log_count > 0U);
    for (step = 0U; step < log_count; ++step)
    {
        assert(A_GasLog_ReadRecord(&gas.log_service, step, record));
        if (record[0] == (uint8_t) A_GAS_LOG_TYPE_EVENT)
        {
            event_count++;
            if ((record[7] == 8U) && (record[8] == 22U) &&
                (record[12] == 1U) && (record[14] == (uint8_t) GAS_CYL_INIT))
            {
                filtered_event_count++;
            }
        }
        else if (record[0] == (uint8_t) A_GAS_LOG_TYPE_REGULAR)
        {
            regular_count++;
            if ((record[7] == 8U) && (record[8] == 23U))
            {
                filtered_regular_count++;
            }
        }
    }
    assert((event_count > A_HMI_EVENT_LOG_VISIBLE_COUNT) &&
           (regular_count > A_HMI_REGULAR_LOG_VISIBLE_COUNT) &&
           (filtered_event_count > 0U) && (filtered_regular_count > 0U));

    sent_rows = 0U;
    clear_count = 0U;
    status_count = 0U;
    page_info_count = 0U;
    first_event_saved = false;
    previous_tx_count = g_test_state.hmi_tx_count; // 当前作用域变量，用于保存数量计数。
    assert(A_HmiLog_Request(&gas.hmi_log, A_HMI_LOG_QUERY_EVENT));
    for (step = 0U; (step < 4096U) && A_HmiLog_IsBusy(&gas.hmi_log); ++step)
    {
        A_HmiLog_Task(&gas.hmi_log, gas.system.date_time.valid);
        if (previous_tx_count == g_test_state.hmi_tx_count)
        {
            continue;
        }
        previous_tx_count = g_test_state.hmi_tx_count; // 当前作用域变量，用于保存数量计数。
        assert(g_test_state.hmi_tx_length <= F_HMI_TX_MAX_SIZE);
        assert((g_test_state.hmi_tx[0] == 0xEEU) &&
               (g_test_state.hmi_tx[4] == A_HMI_EVENT_LOG_PAGE_ID));
        if (g_test_state.hmi_tx[2] == 0x53U)
        {
            clear_count++;
            assert(g_test_state.hmi_tx[6] == A_HMI_EVENT_LOG_RECORD_CONTROL_ID);
        }
        else if (g_test_state.hmi_tx[2] == 0x52U)
        {
            size_t index; // 当前作用域变量，用于保存遍历索引。
            uint8_t separator_count = 0U; // 当前作用域变量，用于保存数量计数。

            sent_rows++;
            assert(g_test_state.hmi_tx[6] == A_HMI_EVENT_LOG_RECORD_CONTROL_ID);
            if (!first_event_saved)
            {
                first_event_length = g_test_state.hmi_tx_length - 11U;
                assert(first_event_length <= sizeof(first_event_text));
                (void) memcpy(first_event_text, &g_test_state.hmi_tx[7], first_event_length);
                first_event_saved = true;
            }
            // 表格采用底部追加，因此第一条发送的匹配事件必须是最新事件。
            for (index = 7U; index < (g_test_state.hmi_tx_length - 4U); ++index)
            {
                if (g_test_state.hmi_tx[index] == (uint8_t) ';')
                {
                    separator_count++;
                }
            }
            assert(separator_count == 4U);
        }
        else if (g_test_state.hmi_tx[2] == 0x10U)
        {
            if (g_test_state.hmi_tx[6] == A_HMI_EVENT_LOG_STATUS_CONTROL_ID)
            {
                status_count++;
            }
            else
            {
                assert(g_test_state.hmi_tx[6] == A_HMI_EVENT_LOG_PAGE_INFO_CONTROL_ID);
                page_info_count++;
                if (page_info_count == 1U)
                {
                    assert(!gas.hmi_log.index_complete);
                    assert(gas.hmi_log.scanned_count < log_count);
                    // 第一页必须在完整索引统计结束之前送达，保证刷新后的可见响应速度。
                }
            }
        }
        else
        {
            assert(false);
        }
    }
    assert(!A_HmiLog_IsBusy(&gas.hmi_log));
    assert((clear_count == 1U) &&
           (sent_rows == A_HMI_EVENT_LOG_VISIBLE_COUNT) &&
           (status_count >= 2U) &&
           (page_info_count >= 2U));
    assert((first_event_length >= 19U) &&
           (memcmp(first_event_text, "2026-08-22 10:30:19", 19U) == 0));
    assert((gas.hmi_log.scan_logical_index == 0U) &&
           (gas.hmi_log.scanned_count == log_count) &&
           gas.hmi_log.index_complete &&
           (gas.hmi_log.matched_count == event_count) &&
           (gas.hmi_log.current_page == 0U));
    // 后台仍需扫描完整快照以建立索引，但串口屏只接收当前页15条事件日志。

    sent_rows = 0U;
    clear_count = 0U;
    previous_tx_count = g_test_state.hmi_tx_count; // 当前作用域变量，用于保存数量计数。
    assert(A_HmiLog_RequestPage(&gas.hmi_log,
                                A_HMI_LOG_QUERY_EVENT,
                                A_HMI_LOG_PAGE_NEXT));
    for (step = 0U; (step < 1024U) && A_HmiLog_IsBusy(&gas.hmi_log); ++step)
    {
        A_HmiLog_Task(&gas.hmi_log, gas.system.date_time.valid);
        if (previous_tx_count == g_test_state.hmi_tx_count)
        {
            continue;
        }
        previous_tx_count = g_test_state.hmi_tx_count; // 当前作用域变量，用于保存数量计数。
        if (g_test_state.hmi_tx[2] == 0x53U)
        {
            clear_count++;
        }
        else if (g_test_state.hmi_tx[2] == 0x52U)
        {
            sent_rows++;
        }
    }
    assert(!A_HmiLog_IsBusy(&gas.hmi_log));
    assert((gas.hmi_log.current_page == 1U) &&
           (clear_count == 1U) &&
           (sent_rows == (((uint16_t) (event_count - A_HMI_EVENT_LOG_VISIBLE_COUNT) >
                            A_HMI_EVENT_LOG_VISIBLE_COUNT) ?
                           A_HMI_EVENT_LOG_VISIBLE_COUNT :
                           (uint16_t) (event_count - A_HMI_EVENT_LOG_VISIBLE_COUNT))));

    assert(A_HmiLog_RequestPage(&gas.hmi_log,
                                A_HMI_LOG_QUERY_EVENT,
                                A_HMI_LOG_PAGE_PREVIOUS));
    for (step = 0U; (step < 1024U) && A_HmiLog_IsBusy(&gas.hmi_log); ++step)
    {
        A_HmiLog_Task(&gas.hmi_log, gas.system.date_time.valid);
    }
    assert(!A_HmiLog_IsBusy(&gas.hmi_log));
    assert(gas.hmi_log.current_page == 0U);
    // “下一页”增加页码并查看更早记录，“上一页”减少页码并返回更新记录。

    sent_rows = 0U;
    clear_count = 0U;
    status_count = 0U;
    page_info_count = 0U;
    regular_send_phase = 0U;
    regular_content_cleared = false;
    previous_tx_count = g_test_state.hmi_tx_count; // 当前作用域变量，用于保存数量计数。
    assert(A_HmiLog_Request(&gas.hmi_log, A_HMI_LOG_QUERY_REGULAR));
    for (step = 0U; (step < 4096U) && A_HmiLog_IsBusy(&gas.hmi_log); ++step)
    {
        A_HmiLog_Task(&gas.hmi_log, gas.system.date_time.valid);
        if (previous_tx_count == g_test_state.hmi_tx_count)
        {
            continue;
        }
        previous_tx_count = g_test_state.hmi_tx_count; // 当前作用域变量，用于保存数量计数。
        assert(g_test_state.hmi_tx_length <= F_HMI_TX_MAX_SIZE);
        assert((g_test_state.hmi_tx[0] == 0xEEU) &&
               (g_test_state.hmi_tx[4] == A_HMI_REGULAR_LOG_PAGE_ID));
        if (g_test_state.hmi_tx[2] == 0x53U)
        {
            clear_count++;
            assert(g_test_state.hmi_tx[6] == A_HMI_REGULAR_LOG_CONTENT_CONTROL_ID);
            assert(!regular_content_cleared);
            regular_content_cleared = true;
        }
        else if (g_test_state.hmi_tx[2] == 0x52U)
        {
            size_t index; // 当前作用域变量，用于保存遍历索引。
            uint8_t separator_count = 0U; // 当前作用域变量，用于保存数量计数。

            sent_rows++;
            assert(g_test_state.hmi_tx[6] == A_HMI_REGULAR_LOG_CONTENT_CONTROL_ID);
            if (regular_send_phase == 0U)
            {
                size_t separator = 7U; // 当前作用域变量，用于保存当前处理数据。

                while ((separator < (g_test_state.hmi_tx_length - 4U)) &&
                       (g_test_state.hmi_tx[separator] != (uint8_t) ';'))
                {
                    separator++;
                }
                assert((separator + 2U) < (g_test_state.hmi_tx_length - 4U));
                assert((g_test_state.hmi_tx[separator + 1U] == 0xD1U) &&
                       (g_test_state.hmi_tx[separator + 2U] == 0xB9U));
                // 压力行第一列为时间，第二列以GBK“压力”开头。
            }
            else
            {
                assert((g_test_state.hmi_tx[7] == (uint8_t) ';') &&
                       (g_test_state.hmi_tx[8] == 0xD7U) &&
                       (g_test_state.hmi_tx[9] == 0xB4U));
                // 状态行第一列留空，第二列以GBK“状态”开头。
            }
            regular_send_phase = (uint8_t) ((regular_send_phase + 1U) % 2U);
            for (index = 7U; index < (g_test_state.hmi_tx_length - 4U); ++index)
            {
                if (g_test_state.hmi_tx[index] == (uint8_t) ';')
                {
                    separator_count++;
                }
            }
            assert(separator_count == 2U);
        }
        else if (g_test_state.hmi_tx[2] == 0x10U)
        {
            if (g_test_state.hmi_tx[6] == A_HMI_REGULAR_LOG_STATUS_CONTROL_ID)
            {
                status_count++;
            }
            else
            {
                assert(g_test_state.hmi_tx[6] == A_HMI_REGULAR_LOG_PAGE_INFO_CONTROL_ID);
                page_info_count++;
                if (page_info_count == 1U)
                {
                    assert(!gas.hmi_log.index_complete);
                    assert(gas.hmi_log.scanned_count < log_count);
                }
            }
        }
        else
        {
            assert(false);
        }
    }
    assert(!A_HmiLog_IsBusy(&gas.hmi_log));
    assert(regular_send_phase == 0U);
    assert((clear_count == 1U) && regular_content_cleared &&
            (sent_rows == (uint16_t) (A_HMI_REGULAR_LOG_VISIBLE_COUNT * 2U)) &&
            (status_count >= 2U) && (page_info_count >= 2U));
    assert((gas.hmi_log.scan_logical_index == 0U) &&
           (gas.hmi_log.scanned_count == log_count) &&
           gas.hmi_log.index_complete &&
           (gas.hmi_log.matched_count == regular_count) &&
           (gas.hmi_log.current_page == 0U));

    sent_rows = 0U;
    clear_count = 0U;
    previous_tx_count = g_test_state.hmi_tx_count; // 当前作用域变量，用于保存数量计数。
    assert(A_HmiLog_RequestPage(&gas.hmi_log,
                                A_HMI_LOG_QUERY_REGULAR,
                                A_HMI_LOG_PAGE_NEXT));
    for (step = 0U; (step < 1024U) && A_HmiLog_IsBusy(&gas.hmi_log); ++step)
    {
        A_HmiLog_Task(&gas.hmi_log, gas.system.date_time.valid);
        if (previous_tx_count == g_test_state.hmi_tx_count)
        {
            continue;
        }
        previous_tx_count = g_test_state.hmi_tx_count; // 当前作用域变量，用于保存数量计数。
        if (g_test_state.hmi_tx[2] == 0x53U)
        {
            clear_count++;
        }
        else if (g_test_state.hmi_tx[2] == 0x52U)
        {
            sent_rows++;
        }
    }
    assert(!A_HmiLog_IsBusy(&gas.hmi_log));
    assert((gas.hmi_log.current_page == 1U) &&
           (clear_count == 1U) &&
           (sent_rows == (uint16_t) (((((uint16_t) (regular_count -
                                                    A_HMI_REGULAR_LOG_VISIBLE_COUNT) >
                                        A_HMI_REGULAR_LOG_VISIBLE_COUNT) ?
                                       A_HMI_REGULAR_LOG_VISIBLE_COUNT :
                                       (uint16_t) (regular_count -
                                                   A_HMI_REGULAR_LOG_VISIBLE_COUNT)) * 2U))));

    assert(A_HmiLog_RequestPage(&gas.hmi_log,
                                A_HMI_LOG_QUERY_REGULAR,
                                A_HMI_LOG_PAGE_LATEST));
    for (step = 0U; (step < 1024U) && A_HmiLog_IsBusy(&gas.hmi_log); ++step)
    {
        A_HmiLog_Task(&gas.hmi_log, gas.system.date_time.valid);
    }
    assert(!A_HmiLog_IsBusy(&gas.hmi_log));
    assert(gas.hmi_log.current_page == 0U);
    // “最新页”直接返回零起始页0，不重新扫描已经完成的EEPROM日志索引。

    gas.hmi_log.edit_filter.time_enabled = true;
    gas.hmi_log.edit_filter.start_year = 2026U;
    gas.hmi_log.edit_filter.start_month = 8U;
    gas.hmi_log.edit_filter.start_day = 22U;
    gas.hmi_log.edit_filter.start_hour = 0U;
    gas.hmi_log.edit_filter.start_minute = 0U;
    gas.hmi_log.edit_filter.start_second = 0U;
    gas.hmi_log.edit_filter.end_year = gas.hmi_log.edit_filter.start_year;
    gas.hmi_log.edit_filter.end_month = gas.hmi_log.edit_filter.start_month;
    gas.hmi_log.edit_filter.end_day = gas.hmi_log.edit_filter.start_day;
    gas.hmi_log.edit_filter.end_hour = 23U;
    gas.hmi_log.edit_filter.end_minute = 59U;
    gas.hmi_log.edit_filter.end_second = 59U;
    gas.hmi_log.edit_filter.cylinder_number = 1U;
    gas.hmi_log.edit_filter.target_state = (uint8_t) GAS_CYL_INIT;
    assert(A_HmiLog_Request(&gas.hmi_log, A_HMI_LOG_QUERY_EVENT));
    for (step = 0U; (step < 4096U) && A_HmiLog_IsBusy(&gas.hmi_log); ++step)
    {
        A_HmiLog_Task(&gas.hmi_log, gas.system.date_time.valid);
    }
    assert(!A_HmiLog_IsBusy(&gas.hmi_log));
    assert(gas.hmi_log.matched_count == filtered_event_count);
    // 事件查询同时按包含边界的时间、1号瓶和新状态“初始化”筛选。

    gas.hmi_log.edit_filter.start_day = 23U;
    gas.hmi_log.edit_filter.end_day = 23U;
    assert(A_HmiLog_Request(&gas.hmi_log, A_HMI_LOG_QUERY_REGULAR));
    for (step = 0U; (step < 4096U) && A_HmiLog_IsBusy(&gas.hmi_log); ++step)
    {
        A_HmiLog_Task(&gas.hmi_log, gas.system.date_time.valid);
    }
    assert(!A_HmiLog_IsBusy(&gas.hmi_log));
    assert(gas.hmi_log.matched_count == filtered_regular_count);
    // 常规日志应按23日时间范围匹配，并忽略事件专用的气瓶和进入状态条件。

    gas.hmi_log.edit_filter.start_day = 24U;
    gas.hmi_log.edit_filter.end_day = 23U;
    assert(A_HmiLog_Request(&gas.hmi_log, A_HMI_LOG_QUERY_EVENT));
    for (step = 0U; (step < 256U) && A_HmiLog_IsBusy(&gas.hmi_log); ++step)
    {
        A_HmiLog_Task(&gas.hmi_log, gas.system.date_time.valid);
    }
    assert(!A_HmiLog_IsBusy(&gas.hmi_log));
    assert(gas.hmi_log.filter_error && (gas.hmi_log.scanned_count == 0U));
    assert((g_test_state.hmi_tx[2] == 0x10U) &&
           (g_test_state.hmi_tx[6] == A_HMI_EVENT_LOG_STATUS_CONTROL_ID) &&
           (memcmp(&g_test_state.hmi_tx[7],
                   "\xCA\xB1\xBC\xE4\xB7\xB6\xCE\xA7\xB4\xED\xCE\xF3", 12U) == 0));
    // 开始晚于结束时不读取EEPROM，结果页直接显示GBK“时间范围错误”。
    gas.hmi_log.edit_filter.time_enabled = false;
    gas.hmi_log.edit_filter.start_day = 1U;
    gas.hmi_log.edit_filter.end_day = 31U;
    gas.hmi_log.edit_filter.cylinder_number = 0U;
    gas.hmi_log.edit_filter.target_state = 0U;

    saved_next_sequence = gas.log_service.next_sequence;
    assert(A_HmiLog_Request(&gas.hmi_log, A_HMI_LOG_QUERY_EVENT));
    for (step = 0U; (step < 4096U) && A_HmiLog_IsBusy(&gas.hmi_log); ++step)
    {
        A_HmiLog_Task(&gas.hmi_log, gas.system.date_time.valid);
        if ((gas.hmi_log.scanned_count > 0U) &&
            (gas.log_service.next_sequence == saved_next_sequence))
        {
            gas.log_service.next_sequence++;
            // 模拟查询期间新日志提交，下一周期应终止旧快照并保留已显示行。
        }
    }
    assert(!A_HmiLog_IsBusy(&gas.hmi_log));
    assert((g_test_state.hmi_tx[2] == 0x10U) &&
           (g_test_state.hmi_tx[6] == A_HMI_EVENT_LOG_STATUS_CONTROL_ID) &&
           (g_test_state.hmi_tx_length >= 21U) &&
           (memcmp(&g_test_state.hmi_tx[7],
                   "\xC8\xD5\xD6\xBE\xD2\xD1\xB8\xFC\xD0\xC2", 10U) == 0));
    gas.log_service.next_sequence = saved_next_sequence;
    // 快照变化时必须显示GBK“日志已更新”，且不使用已过期快照继续追加。

    gas.system.date_time.valid = false;
    assert(A_HmiLog_Request(&gas.hmi_log, A_HMI_LOG_QUERY_EVENT));
    for (step = 0U; (step < 4096U) && A_HmiLog_IsBusy(&gas.hmi_log); ++step)
    {
        A_HmiLog_Task(&gas.hmi_log, gas.system.date_time.valid);
    }
    assert(!A_HmiLog_IsBusy(&gas.hmi_log));
    assert((g_test_state.hmi_tx[2] == 0x10U) &&
           (g_test_state.hmi_tx[6] == A_HMI_EVENT_LOG_STATUS_CONTROL_ID) &&
           (g_test_state.hmi_tx_length >= 19U) &&
           (memcmp(&g_test_state.hmi_tx[7],
                   "\xCA\xB1\xBC\xE4\xCE\xDE\xD0\xA7", 8U) == 0));
    // RTC无效时既有日志仍可读取，但查询结束必须明确提示当前不会新增日志。
}

/*
 * 函数名：main。
 * 说明：执行三阀七状态、日志、外部通讯、七路压力、串口屏RTC与卡片高亮和自动切瓶主机单元测试。
 * 输入：无。
 * 输出：全部测试通过时返回 0。
 */
int main(void)
{
    Test_ConfigV2Migration();
    Test_ConfigV3Migration();
    Test_TestValveTimeRange();
    Test_TotalPressurePoll();
    Test_DefaultCanProtocol();
    Test_CanDirectWriteAndControl();
    Test_ExternalModbusConfig();
    Test_GasLog();
    Test_QualificationGate();
    Test_StateAndSwitch();
    Test_ManualAndDisabled();
    Test_PlatformReadyValveGate();
    Test_EmergencyAllOffRetry();
    Test_Hmi();
    Test_HmiLogMenuSelect();
    Test_HmiConfig();
    Test_HmiLogQuery();
    puts("V1.13平台就绪开阀保护、紧急全关失败重试、日志查询和通讯回归测试通过。");
    return 0;
}
