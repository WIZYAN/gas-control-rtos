/*
 * Version: v1.14
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现SCI0外部RS485方向控制、匹配电阻和异步收发。
 */

// 本文件实现 SCI0、RS485方向和匹配电阻的硬件层封装。
#include "H_Modbus.h"

#include <stddef.h>
#include <string.h>

#include "hal_data.h"

/*
 * 函数名：H_Modbus_SetReceiveMode。
 * 说明：将外部 RS485 收发器切换到接收状态。
 * 输入：无。
 * 输出：方向 GPIO 成功切换到接收电平时返回 true，否则返回 false。
 */
static bool H_Modbus_SetReceiveMode(void)
{
    //写入GPIO引脚电平
    return (R_IOPORT_PinWrite(&g_ioport_ctrl, SCI0_EN, BSP_IO_LEVEL_LOW) == FSP_SUCCESS);
}

/*
 * 函数名：H_Modbus_SetTransmitMode。
 * 说明：将外部 RS485 收发器切换到发送状态。
 * 输入：无。
 * 输出：方向 GPIO 成功切换到发送电平时返回 true，否则返回 false。
 */
static bool H_Modbus_SetTransmitMode(void)
{
    //写入GPIO引脚电平
    return (R_IOPORT_PinWrite(&g_ioport_ctrl, SCI0_EN, BSP_IO_LEVEL_HIGH) == FSP_SUCCESS);
}

/*
 * 函数名：H_Modbus_ResetReceiveState。
 * 说明：清空当前外部 Modbus 接收进度并准备接收下一帧。
 * 输入：context 为硬件层上下文输入输出指针。
 * 输出：无；接收长度、预期长度和完整帧标志在 context 中被复位。
 */
static void H_Modbus_ResetReceiveState(H_Modbus_Context *context)
{
    context->receive_length = 0U;
    context->expected_length = 0U;
    context->frame_ready = false;
}

/*
 * 函数名：H_Modbus_UpdateExpectedLength。
 * 说明：根据已经接收的功能码和字节计数字段推导 Modbus RTU 请求的完整长度。
 * 输入：context 为硬件层上下文输入输出指针。
 * 输出：无；推导结果写入 context->expected_length。
 */
static void H_Modbus_UpdateExpectedLength(H_Modbus_Context *context)
{
    uint8_t function_code; // 当前作用域变量，用于保存当前处理数据。

    if (context->receive_length < 2U)
    {
        return;
    }

    function_code = context->receive_buffer[1];
    if ((function_code == 0x03U) || (function_code == 0x04U) || (function_code == 0x06U))
    {
        context->expected_length = 8U; // 03、04 和 06 请求帧均固定为 8 字节。
    }
    else if ((function_code == 0x10U) && (context->receive_length >= 7U))
    {
        uint16_t expected = (uint16_t) (9U + context->receive_buffer[6]); // 当前作用域变量，用于保存当前处理数据。

        if (expected <= H_MODBUS_FRAME_MAX_LENGTH)
        {
            context->expected_length = expected; // 10 请求帧长度为固定字段 9 字节加数据字节数。
        }
        else
        {
            context->uart_error = true;
            //清空当前外部 Modbus 接收进度并准备接收下一帧
            H_Modbus_ResetReceiveState(context);
        }
    }
    else if ((function_code != 0x10U) && (context->receive_length >= 2U))
    {
        context->expected_length = 8U; // 未支持功能码仍按标准短请求接收，以便功能层返回异常码。
    }
}

/*
 * 函数名：H_Modbus_Init。
 * 说明：初始化外部 Modbus 使用的 SCI0、RS485 方向控制和匹配电阻控制。
 * 输入：context 为待初始化的硬件层上下文输入输出指针。
 * 输出：初始化成功时返回 true，否则返回 false。
 */
bool H_Modbus_Init(H_Modbus_Context *context)
{
    fsp_err_t result; // 当前作用域变量，用于保存操作结果。

    if (context == NULL)
    {
        return false;
    }

    //初始化或清空内存数据
    memset(context, 0, sizeof(*context));
    //将外部 RS485 收发器切换到接收状态
    if (!H_Modbus_SetReceiveMode())
    {
        context->uart_error = true;
        return false;
    }
    //写入GPIO引脚电平
    if (R_IOPORT_PinWrite(&g_ioport_ctrl, SCI0_485RES, BSP_IO_LEVEL_HIGH) != FSP_SUCCESS)
    {
        context->uart_error = true;
        return false;
    }
    // 高电平使能外部总线匹配电阻，引脚写入失败时不允许继续打开串口。

    result = R_SCI_UART_Open(&rs485_out_ctrl, &rs485_out_cfg);
    if (result == FSP_ERR_ALREADY_OPEN)
    {
        result = FSP_SUCCESS;
    }
    if (result != FSP_SUCCESS)
    {
        context->uart_error = true;
        return false;
    }
    context->uart_open = true;

    //注册SCI串口回调
    result = R_SCI_UART_CallbackSet(&rs485_out_ctrl, rs485_out_callback, context, NULL);
    if (result != FSP_SUCCESS)
    {
        context->uart_error = true;
        //关闭SCI串口
        result = R_SCI_UART_Close(&rs485_out_ctrl);
        if ((result == FSP_SUCCESS) || (result == FSP_ERR_NOT_OPEN))
        {
            context->uart_open = false;
        }
        return false;
    }

    //清空当前外部 Modbus 接收进度并准备接收下一帧
    H_Modbus_ResetReceiveState(context);
    return true;
}

/*
 * 函数名：H_Modbus_Deinit。
 * 说明：关闭SCI0并把外部RS485收发器恢复为接收状态，供通讯模式切换使用。
 * 输入：context为外部Modbus硬件层上下文输入输出指针。
 * 输出：SCI0已经关闭或成功关闭时返回true，否则返回false。
 */
bool H_Modbus_Deinit(H_Modbus_Context *context)
{
    fsp_err_t result; // 当前作用域变量，用于保存操作结果。
    bool receive_mode_set; // RS485接收方向恢复标志；使用范围：当前反初始化函数内；false表示方向GPIO写入失败，true表示已切回接收。

    if (context == NULL)
    {
        return false;
    }
    //将外部 RS485 收发器切换到接收状态
    receive_mode_set = H_Modbus_SetReceiveMode();
    if (!receive_mode_set)
    {
        context->uart_error = true;
    }
    if (!context->uart_open)
    {
        return receive_mode_set;
    }
    //关闭SCI串口
    result = R_SCI_UART_Close(&rs485_out_ctrl);
    if ((result != FSP_SUCCESS) && (result != FSP_ERR_NOT_OPEN))
    {
        return false;
    }
    //初始化或清空内存数据
    (void) memset(context, 0, sizeof(*context));
    context->uart_error = !receive_mode_set;
    return receive_mode_set;
}

/*
 * 函数名：H_Modbus_TakeFrame。
 * 说明：从硬件层取出一帧已经完整接收的 Modbus RTU 请求。
 * 输入：context 为硬件层上下文；buffer 为调用者接收缓存；capacity 为接收缓存容量；length 为实际长度输出指针。
 * 输出：成功取到完整帧时返回 true，否则返回 false；帧数据和长度分别通过 buffer、length 输出。
 */
bool H_Modbus_TakeFrame(H_Modbus_Context *context,
                      uint8_t *buffer,
                      uint16_t capacity,
                      uint16_t *length)
{
    uint16_t frame_length; // 当前作用域变量，用于保存有效数据长度。

    if ((context == NULL) || (buffer == NULL) || (length == NULL) || !context->frame_ready)
    {
        return false;
    }

    frame_length = context->receive_length;
    if ((frame_length == 0U) || (frame_length > capacity))
    {
        //清空当前外部 Modbus 接收进度并准备接收下一帧
        H_Modbus_ResetReceiveState(context);
        return false;
    }

    //复制内存数据
    memcpy(buffer, context->receive_buffer, frame_length);
    *length = frame_length;
    H_Modbus_ResetReceiveState(context); // 完整帧复制完成后才允许接收下一帧。
    return true;
}

/*
 * 函数名：H_Modbus_Send。
 * 说明：通过 SCI0 和外部 RS485 接口异步发送一帧 Modbus RTU 响应。
 * 输入：context 为硬件层上下文；data 为待发送数据；length 为待发送字节数。
 * 输出：成功启动发送时返回 true，否则返回 false。
 */
bool H_Modbus_Send(H_Modbus_Context *context, const uint8_t *data, uint16_t length)
{
    fsp_err_t result; // 当前作用域变量，用于保存操作结果。

    if ((context == NULL) || (data == NULL) || (length == 0U) ||
        (length > H_MODBUS_FRAME_MAX_LENGTH) || !context->uart_open || context->transmit_busy)
    {
        return false;
    }

    //复制内存数据
    memcpy(context->transmit_buffer, data, length);
    //将外部 RS485 收发器切换到发送状态
    if (!H_Modbus_SetTransmitMode())
    {
        context->uart_error = true;
        return false;
    }
    context->transmit_busy = true;
    //启动SCI异步发送
    result = R_SCI_UART_Write(&rs485_out_ctrl, context->transmit_buffer, length);
    if (result != FSP_SUCCESS)
    {
        context->transmit_busy = false;
        context->uart_error = true;
        //将外部 RS485 收发器切换到接收状态
        if (!H_Modbus_SetReceiveMode())
        {
            context->uart_error = true;
        }
        return false;
    }

    return true;
}

/*
 * 函数名：H_Modbus_IsTransmitBusy。
 * 说明：查询外部 Modbus 硬件层是否仍在发送响应。
 * 输入：context 为只读硬件层上下文指针。
 * 输出：正在发送时返回 true，否则返回 false。
 */
bool H_Modbus_IsTransmitBusy(const H_Modbus_Context *context)
{
    return ((context != NULL) && context->transmit_busy);
}

/*
 * 函数名：H_Modbus_HasFault。
 * 说明：查询SCI0外部Modbus硬件层是否存在串口故障。
 * 输入：context为只读硬件层上下文。
 * 输出：接口未打开或发生串口错误时返回true，否则返回false。
 */
bool H_Modbus_HasFault(const H_Modbus_Context *context)
{
    return ((context == NULL) || !context->uart_open || context->uart_error);
}

/*
 * 函数名：rs485_out_callback。
 * 说明：处理 SCI0 的逐字节接收、发送完成及通信错误事件。
 * 输入：p_args 为 FSP 串口回调参数，其中 p_context 指向 H_Modbus_Context。
 * 输出：无；接收帧、发送状态和错误状态写入硬件层上下文。
 */
void rs485_out_callback(uart_callback_args_t *p_args)
{
    H_Modbus_Context *context; // 当前作用域变量，用于保存模块上下文指针。

    if ((p_args == NULL) || (p_args->p_context == NULL))
    {
        return;
    }

    context = (H_Modbus_Context *) p_args->p_context;
    if (p_args->event == UART_EVENT_RX_CHAR)
    {
        if (!context->frame_ready && (context->receive_length < H_MODBUS_FRAME_MAX_LENGTH))
        {
            context->receive_buffer[context->receive_length] = (uint8_t) p_args->data;
            context->receive_length++;
            //根据已经接收的功能码和字节计数字段推导 Modbus RTU 请求的完整长度
            H_Modbus_UpdateExpectedLength(context);
            if ((context->expected_length != 0U) &&
                (context->receive_length >= context->expected_length))
            {
                context->frame_ready = true; // 达到推导长度后冻结本帧，等待功能层取走。
            }
        }
        else if (!context->frame_ready)
        {
            context->uart_error = true;
            //清空当前外部 Modbus 接收进度并准备接收下一帧
            H_Modbus_ResetReceiveState(context);
        }
    }
    else if (p_args->event == UART_EVENT_TX_COMPLETE)
    {
        //将外部 RS485 收发器切换到接收状态
        if (!H_Modbus_SetReceiveMode())
        {
            context->uart_error = true;
        }
        context->transmit_busy = false;
        // 最后一个停止位发送完成后再切回接收，避免响应尾字节被截断。
    }
    else if ((p_args->event & (UART_EVENT_ERR_PARITY |
                               UART_EVENT_ERR_FRAMING |
                               UART_EVENT_ERR_OVERFLOW |
                               UART_EVENT_BREAK_DETECT)) != 0U)
    {
        context->uart_error = true;
        //清空当前外部 Modbus 接收进度并准备接收下一帧
        H_Modbus_ResetReceiveState(context);
        context->transmit_busy = false;
        //将外部 RS485 收发器切换到接收状态
        if (!H_Modbus_SetReceiveMode())
        {
            context->uart_error = true;
        }
    }
    else
    {
        // 其他事件不改变 Modbus 帧处理状态。
    }
}
