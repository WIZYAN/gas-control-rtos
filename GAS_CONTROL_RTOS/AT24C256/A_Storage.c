#include "A_Storage.h"

#include <string.h>

#include "hal_data.h"

/*
 * 函数名：A_Storage_Init。
 * 说明：使用板级 AT24C256 引脚和默认七位地址初始化 EEPROM 存储功能。
 * 输入：context 为存储功能上下文输入输出指针。
 * 输出：EEPROM 初始化并返回应答时返回 true，否则返回 false。
 */
bool A_Storage_Init(A_Storage_Context *context)
{
    if (context == NULL)
    {
        return false;
    }

    (void) memset(context, 0, sizeof(*context));
    context->ready = F_At24c256_Init(&context->device,
                                   AT24C_SCL,
                                   AT24C_SDA,
                                   AT24C_WP,
                                   AT24C256_DEFAULT_ADDRESS_7BIT);
    return context->ready;
}

/*
 * 函数名：A_Storage_Read。
 * 说明：从 EEPROM 指定地址读取一段原始数据，供后续参数和日志模块使用。
 * 输入：context 为存储上下文；address 为起始地址；buffer 为输出缓冲区；length 为读取长度。
 * 输出：读取成功时返回 true，否则返回 false。
 */
bool A_Storage_Read(A_Storage_Context *context,
                         uint16_t address,
                         uint8_t *buffer,
                         size_t length)
{
    return ((context != NULL) && context->ready &&
            F_At24c256_Read(&context->device, address, buffer, length));
}

/*
 * 函数名：A_Storage_Write。
 * 说明：向 EEPROM 指定地址写入一段原始数据，并由底层自动处理页边界和写周期。
 * 输入：context 为存储上下文；address 为起始地址；data 为只读数据；length 为写入长度。
 * 输出：写入成功时返回 true，否则返回 false。
 */
bool A_Storage_Write(A_Storage_Context *context,
                          uint16_t address,
                          const uint8_t *data,
                          size_t length)
{
    return ((context != NULL) && context->ready &&
            F_At24c256_Write(&context->device, address, data, length));
}
