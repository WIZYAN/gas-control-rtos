/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明外部Modbus RTU从站功能层上下文和寄存器接口。
 */

// 本文件声明 SCI0/RS485 外部 Modbus RTU 从站功能层接口。
#ifndef F_MODBUS_H
#define F_MODBUS_H

#include <stdbool.h>
#include <stdint.h>

#include "H_Modbus.h"

#define F_MODBUS_INPUT_REGISTER_COUNT   (38U)     // 外部 Modbus 输入寄存器表数量，覆盖 0x0000～0x0025。
#define F_MODBUS_HOLDING_REGISTER_COUNT (38U)     // 外部Modbus保持寄存器表数量，覆盖0x0100～0x0125。
#define F_MODBUS_HOLDING_BASE_ADDRESS   (0x0100U) // 保持寄存器表的 PDU 起始地址。

// 外部 Modbus 功能层上下文，保存寄存器表、从站地址以及最近一次写寄存器事件。
typedef struct
{
    H_Modbus_Context *hardware; // 下层 SCI0 Modbus 硬件实例指针。
    uint16_t input_register[F_MODBUS_INPUT_REGISTER_COUNT]; // 功能码 04 只读输入寄存器表。
    uint16_t holding_register[F_MODBUS_HOLDING_REGISTER_COUNT]; // 功能码 03/06/10 保持寄存器表。
    uint16_t write_start_offset; // 最近一次主站写入的保持寄存器起始偏移。
    uint16_t write_register_count; // 最近一次主站写入的保持寄存器数量。
    uint8_t slave_address; // 外部 Modbus 从站地址。
    bool write_pending; // 存在尚未由应用层处理的写寄存器事件；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
} F_Modbus_Context;

/*
 * 函数名：F_Modbus_Init。
 * 说明：初始化外部 Modbus 硬件层、从站功能层及输入、保持寄存器表。
 * 输入：context 为待初始化的功能层上下文；hardware 为待初始化的硬件层实例；slave_address 为从站地址。
 * 输出：参数有效且硬件层初始化成功时返回 true，否则返回 false。
 */
bool F_Modbus_Init(F_Modbus_Context *context,
                 H_Modbus_Context *hardware,
                 uint8_t slave_address);

/*
 * 函数名：F_Modbus_Deinit。
 * 说明：通过硬件层关闭SCI0外部Modbus接口并清除功能层状态。
 * 输入：context为功能层上下文输入输出指针。
 * 输出：接口已经关闭或成功关闭时返回true，否则返回false。
 */
bool F_Modbus_Deinit(F_Modbus_Context *context);

/*
 * 函数名：F_Modbus_HasFault。
 * 说明：逐层查询SCI0外部Modbus是否存在硬件通讯故障。
 * 输入：context为只读功能层上下文。
 * 输出：功能层无硬件实例或硬件故障时返回true，否则返回false。
 */
bool F_Modbus_HasFault(const F_Modbus_Context *context);

/*
 * 函数名：F_Modbus_Task。
 * 说明：处理一帧外部 Modbus RTU 请求，支持功能码 03、04、06 和 10。
 * 输入：context 为功能层上下文输入输出指针。
 * 输出：无；响应通过硬件层发送，写寄存器事件记录在 context 中。
 */
void F_Modbus_Task(F_Modbus_Context *context);

/*
 * 函数名：F_Modbus_SetInputRegister。
 * 说明：更新一个供功能码 04 读取的输入寄存器。
 * 输入：context 为功能层上下文；offset 为输入寄存器偏移；value 为待写入数值。
 * 输出：偏移有效并成功更新时返回 true，否则返回 false。
 */
bool F_Modbus_SetInputRegister(F_Modbus_Context *context, uint16_t offset, uint16_t value);

/*
 * 函数名：F_Modbus_GetHoldingRegister。
 * 说明：读取一个保持寄存器的当前数值，供应用层处理主站命令。
 * 输入：context 为只读功能层上下文；offset 为保持寄存器偏移；value 为数值输出指针。
 * 输出：参数有效时返回 true，否则返回 false；寄存器数值通过 value 输出。
 */
bool F_Modbus_GetHoldingRegister(const F_Modbus_Context *context,
                               uint16_t offset,
                               uint16_t *value);

/*
 * 函数名：F_Modbus_SetHoldingRegister。
 * 说明：由本机应用层更新一个保持寄存器的当前数值。
 * 输入：context 为功能层上下文；offset 为保持寄存器偏移；value 为待写入数值。
 * 输出：参数有效并成功更新时返回 true，否则返回 false。
 */
bool F_Modbus_SetHoldingRegister(F_Modbus_Context *context, uint16_t offset, uint16_t value);

/*
 * 函数名：F_Modbus_TakeWriteEvent。
 * 说明：取出并清除最近一次由外部主站产生的保持寄存器写入事件。
 * 输入：context 为功能层上下文；start_offset 为起始偏移输出指针；register_count 为写入数量输出指针。
 * 输出：存在待处理写事件时返回 true，否则返回 false。
 */
bool F_Modbus_TakeWriteEvent(F_Modbus_Context *context,
                           uint16_t *start_offset,
                           uint16_t *register_count);

/*
 * 函数名：F_Modbus_Crc16。
 * 说明：计算 Modbus RTU 使用的 CRC16 校验值。
 * 输入：data 为待校验数据；length 为字节数。
 * 输出：返回 CRC16 数值，发送时低字节在前。
 */
uint16_t F_Modbus_Crc16(const uint8_t *data, uint16_t length);

#endif
