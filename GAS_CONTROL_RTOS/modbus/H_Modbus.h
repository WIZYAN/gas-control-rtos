// 本文件声明 SCI0/RS485 外部 Modbus 硬件层接口。
#ifndef H_MODBUS_H
#define H_MODBUS_H

#include <stdbool.h>
#include <stdint.h>

#define H_MODBUS_FRAME_MAX_LENGTH (128U) // 外部 Modbus RTU 单帧收发缓存的最大字节数。

// 外部 Modbus 硬件层上下文，保存 SCI0 收发过程中的全部状态，模块不使用全局变量。
typedef struct
{
    uint8_t receive_buffer[H_MODBUS_FRAME_MAX_LENGTH]; // 接收帧缓存。
    uint8_t transmit_buffer[H_MODBUS_FRAME_MAX_LENGTH]; // 发送帧缓存，保证异步发送期间数据持续有效。
    volatile uint16_t receive_length; // 当前已经接收的字节数。
    volatile uint16_t expected_length; // 根据功能码推导出的完整请求帧长度。
    volatile bool frame_ready; // 一帧请求已经完整接收的标志。
    volatile bool transmit_busy; // SCI0 正在发送响应的标志。
    volatile bool uart_error; // 串口发生通信错误的标志。
    bool uart_open; // SCI0 已经成功打开的标志。
} H_Modbus_Context;

/*
 * 函数名：H_Modbus_Init。
 * 说明：初始化外部 Modbus 使用的 SCI0、RS485 方向控制和匹配电阻控制。
 * 输入：context 为待初始化的硬件层上下文输入输出指针。
 * 输出：初始化成功时返回 true，否则返回 false。
 */
bool H_Modbus_Init(H_Modbus_Context *context);

/*
 * 函数名：H_Modbus_Deinit。
 * 说明：关闭SCI0并把外部RS485收发器恢复为接收状态，供通讯模式切换使用。
 * 输入：context为外部Modbus硬件层上下文输入输出指针。
 * 输出：SCI0已经关闭或成功关闭时返回true，否则返回false。
 */
bool H_Modbus_Deinit(H_Modbus_Context *context);

/*
 * 函数名：H_Modbus_TakeFrame。
 * 说明：从硬件层取出一帧已经完整接收的 Modbus RTU 请求。
 * 输入：context 为硬件层上下文；buffer 为调用者接收缓存；capacity 为接收缓存容量；length 为实际长度输出指针。
 * 输出：成功取到完整帧时返回 true，否则返回 false；帧数据和长度分别通过 buffer、length 输出。
 */
bool H_Modbus_TakeFrame(H_Modbus_Context *context,
                      uint8_t *buffer,
                      uint16_t capacity,
                      uint16_t *length);

/*
 * 函数名：H_Modbus_Send。
 * 说明：通过 SCI0 和外部 RS485 接口异步发送一帧 Modbus RTU 响应。
 * 输入：context 为硬件层上下文；data 为待发送数据；length 为待发送字节数。
 * 输出：成功启动发送时返回 true，否则返回 false。
 */
bool H_Modbus_Send(H_Modbus_Context *context, const uint8_t *data, uint16_t length);

/*
 * 函数名：H_Modbus_IsTransmitBusy。
 * 说明：查询外部 Modbus 硬件层是否仍在发送响应。
 * 输入：context 为只读硬件层上下文指针。
 * 输出：正在发送时返回 true，否则返回 false。
 */
bool H_Modbus_IsTransmitBusy(const H_Modbus_Context *context);

/*
 * 函数名：H_Modbus_HasFault。
 * 说明：查询SCI0外部Modbus硬件层是否存在串口故障。
 * 输入：context为只读硬件层上下文。
 * 输出：接口未打开或发生串口错误时返回true，否则返回false。
 */
bool H_Modbus_HasFault(const H_Modbus_Context *context);

#endif
