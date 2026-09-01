/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现内部Modbus主站请求、响应校验、超时和重试状态机。
 */

#include "F_Modbus_Poll.h"

#include <string.h>

/*
 * 函数名：F_ModbusPoll_TimeReached。
 * 说明：使用无符号毫秒差判断当前时间是否已经到达截止时间。
 * 输入：now_ms 为当前时间；deadline_ms 为截止时间。
 * 输出：已经到达或超过截止时间时返回 true，否则返回 false。
 */
static bool F_ModbusPoll_TimeReached(uint32_t now_ms, uint32_t deadline_ms)
{
    return ((int32_t) (now_ms - deadline_ms) >= 0);
}

/*
 * 函数名：F_ModbusPoll_FinishTransaction。
 * 说明：结束当前内部 Modbus 事务，中止底层收发并保存待取结果。
 * 输入：context 为功能上下文；result 为事务最终结果。
 * 输出：无；更新事务状态和结果标志。
 */
static void F_ModbusPoll_FinishTransaction(F_Modbus_Poll_Context *context,
                              F_Modbus_Poll_Result result)
{
    if (context == NULL)
    {
        return;
    }

    H_ModbusPoll_Abort(&context->hardware);
    context->state = MODBUS_POLL_STATE_IDLE;
    context->result = result;
    context->result_pending = true;
}

/*
 * 函数名：F_ModbusPoll_Crc16。
 * 说明：按照 Modbus RTU 多项式计算指定数据的 CRC16。
 * 输入：data 为只读数据缓冲区；length 为参与计算的字节数。
 * 输出：返回 16 位 CRC，加入帧时低字节在前。
 */
uint16_t F_ModbusPoll_Crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU; // 当前作用域变量，用于保存CRC校验值。
    size_t i; // 当前作用域变量，用于保存当前处理数据。
    uint8_t bit; // 当前作用域变量，用于保存位掩码。

    if (data == NULL)
    {
        return crc;
    }

    for (i = 0U; i < length; ++i)
    {
        crc = (uint16_t) (crc ^ (uint16_t) data[i]);
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = ((crc & 0x0001U) != 0U) ?
                  (uint16_t) ((crc >> 1U) ^ 0xA001U) :
                  (uint16_t) (crc >> 1U);
        }
    }

    return crc;
}

/*
 * 函数名：F_ModbusPoll_Init。
 * 说明：初始化内部 Modbus 主站功能上下文并绑定 SCI1 硬件平台。
 * 输入：context 为主站功能上下文；platform 为气源硬件平台上下文。
 * 输出：硬件绑定成功时返回 true，否则返回 false。
 */
bool F_ModbusPoll_Init(F_Modbus_Poll_Context *context,
                       H_Gas_Platform_Context *platform)
{
    if ((context == NULL) || (platform == NULL))
    {
        return false;
    }

    (void) memset(context, 0, sizeof(*context));
    context->state = MODBUS_POLL_STATE_IDLE;
    return H_ModbusPoll_Init(&context->hardware, platform);
}

/*
 * 函数名：F_ModbusPoll_StartRead。
 * 说明：构造功能码 03 或 04 的请求帧，并启动一次非阻塞 Modbus RTU 读取事务。
 * 输入：context 为功能上下文；slave_address 为从站地址；function_code 为功能码；start_register 为起始寄存器；register_count 为数量；now_ms 为当前时间；timeout_ms 为超时。
 * 输出：事务成功启动时返回 true，否则返回 false。
 */
bool F_ModbusPoll_StartRead(F_Modbus_Poll_Context *context,
                            uint8_t slave_address,
                            uint8_t function_code,
                            uint16_t start_register,
                            uint16_t register_count,
                            uint32_t now_ms,
                            uint32_t timeout_ms)
{
    uint16_t crc; // 当前作用域变量，用于保存CRC校验值。
    size_t response_length; // 当前作用域变量，用于保存有效数据长度。

    if ((context == NULL) || (context->state != MODBUS_POLL_STATE_IDLE) ||
        context->result_pending || (slave_address == 0U) ||
        ((function_code != 0x03U) && (function_code != 0x04U)) ||
        (register_count == 0U))
    {
        return false;
    }

    response_length = 5U + ((size_t) register_count * 2U);
    // 正常响应由地址、功能码、字节数、寄存器数据和 CRC 组成，先计算长度再检查接收缓存边界。
    if (response_length > sizeof(context->response))
    {
        context->result = MODBUS_POLL_RESULT_PROTOCOL;
        context->result_pending = true;
        return false;
    }

    context->request[0] = slave_address;
    context->request[1] = function_code;
    context->request[2] = (uint8_t) (start_register >> 8U);
    context->request[3] = (uint8_t) start_register;
    context->request[4] = (uint8_t) (register_count >> 8U);
    context->request[5] = (uint8_t) register_count;
    crc = F_ModbusPoll_Crc16(context->request, 6U);
    context->request[6] = (uint8_t) crc;
    context->request[7] = (uint8_t) (crc >> 8U);

    context->slave_address = slave_address;
    context->function_code = function_code;
    context->register_count = register_count;
    context->expected_response_length = response_length;
    context->deadline_ms = now_ms + timeout_ms;
    context->result = MODBUS_POLL_RESULT_NONE;
    // 同一个截止时间覆盖发送和接收全过程，任一阶段停滞都能退出本次事务。

    if (!H_ModbusPoll_TxStart(&context->hardware,
                                context->request,
                                sizeof(context->request)))
    {
        F_ModbusPoll_FinishTransaction(context, MODBUS_POLL_RESULT_IO);
        return false;
    }

    context->state = MODBUS_POLL_STATE_TX;
    return true;
}

/*
 * 函数名：F_ModbusPoll_Task。
 * 说明：推进内部 Modbus 主站发送、接收、协议校验和超时状态机。
 * 输入：context 为功能上下文；now_ms 为当前毫秒计数。
 * 输出：无；完成后通过 context 保存事务结果。
 */
void F_ModbusPoll_Task(F_Modbus_Poll_Context *context, uint32_t now_ms)
{
    uint16_t received_crc; // 当前作用域变量，用于保存CRC校验值。
    uint16_t calculated_crc; // 当前作用域变量，用于保存CRC校验值。

    if ((context == NULL) || (context->state == MODBUS_POLL_STATE_IDLE))
    {
        return;
    }

    if (F_ModbusPoll_TimeReached(now_ms, context->deadline_ms))
    {
        F_ModbusPoll_FinishTransaction(context, MODBUS_POLL_RESULT_TIMEOUT);
        return;
    }

    if (context->state == MODBUS_POLL_STATE_TX)
    {
        if (H_ModbusPoll_TxDone(&context->hardware))
        {
            // RS485 必须等最后一个发送字节完成并切回接收方向后，才允许启动响应接收。
            if (H_ModbusPoll_RxStart(&context->hardware,
                                       context->response,
                                       context->expected_response_length))
            {
                context->state = MODBUS_POLL_STATE_RX;
            }
            else
            {
                F_ModbusPoll_FinishTransaction(context, MODBUS_POLL_RESULT_IO);
            }
        }
        return;
    }

    if ((context->state != MODBUS_POLL_STATE_RX) ||
        !H_ModbusPoll_RxDone(&context->hardware))
    {
        return;
    }

    received_crc = (uint16_t) ((uint16_t) context->response[context->expected_response_length - 2U] |
                               ((uint16_t) context->response[context->expected_response_length - 1U] << 8U));
    calculated_crc = F_ModbusPoll_Crc16(context->response,
                                         context->expected_response_length - 2U);
    // 先校验整帧 CRC，再核对从站、功能码和数据长度，避免错误帧被上层当成压力数据。

    if (received_crc != calculated_crc)
    {
        F_ModbusPoll_FinishTransaction(context, MODBUS_POLL_RESULT_CRC);
    }
    else if ((context->response[0] != context->slave_address) ||
             (context->response[1] != context->function_code) ||
             (context->response[2] != (uint8_t) (context->register_count * 2U)))
    {
        F_ModbusPoll_FinishTransaction(context, MODBUS_POLL_RESULT_PROTOCOL);
    }
    else
    {
        F_ModbusPoll_FinishTransaction(context, MODBUS_POLL_RESULT_OK);
    }
}

/*
 * 函数名：F_ModbusPoll_TakeResult。
 * 说明：取走最近一次事务结果，并在成功时返回 Modbus 响应的数据字段。
 * 输入：context 为功能上下文；result、payload、payload_length 为输出参数。
 * 输出：存在待取结果时返回 true，否则返回 false。
 */
bool F_ModbusPoll_TakeResult(F_Modbus_Poll_Context *context,
                             F_Modbus_Poll_Result *result,
                             const uint8_t **payload,
                             size_t *payload_length)
{
    if ((context == NULL) || !context->result_pending || (result == NULL) ||
        (payload == NULL) || (payload_length == NULL))
    {
        return false;
    }

    *result = context->result;
    if (context->result == MODBUS_POLL_RESULT_OK)
    {
        *payload = &context->response[3];
        *payload_length = context->response[2];
        // 返回的是上下文接收缓存视图，仅保证在下一次事务覆盖该缓存前有效。
    }
    else
    {
        *payload = NULL;
        *payload_length = 0U;
    }

    context->result_pending = false;
    context->result = MODBUS_POLL_RESULT_NONE;
    return true;
}
