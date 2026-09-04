/*
 * Version: v1.14
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明内部Modbus主站状态、结果和非阻塞轮询接口。
 */

#ifndef F_MODBUS_POLL_H
#define F_MODBUS_POLL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "H_Gas_Platform.h"

#define MODBUS_POLL_REQUEST_SIZE       (8U)  // 功能码 03/04 读寄存器请求帧的固定字节数。
#define MODBUS_POLL_RESPONSE_MAX_SIZE  (64U) // 内部传感器响应帧缓存的最大字节数。

// 内部 Modbus 主站事务结果。
typedef enum
{
    MODBUS_POLL_RESULT_NONE = 0,   // 当前没有可读取的事务结果。
    MODBUS_POLL_RESULT_OK,         // 响应地址、功能码、长度和 CRC 均正确。
    MODBUS_POLL_RESULT_BUSY,       // 上一笔事务尚未完成。
    MODBUS_POLL_RESULT_TIMEOUT,    // 响应超过配置的截止时间。
    MODBUS_POLL_RESULT_CRC,        // 响应 CRC 校验失败。
    MODBUS_POLL_RESULT_PROTOCOL,   // 响应地址、功能码或字节数不符合请求。
    MODBUS_POLL_RESULT_IO          // 底层 SCI1 收发、方向恢复或中止操作失败。
} F_Modbus_Poll_Result;

// 内部 Modbus 主站功能层状态。
typedef enum
{
    MODBUS_POLL_STATE_IDLE = 0, // 当前没有事务。
    MODBUS_POLL_STATE_TX,       // 请求帧正在发送。
    MODBUS_POLL_STATE_RX        // 正在等待定长响应。
} F_Modbus_Poll_State;

// 内部 Modbus 主站功能层上下文，保存单笔非阻塞事务及其收发缓冲区。
typedef struct
{
    H_Gas_Platform_Context *platform;            // 当前事务直接使用的气源硬件平台，不再经过只转发调用的中间上下文。
    uint8_t request[MODBUS_POLL_REQUEST_SIZE];          // 当前请求帧缓冲区。
    uint8_t response[MODBUS_POLL_RESPONSE_MAX_SIZE];    // 当前响应帧缓冲区。
    uint8_t slave_address;                              // 当前请求的从站地址。
    uint8_t function_code;                              // 当前请求的功能码。
    uint16_t register_count;                            // 当前请求的寄存器数量。
    size_t expected_response_length;                    // 正常响应的期望总字节数。
    uint32_t deadline_ms;                               // 当前事务响应截止时间。
    F_Modbus_Poll_State state;                            // 当前事务子状态。
    F_Modbus_Poll_Result result;                          // 当前或最近一次事务结果。
    bool result_pending; // 是否存在尚未被上层取走的结果；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
} F_Modbus_Poll_Context;

/*
 * 函数名：F_ModbusPoll_Init。
 * 说明：初始化内部 Modbus 主站功能上下文并绑定 SCI1 硬件平台。
 * 输入：context 为主站功能上下文；platform 为气源硬件平台上下文。
 * 输出：硬件绑定成功时返回 true，否则返回 false。
 */
bool F_ModbusPoll_Init(F_Modbus_Poll_Context *context,
                       H_Gas_Platform_Context *platform);

/*
 * 函数名：F_ModbusPoll_StartRead。
 * 说明：构造功能码 03 或 04 的请求帧，先挂接定长接收缓冲区，再启动非阻塞 Modbus RTU 发送。
 * 输入：context 为功能上下文；slave_address 为从站地址；function_code 为功能码；start_register 为起始寄存器；register_count 为数量；now_ms 为当前时间；timeout_ms 为超时。
 * 输出：事务成功启动时返回 true，否则返回 false。
 */
bool F_ModbusPoll_StartRead(F_Modbus_Poll_Context *context,
                            uint8_t slave_address,
                            uint8_t function_code,
                            uint16_t start_register,
                            uint16_t register_count,
                            uint32_t now_ms,
                            uint32_t timeout_ms);

/*
 * 函数名：F_ModbusPoll_Task。
 * 说明：推进内部 Modbus 主站发送、接收、协议校验和超时状态机。
 * 输入：context 为功能上下文；now_ms 为当前毫秒计数。
 * 输出：无；完成后通过 context 保存事务结果。
 */
void F_ModbusPoll_Task(F_Modbus_Poll_Context *context, uint32_t now_ms);

/*
 * 函数名：F_ModbusPoll_TakeResult。
 * 说明：取走最近一次事务结果，并在成功时返回 Modbus 响应的数据字段。
 * 输入：context 为功能上下文；result、payload、payload_length 为输出参数。
 * 输出：存在待取结果时返回 true，否则返回 false。
 */
bool F_ModbusPoll_TakeResult(F_Modbus_Poll_Context *context,
                             F_Modbus_Poll_Result *result,
                             const uint8_t **payload,
                             size_t *payload_length);

/*
 * 函数名：F_ModbusPoll_Crc16。
 * 说明：按照 Modbus RTU 多项式计算指定数据的 CRC16。
 * 输入：data 为只读数据缓冲区；length 为参与计算的字节数。
 * 输出：返回 16 位 CRC，加入帧时低字节在前。
 */
uint16_t F_ModbusPoll_Crc16(const uint8_t *data, size_t length);

#endif
