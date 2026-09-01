/*
 * Version: v1.12
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现SCI9串口屏硬件初始化、异步收发和回调处理。
 */

#include "H_Hmi.h"

#include <limits.h>
#include <string.h>

#include "hal_data.h"

/*
 * 函数名：H_Hmi_Init。
 * 说明：按照电源、使能和通信顺序启动串口屏，打开 SCI9 dis_uart 实例并绑定回调上下文。
 * 输入：context 为串口屏硬件层上下文输入输出指针。
 * 输出：电源与使能GPIO写入、SCI9打开和回调绑定全部成功时返回 true，否则返回 false。
 */
bool H_Hmi_Init(H_Hmi_Context *context)
{
    if (context == NULL)
    {
        return false;
    }

    (void) memset(context, 0, sizeof(*context));
    if (R_IOPORT_PinWrite(&g_ioport_ctrl, DISP_POWER, BSP_IO_LEVEL_HIGH) != FSP_SUCCESS)
    {
        return false;
    }
    R_BSP_SoftwareDelay(100U, BSP_DELAY_UNITS_MILLISECONDS);
    // 先开启串口屏主电源并等待100ms，确保电源轨稳定后再使能屏幕。

    if (R_IOPORT_PinWrite(&g_ioport_ctrl, DISP_EN, BSP_IO_LEVEL_HIGH) != FSP_SUCCESS)
    {
        (void) R_IOPORT_PinWrite(&g_ioport_ctrl, DISP_POWER, BSP_IO_LEVEL_LOW);
        return false;
    }
    R_BSP_SoftwareDelay(500U, BSP_DELAY_UNITS_MILLISECONDS);
    // 屏幕使能后等待500ms完成启动，再打开SCI9并发送RTC及控件刷新命令。

    if (R_SCI_UART_Open(&dis_uart_ctrl, &dis_uart_cfg) != FSP_SUCCESS)
    {
        return false;
    }
    if (R_SCI_UART_CallbackSet(&dis_uart_ctrl, dis_uart_callback, context, NULL) != FSP_SUCCESS)
    {
        (void) R_SCI_UART_Close(&dis_uart_ctrl);
        return false;
    }

    context->ready = true;
    return true;
}

/*
 * 函数名：H_Hmi_ReadByte。
 * 说明：从 SCI9 接收环形缓冲区取出一个字节。
 * 输入：context 为串口屏硬件层上下文；value 为字节输出指针。
 * 输出：成功取得一个字节时返回 true，缓冲区为空或参数无效时返回 false。
 */
bool H_Hmi_ReadByte(H_Hmi_Context *context, uint8_t *value)
{
    if ((context == NULL) || (value == NULL) || (context->rx_tail == context->rx_head))
    {
        return false;
    }

    *value = context->rx_buffer[context->rx_tail];
    context->rx_tail = (uint16_t) ((context->rx_tail + 1U) % H_HMI_RX_BUFFER_SIZE);
    return true;
}

/*
 * 函数名：H_Hmi_Write。
 * 说明：通过 SCI9 异步发送一帧大彩串口屏数据。
 * 输入：context 为硬件层上下文；data 为发送缓冲区；length 为发送字节数。
 * 输出：成功启动发送时返回 true，否则返回 false。
 */
bool H_Hmi_Write(H_Hmi_Context *context, const uint8_t *data, size_t length)
{
    if ((context == NULL) || !context->ready || context->tx_busy ||
        (data == NULL) || (length == 0U) || (length > UINT32_MAX))
    {
        return false;
    }

    context->tx_busy = true;
    if (R_SCI_UART_Write(&dis_uart_ctrl, data, (uint32_t) length) != FSP_SUCCESS)
    {
        context->tx_busy = false;
        context->uart_error = true;
        return false;
    }
    return true;
}

/*
 * 函数名：H_Hmi_IsTxBusy。
 * 说明：查询 SCI9 是否仍在异步发送上一帧数据。
 * 输入：context 为只读硬件层上下文指针。
 * 输出：正在发送时返回 true，否则返回 false。
 */
bool H_Hmi_IsTxBusy(const H_Hmi_Context *context)
{
    return ((context != NULL) && context->tx_busy);
}

/*
 * 函数名：dis_uart_callback。
 * 说明：接收 SCI9 单字节事件并维护串口屏异步发送和错误状态。
 * 输入：p_args 为 FSP 串口回调参数，p_context 指向 H_Hmi_Context。
 * 输出：无；通过上下文输出接收字节和通信状态。
 */
void dis_uart_callback(uart_callback_args_t *p_args)
{
    H_Hmi_Context *context; // 当前作用域变量，用于保存模块上下文指针。

    if ((p_args == NULL) || (p_args->p_context == NULL))
    {
        return;
    }
    context = (H_Hmi_Context *) p_args->p_context;

    if (p_args->event == UART_EVENT_RX_CHAR)
    {
        uint16_t next_head = (uint16_t) ((context->rx_head + 1U) % H_HMI_RX_BUFFER_SIZE); // 当前作用域变量，用于保存队列头位置。

        if (next_head != context->rx_tail)
        {
            context->rx_buffer[context->rx_head] = (uint8_t) p_args->data;
            context->rx_head = next_head;
        }
        else
        {
            context->uart_error = true; // 环形缓冲区溢出时丢弃新字节并记录通信异常。
        }
    }
    else if (p_args->event == UART_EVENT_TX_COMPLETE)
    {
        context->tx_busy = false;
    }
    else if ((p_args->event == UART_EVENT_ERR_PARITY) ||
             (p_args->event == UART_EVENT_ERR_FRAMING) ||
             (p_args->event == UART_EVENT_ERR_OVERFLOW) ||
             (p_args->event == UART_EVENT_BREAK_DETECT))
    {
        context->uart_error = true;
    }
}
