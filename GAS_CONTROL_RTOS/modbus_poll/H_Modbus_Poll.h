/*
 * Version: v1.12
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明SCI1内部Modbus轮询硬件层收发接口。
 */

#ifndef H_MODBUS_POLL_H
#define H_MODBUS_POLL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "H_Gas_Platform.h"

// 内部 Modbus 主站硬件层上下文，通过指针复用气源平台中的 SCI1 和 PRE_EN 控制接口。
typedef struct
{
    H_Gas_Platform_Context *platform; // 当前主站使用的气源硬件平台实例。
} H_Modbus_Poll_Context;

/*
 * 函数名：H_ModbusPoll_Init。
 * 说明：把内部 Modbus 主站硬件上下文绑定到已经初始化的气源硬件平台。
 * 输入：context 为主站硬件上下文；platform 为拥有 SCI1 和 PRE_EN 的平台上下文。
 * 输出：两个参数有效且平台串口已打开时返回 true，否则返回 false。
 */
bool H_ModbusPoll_Init(H_Modbus_Poll_Context *context,
                        H_Gas_Platform_Context *platform);

/*
 * 函数名：H_ModbusPoll_TxStart。
 * 说明：通过压力传感器 SCI1 和 PRE_EN 启动一次 Modbus 请求发送。
 * 输入：context 为主站硬件上下文；data 为请求帧；length 为请求帧长度。
 * 输出：底层异步发送成功启动时返回 true，否则返回 false。
 */
bool H_ModbusPoll_TxStart(H_Modbus_Poll_Context *context,
                           const uint8_t *data,
                           size_t length);

/*
 * 函数名：H_ModbusPoll_RxStart。
 * 说明：通过压力传感器 SCI1 启动指定长度的 Modbus 响应接收。
 * 输入：context 为主站硬件上下文；data 为响应缓冲区；length 为期望响应长度。
 * 输出：底层异步接收成功启动时返回 true，否则返回 false。
 */
bool H_ModbusPoll_RxStart(H_Modbus_Poll_Context *context,
                           uint8_t *data,
                           size_t length);

/*
 * 函数名：H_ModbusPoll_Abort。
 * 说明：中止当前 SCI1 Modbus 事务并恢复 PRE_EN 接收状态。
 * 输入：context 为主站硬件上下文。
 * 输出：无。
 */
void H_ModbusPoll_Abort(H_Modbus_Poll_Context *context);

/*
 * 函数名：H_ModbusPoll_TxDone。
 * 说明：查询当前内部 Modbus 请求是否已经发送完成。
 * 输入：context 为只读主站硬件上下文。
 * 输出：发送完成且无串口错误时返回 true，否则返回 false。
 */
bool H_ModbusPoll_TxDone(const H_Modbus_Poll_Context *context);

/*
 * 函数名：H_ModbusPoll_RxDone。
 * 说明：查询当前内部 Modbus 响应是否已经接收完成。
 * 输入：context 为只读主站硬件上下文。
 * 输出：接收完成且无串口错误时返回 true，否则返回 false。
 */
bool H_ModbusPoll_RxDone(const H_Modbus_Poll_Context *context);

#endif
