/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现CAN扩展帧协议编解码、校验和异步响应队列。
 */

#include "F_Can_Protocol.h"

#include <stddef.h>
#include <string.h>

#define F_CAN_ID_FUNCTION_SHIFT       (24U) // 功能码位于标识符bit28～bit24。
#define F_CAN_ID_TARGET_TYPE_SHIFT    (19U) // 目标类型位于标识符bit23～bit19。
#define F_CAN_ID_TARGET_ADDRESS_SHIFT (12U) // 目标地址位于标识符bit18～bit12。
#define F_CAN_ID_SOURCE_TYPE_SHIFT    (7U)  // 源类型位于标识符bit11～bit7。
#define F_CAN_ID_FUNCTION_MASK        (0x1FU)// 功能码和设备类型使用5位掩码。
#define F_CAN_ID_ADDRESS_MASK         (0x7FU)// 设备地址使用7位掩码。

/*
 * 函数名：F_CanProtocol_NextQueueIndex。
 * 说明：计算协议发送环形队列的下一个索引。
 * 输入：index 为当前索引。
 * 输出：返回按发送队列容量回绕后的索引。
 */
static uint8_t F_CanProtocol_NextQueueIndex(uint8_t index)
{
    return (uint8_t) ((index + 1U) % F_CAN_TX_QUEUE_CAPACITY);
}

/*
 * 函数名：F_CanProtocol_BuildId。
 * 说明：把功能码、目标节点和源节点组合为参考协议的29位扩展标识符。
 * 输入：function为功能码；target_type和target_address为目标；source_type和source_address为源节点。
 * 输出：返回组合后的29位CAN标识符。
 */
static uint32_t F_CanProtocol_BuildId(F_Can_Function function,
                                      uint8_t target_type,
                                      uint8_t target_address,
                                      uint8_t source_type,
                                      uint8_t source_address)
{
    return (((uint32_t) function & F_CAN_ID_FUNCTION_MASK) << F_CAN_ID_FUNCTION_SHIFT) |
           (((uint32_t) target_type & F_CAN_ID_FUNCTION_MASK) << F_CAN_ID_TARGET_TYPE_SHIFT) |
           (((uint32_t) target_address & F_CAN_ID_ADDRESS_MASK) << F_CAN_ID_TARGET_ADDRESS_SHIFT) |
           (((uint32_t) source_type & F_CAN_ID_FUNCTION_MASK) << F_CAN_ID_SOURCE_TYPE_SHIFT) |
           ((uint32_t) source_address & F_CAN_ID_ADDRESS_MASK);
}

/*
 * 函数名：F_CanProtocol_CalculateCrc。
 * 说明：按参考协议对数据字节0、1、3～7及标识符功能码计算8位校验值。
 * 输入：id 为29位CAN标识符；data 为固定8字节数据区。
 * 输出：返回由Modbus CRC16中间结果右移4位得到的低8位校验值。
 */
uint8_t F_CanProtocol_CalculateCrc(uint32_t id, const uint8_t data[H_CAN_FRAME_DATA_LENGTH])
{
    uint8_t crc_data[H_CAN_FRAME_DATA_LENGTH]; // 当前作用域变量，用于保存CRC校验值数组。
    uint16_t crc = 0xFFFFU; // 当前作用域变量，用于保存CRC校验值。
    uint8_t byte_index; // 当前作用域变量，用于保存遍历索引。
    uint8_t bit_index; // 当前作用域变量，用于保存遍历索引。

    if (data == NULL)
    {
        return 0U;
    }

    crc_data[0] = data[0];
    crc_data[1] = data[1];
    crc_data[2] = data[3];
    crc_data[3] = data[4];
    crc_data[4] = data[5];
    crc_data[5] = data[6];
    crc_data[6] = data[7];
    crc_data[7] = (uint8_t) (id >> F_CAN_ID_FUNCTION_SHIFT);
    for (byte_index = 0U; byte_index < H_CAN_FRAME_DATA_LENGTH; ++byte_index)
    {
        crc = (uint16_t) (crc ^ (uint16_t) crc_data[byte_index]);
        for (bit_index = 0U; bit_index < 8U; ++bit_index)
        {
            crc = ((crc & 1U) != 0U) ? (uint16_t) ((crc >> 1U) ^ 0xA001U)
                                     : (uint16_t) (crc >> 1U);
        }
    }
    return (uint8_t) (crc >> 4U);
}

/*
 * 函数名：F_CanProtocol_Init。
 * 说明：初始化CAN0硬件层和自定义协议功能层，两者由上层并列持有。
 * 输入：context为功能层上下文；hardware为CAN硬件层；local_type和local_address为本机节点标识。
 * 输出：参数、节点标识及CAN0初始化均有效时返回true，否则返回false。
 */
bool F_CanProtocol_Init(F_Can_Protocol_Context *context,
                        H_Can_Context *hardware,
                        uint8_t local_type,
                        uint8_t local_address)
{
    if ((context == NULL) || (hardware == NULL) ||
        (local_type > F_CAN_ID_FUNCTION_MASK) || (local_address > F_CAN_ID_ADDRESS_MASK))
    {
        return false;
    }

    (void) memset(context, 0, sizeof(*context));
    context->local_type = local_type;
    context->local_address = local_address;
    if (!H_Can_Init(hardware))
    {
        return false;
    }
    context->ready = true;
    return true;
}

/*
 * 函数名：F_CanProtocol_Deinit。
 * 说明：通过硬件层关闭CAN0并清除协议就绪状态。
 * 输入：context为CAN协议功能层上下文输入输出指针。
 * 输出：CAN0已经关闭或成功关闭时返回true，否则返回false。
 */
bool F_CanProtocol_Deinit(F_Can_Protocol_Context *context, H_Can_Context *hardware)
{
    bool success; // success 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示操作失败，true表示操作成功。

    if ((context == NULL) || (hardware == NULL))
    {
        return false;
    }
    success = H_Can_Deinit(hardware);
    context->ready = false;
    return success;
}

/*
 * 函数名：F_CanProtocol_HasFault。
 * 说明：逐层查询CAN0硬件是否存在影响通讯的故障。
 * 输入：context为只读CAN协议功能层上下文。
 * 输出：功能层未就绪或硬件层故障时返回true，否则返回false。
 */
bool F_CanProtocol_HasFault(const F_Can_Protocol_Context *context,
                            const H_Can_Context *hardware)
{
    return ((context == NULL) || (hardware == NULL) || !context->ready ||
            H_Can_HasFault(hardware));
}

/*
 * 函数名：F_CanProtocol_QueueResponse。
 * 说明：构造一帧读或写响应并加入发送环形队列。
 * 输入：context为功能层上下文；function为响应功能码；target_type和target_address为目标；data_address为地址；value为32位数据。
 * 输出：成功入队时返回true，否则返回false。
 */
static bool F_CanProtocol_QueueResponse(F_Can_Protocol_Context *context,
                                        F_Can_Function function,
                                        uint8_t target_type,
                                        uint8_t target_address,
                                        uint16_t data_address,
                                        uint32_t value)
{
    H_Can_Frame *frame; // 当前作用域变量，用于保存通信帧缓冲区或长度指针。
    uint8_t next; // 当前作用域变量，用于保存当前处理数据。

    if ((context == NULL) || !context->ready ||
        (target_type > F_CAN_ID_FUNCTION_MASK) || (target_address > F_CAN_ID_ADDRESS_MASK))
    {
        return false;
    }
    next = F_CanProtocol_NextQueueIndex(context->transmit_head);
    if (next == context->transmit_tail)
    {
        return false;
    }

    frame = &context->transmit_queue[context->transmit_head];
    frame->id = F_CanProtocol_BuildId(function,
                                      target_type,
                                      target_address,
                                      context->local_type,
                                      context->local_address);
    frame->data[0] = (uint8_t) data_address;
    frame->data[1] = (uint8_t) (data_address >> 8U);
    frame->data[2] = 0U;
    frame->data[3] = 1U;
    frame->data[4] = (uint8_t) value;
    frame->data[5] = (uint8_t) (value >> 8U);
    frame->data[6] = (uint8_t) (value >> 16U);
    frame->data[7] = (uint8_t) (value >> 24U);
    frame->data[2] = F_CanProtocol_CalculateCrc(frame->id, frame->data);
    context->transmit_head = next;
    return true;
}

/*
 * 函数名：F_CanProtocol_QueueReadResponse。
 * 说明：将一个32位读响应加入非阻塞发送队列。
 * 输入：context 为功能层上下文；target_type和target_address为请求方节点；data_address为数据地址；value为32位原始数据。
 * 输出：响应成功入队时返回true，队列已满或参数无效时返回false。
 */
bool F_CanProtocol_QueueReadResponse(F_Can_Protocol_Context *context,
                                     uint8_t target_type,
                                     uint8_t target_address,
                                     uint16_t data_address,
                                     uint32_t value)
{
    return F_CanProtocol_QueueResponse(context,
                                       F_CAN_FUNCTION_READ_RESPONSE,
                                       target_type,
                                       target_address,
                                       data_address,
                                       value);
}

/*
 * 函数名：F_CanProtocol_QueueWriteResponse。
 * 说明：将写操作结果响应加入非阻塞发送队列。
 * 输入：context 为功能层上下文；target_type和target_address为请求方节点；data_address为写地址；result为协议结果码。
 * 输出：响应成功入队时返回true，队列已满或参数无效时返回false。
 */
bool F_CanProtocol_QueueWriteResponse(F_Can_Protocol_Context *context,
                                      uint8_t target_type,
                                      uint8_t target_address,
                                      uint16_t data_address,
                                      uint32_t result)
{
    return F_CanProtocol_QueueResponse(context,
                                       F_CAN_FUNCTION_WRITE_RESPONSE,
                                       target_type,
                                       target_address,
                                       data_address,
                                       result);
}

/*
 * 函数名：F_CanProtocol_Task。
 * 说明：非阻塞处理一帧接收数据及一帧待发送响应，并校验29位标识符和自定义CRC。
 * 输入：context 为CAN协议功能层上下文输入输出指针。
 * 输出：无；合法请求和响应发送状态保存在context中。
 */
void F_CanProtocol_Task(F_Can_Protocol_Context *context, H_Can_Context *hardware)
{
    H_Can_Frame frame; // 当前作用域变量，用于保存通信帧缓冲区或长度。

    if ((context == NULL) || (hardware == NULL) || !context->ready)
    {
        return;
    }

    if ((context->transmit_tail != context->transmit_head) &&
        !H_Can_IsTransmitBusy(hardware) &&
        H_Can_Send(hardware, &context->transmit_queue[context->transmit_tail]))
    {
        context->transmit_tail = F_CanProtocol_NextQueueIndex(context->transmit_tail);
        // 只有硬件层成功接收发送请求后才移出队列，CAN忙时保留到下一个周期重试。
    }

    if (context->request_pending || !H_Can_TakeFrame(hardware, &frame))
    {
        return;
    }
    else
    {
        uint8_t function = (uint8_t) ((frame.id >> F_CAN_ID_FUNCTION_SHIFT) & F_CAN_ID_FUNCTION_MASK); // 当前作用域变量，用于保存当前处理数据。
        uint8_t target_type = (uint8_t) ((frame.id >> F_CAN_ID_TARGET_TYPE_SHIFT) & F_CAN_ID_FUNCTION_MASK); // 当前作用域变量，用于保存数据类型。
        uint8_t target_address = (uint8_t) ((frame.id >> F_CAN_ID_TARGET_ADDRESS_SHIFT) & F_CAN_ID_ADDRESS_MASK); // 当前作用域变量，用于保存存储或寄存器地址。
        uint8_t data_length = frame.data[3]; // 当前作用域变量，用于保存有效数据长度。
        bool directed = ((target_type == context->local_type) &&
                         (target_address == context->local_address));
        bool broadcast_read = ((function == (uint8_t) F_CAN_FUNCTION_BROADCAST_READ) &&
                               ((target_type == context->local_type) ||
                                (target_type == F_CAN_BROADCAST_TYPE)) &&
                               ((target_address == context->local_address) ||
                                (target_address == F_CAN_BROADCAST_ADDRESS)));

        if ((!directed && !broadcast_read) ||
            ((function != (uint8_t) F_CAN_FUNCTION_WRITE) &&
             (function != (uint8_t) F_CAN_FUNCTION_READ) &&
             !broadcast_read) ||
            (data_length == 0U) || (data_length > F_CAN_MAX_CONSECUTIVE_VALUES) ||
            ((function == (uint8_t) F_CAN_FUNCTION_WRITE) && (data_length != 1U)) ||
            (frame.data[2] != F_CanProtocol_CalculateCrc(frame.id, frame.data)))
        {
            context->invalid_frame_count++;
            return;
        }

        context->pending_request.function = (F_Can_Function) function;
        context->pending_request.source_type =
            (uint8_t) ((frame.id >> F_CAN_ID_SOURCE_TYPE_SHIFT) & F_CAN_ID_FUNCTION_MASK);
        context->pending_request.source_address = (uint8_t) (frame.id & F_CAN_ID_ADDRESS_MASK);
        context->pending_request.data_address =
            (uint16_t) (((uint16_t) frame.data[1] << 8U) | frame.data[0]);
        context->pending_request.data_length = data_length;
        context->pending_request.value = ((uint32_t) frame.data[4]) |
                                         ((uint32_t) frame.data[5] << 8U) |
                                         ((uint32_t) frame.data[6] << 16U) |
                                         ((uint32_t) frame.data[7] << 24U);
        context->request_pending = true;
    }
}

/*
 * 函数名：F_CanProtocol_TakeRequest。
 * 说明：取出并清除一条已经通过目标地址、长度和CRC校验的读写请求。
 * 输入：context 为功能层上下文；request 为请求输出指针。
 * 输出：成功取出请求时返回true，没有请求或参数无效时返回false。
 */
bool F_CanProtocol_TakeRequest(F_Can_Protocol_Context *context, F_Can_Request *request)
{
    if ((context == NULL) || (request == NULL) || !context->request_pending)
    {
        return false;
    }
    *request = context->pending_request;
    context->request_pending = false;
    return true;
}
