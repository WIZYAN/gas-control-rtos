/*
 * Version: v1.14
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现RA4M1 CAN0初始化、邮箱收发和中断接收缓存。
 */

#include "H_Can.h"

#include <stddef.h>
#include <string.h>

#include "hal_data.h"

/*
 * 函数名：H_Can_NextQueueIndex。
 * 说明：计算CAN接收环形队列的下一个索引位置。
 * 输入：index 为当前队列索引。
 * 输出：返回按队列容量回绕后的下一个索引。
 */
static uint8_t H_Can_NextQueueIndex(uint8_t index)
{
    return (uint8_t) ((index + 1U) % H_CAN_RX_QUEUE_CAPACITY);
}

/*
 * 函数名：H_Can_Init。
 * 说明：打开FSP CAN0实例并把回调上下文绑定到当前硬件层实例。
 * 输入：context 为待初始化的CAN硬件层上下文输入输出指针。
 * 输出：CAN0成功打开并绑定回调时返回true，否则返回false。
 */
bool H_Can_Init(H_Can_Context *context)
{
    fsp_err_t result; // 当前作用域变量，用于保存操作结果。
    bool opened_here = false; // 本次初始化打开CAN实例的标志；使用范围：H_Can_Init错误回滚流程内；取值范围：false/true，false表示实例此前已打开，true表示本函数刚完成打开且失败时需要关闭。

    if (context == NULL)
    {
        return false;
    }

    (void) memset(context, 0, sizeof(*context));
    result = R_CAN_Open(&g_can0_ctrl, &g_can0_cfg);
    if (result == FSP_SUCCESS)
    {
        opened_here = true;
    }
    else if (result == FSP_ERR_ALREADY_OPEN)
    {
        result = FSP_SUCCESS;
    }
    else
    {
        return false;
    }

    result = R_CAN_CallbackSet(&g_can0_ctrl, can_callback, context, NULL);
    if (result != FSP_SUCCESS)
    {
        if (opened_here)
        {
            (void) R_CAN_Close(&g_can0_ctrl);
        }
        return false;
    }

    context->can_open = true;
    return true;
}

/*
 * 函数名：H_Can_Deinit。
 * 说明：关闭当前CAN0实例并清除异步收发状态，供外部通讯模式切换使用。
 * 输入：context 为CAN硬件层上下文输入输出指针。
 * 输出：CAN0已经关闭或成功关闭时返回true，否则返回false。
 */
bool H_Can_Deinit(H_Can_Context *context)
{
    fsp_err_t result; // 当前作用域变量，用于保存操作结果。

    if (context == NULL)
    {
        return false;
    }
    if (!context->can_open)
    {
        return true;
    }

    result = R_CAN_Close(&g_can0_ctrl);
    if ((result != FSP_SUCCESS) && (result != FSP_ERR_NOT_OPEN))
    {
        return false;
    }

    (void) memset(context, 0, sizeof(*context));
    return true;
}

/*
 * 函数名：H_Can_TakeFrame。
 * 说明：从接收中断环形队列取出一帧完整的29位扩展数据帧。
 * 输入：context 为CAN硬件层上下文；frame 为接收帧输出指针。
 * 输出：成功取出一帧时返回true，队列为空或参数无效时返回false。
 */
bool H_Can_TakeFrame(H_Can_Context *context, H_Can_Frame *frame)
{
    uint8_t tail; // 当前作用域变量，用于保存队列尾位置。

    if ((context == NULL) || (frame == NULL) ||
        (context->receive_tail == context->receive_head))
    {
        return false;
    }

    tail = context->receive_tail;
    *frame = context->receive_queue[tail];
    context->receive_tail = H_Can_NextQueueIndex(tail);
    return true;
}

/*
 * 函数名：H_Can_Send。
 * 说明：通过CAN0邮箱0异步发送一帧固定8字节的29位扩展数据帧。
 * 输入：context 为CAN硬件层上下文；frame 为待发送的只读帧。
 * 输出：成功启动发送时返回true，驱动忙或参数无效时返回false。
 */
bool H_Can_Send(H_Can_Context *context, const H_Can_Frame *frame)
{
    can_frame_t hardware_frame; // 当前作用域变量，用于保存通信帧缓冲区或长度。
    fsp_err_t result; // 当前作用域变量，用于保存操作结果。

    if ((context == NULL) || (frame == NULL) || !context->can_open ||
        context->transmit_busy || context->bus_off ||
        (frame->id > H_CAN_EXTENDED_ID_MAX))
    {
        return false;
    }

    (void) memset(&hardware_frame, 0, sizeof(hardware_frame));
    hardware_frame.id = frame->id;
    hardware_frame.id_mode = CAN_ID_MODE_EXTENDED;
    hardware_frame.type = CAN_FRAME_TYPE_DATA;
    hardware_frame.data_length_code = H_CAN_FRAME_DATA_LENGTH;
    (void) memcpy(hardware_frame.data, frame->data, H_CAN_FRAME_DATA_LENGTH);

    context->transmit_busy = true;
    result = R_CAN_Write(&g_can0_ctrl, H_CAN_TX_MAILBOX, &hardware_frame);
    if (result != FSP_SUCCESS)
    {
        context->transmit_busy = false;
        return false;
    }
    return true;
}

/*
 * 函数名：H_Can_IsTransmitBusy。
 * 说明：查询CAN0是否仍在发送上一帧数据。
 * 输入：context 为只读CAN硬件层上下文指针。
 * 输出：发送尚未完成时返回true，否则返回false。
 */
bool H_Can_IsTransmitBusy(const H_Can_Context *context)
{
    return ((context != NULL) && context->transmit_busy);
}

/*
 * 函数名：H_Can_HasFault。
 * 说明：查询CAN0是否处于总线关闭等不可正常通讯状态。
 * 输入：context为只读CAN硬件层上下文。
 * 输出：存在硬件通讯故障时返回true，否则返回false。
 */
bool H_Can_HasFault(const H_Can_Context *context)
{
    return ((context == NULL) || !context->can_open || context->bus_off);
}

/*
 * 函数名：can_callback。
 * 说明：处理CAN0接收完成、发送完成、总线关闭、恢复和邮箱丢帧事件。
 * 输入：p_args 为FSP CAN回调参数，其中p_context指向H_Can_Context。
 * 输出：无；接收帧、发送状态和错误计数写入硬件层上下文。
 */
void can_callback(can_callback_args_t *p_args)
{
    H_Can_Context *context; // 当前作用域变量，用于保存模块上下文指针。

    if ((p_args == NULL) || (p_args->p_context == NULL))
    {
        return;
    }
    context = (H_Can_Context *) p_args->p_context;

    if ((p_args->event & CAN_EVENT_RX_COMPLETE) != 0U)
    {
        uint8_t head = context->receive_head; // 当前作用域变量，用于保存队列头位置。
        uint8_t next = H_Can_NextQueueIndex(head); // 当前作用域变量，用于保存当前处理数据。

        if ((p_args->frame.id_mode != CAN_ID_MODE_EXTENDED) ||
            (p_args->frame.type != CAN_FRAME_TYPE_DATA) ||
            (p_args->frame.data_length_code != H_CAN_FRAME_DATA_LENGTH) ||
            (next == context->receive_tail))
        {
            context->dropped_frames++;
        }
        else
        {
            context->receive_queue[head].id = p_args->frame.id;
            (void) memcpy(context->receive_queue[head].data,
                          p_args->frame.data,
                          H_CAN_FRAME_DATA_LENGTH);
            context->receive_head = next;
            // 帧内容全部复制完成后才推进head，主循环不会读到尚未写完的中断数据。
        }
    }
    if ((p_args->event & CAN_EVENT_TX_COMPLETE) != 0U)
    {
        context->transmit_busy = false;
    }
    if ((p_args->event & CAN_EVENT_ERR_BUS_OFF) != 0U)
    {
        context->bus_off = true;
        context->transmit_busy = false;
    }
    if ((p_args->event & CAN_EVENT_BUS_RECOVERY) != 0U)
    {
        context->bus_off = false;
    }
    if ((p_args->event & CAN_EVENT_MAILBOX_MESSAGE_LOST) != 0U)
    {
        context->dropped_frames++;
    }
}
