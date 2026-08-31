/*
 * Version: v1.11
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明SCI9串口屏硬件层上下文和异步收发接口。
 */

#ifndef H_HMI_H
#define H_HMI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define H_HMI_RX_BUFFER_SIZE (128U) // SCI9 中断接收环形缓冲区容量。

// 串口屏硬件层上下文，集中保存 SCI9 的异步收发状态和接收环形缓冲区。
typedef struct
{
    volatile uint8_t rx_buffer[H_HMI_RX_BUFFER_SIZE]; // SCI9 回调写入的字节环形缓冲区。
    volatile uint16_t rx_head;                        // 回调下一次写入位置。
    volatile uint16_t rx_tail;                        // 任务下一次读取位置，同时供串口中断判断缓冲区是否已满。
    volatile bool tx_busy;                            // HMI发送状态标志；使用范围：SCI9中断与HMI任务之间；取值范围：false/true，false表示发送空闲，true表示正在异步发送。
    volatile bool uart_error;                         // HMI串口错误标志；使用范围：SCI9中断与HMI任务之间；取值范围：false/true，false表示通信正常，true表示最近一次通信发生错误。
    bool ready; // SCI9 是否已经成功打开并绑定上下文；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。
} H_Hmi_Context;

/*
 * 函数名：H_Hmi_Init。
 * 说明：打开 FSP 的 SCI9 dis_uart 实例并绑定串口屏回调上下文。
 * 输入：context 为串口屏硬件层上下文输入输出指针。
 * 输出：初始化成功时返回 true，否则返回 false。
 */
bool H_Hmi_Init(H_Hmi_Context *context);

/*
 * 函数名：H_Hmi_ReadByte。
 * 说明：从 SCI9 接收环形缓冲区取出一个字节。
 * 输入：context 为串口屏硬件层上下文；value 为字节输出指针。
 * 输出：成功取得一个字节时返回 true，缓冲区为空或参数无效时返回 false。
 */
bool H_Hmi_ReadByte(H_Hmi_Context *context, uint8_t *value);

/*
 * 函数名：H_Hmi_Write。
 * 说明：通过 SCI9 异步发送一帧大彩串口屏数据。
 * 输入：context 为硬件层上下文；data 为发送缓冲区；length 为发送字节数。
 * 输出：成功启动发送时返回 true，否则返回 false。
 */
bool H_Hmi_Write(H_Hmi_Context *context, const uint8_t *data, size_t length);

/*
 * 函数名：H_Hmi_IsTxBusy。
 * 说明：查询 SCI9 是否仍在异步发送上一帧数据。
 * 输入：context 为只读硬件层上下文指针。
 * 输出：正在发送时返回 true，否则返回 false。
 */
bool H_Hmi_IsTxBusy(const H_Hmi_Context *context);

#endif
