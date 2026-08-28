#ifndef A_STORAGE_H
#define A_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "F_At24c256.h"

// EEPROM 存储功能层上下文，聚合 AT24C256 驱动状态并为应用层提供稳定接口。
typedef struct
{
    F_At24c256_Context device; // 当前实例使用的 AT24C256 驱动上下文。
    bool ready;              // EEPROM 是否已经应答并允许执行存储操作。
} A_Storage_Context;

/*
 * 函数名：A_Storage_Init。
 * 说明：使用板级 AT24C256 引脚和默认七位地址初始化 EEPROM 存储功能。
 * 输入：context 为存储功能上下文输入输出指针。
 * 输出：EEPROM 初始化并返回应答时返回 true，否则返回 false。
 */
bool A_Storage_Init(A_Storage_Context *context);

/*
 * 函数名：A_Storage_Read。
 * 说明：从 EEPROM 指定地址读取一段原始数据，供后续参数和日志模块使用。
 * 输入：context 为存储上下文；address 为起始地址；buffer 为输出缓冲区；length 为读取长度。
 * 输出：读取成功时返回 true，否则返回 false。
 */
bool A_Storage_Read(A_Storage_Context *context,
                         uint16_t address,
                         uint8_t *buffer,
                         size_t length);

/*
 * 函数名：A_Storage_Write。
 * 说明：向 EEPROM 指定地址写入一段原始数据，并由底层自动处理页边界和写周期。
 * 输入：context 为存储上下文；address 为起始地址；data 为只读数据；length 为写入长度。
 * 输出：写入成功时返回 true，否则返回 false。
 */
bool A_Storage_Write(A_Storage_Context *context,
                          uint16_t address,
                          const uint8_t *data,
                          size_t length);

/*
 * 函数名：A_Storage_EraseRange。
 * 说明：把EEPROM指定地址范围填充为0xFF，供日志物理清除等应用层维护操作使用。
 * 输入：context为存储上下文；address为起始地址；length为需要清除的字节数。
 * 输出：指定范围全部成功写为0xFF时返回true，否则返回false。
 */
bool A_Storage_EraseRange(A_Storage_Context *context,
                          uint16_t address,
                          size_t length);

#endif
