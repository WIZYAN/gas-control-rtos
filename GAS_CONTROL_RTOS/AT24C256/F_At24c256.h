/*
 * Version: v1.11
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明AT24C256功能层上下文及非阻塞读写接口。
 */

#ifndef F_AT24C256_H
#define F_AT24C256_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "H_Soft_Iic.h"

#define AT24C256_CAPACITY_BYTES       (32768UL) // AT24C256 可寻址的总字节容量。
#define AT24C256_PAGE_SIZE_BYTES      (64U)     // AT24C256 单页写入的字节数。
#define AT24C256_DEFAULT_ADDRESS_7BIT (0x50U)   // A0～A2 接地时的七位 IIC 器件地址。
#define AT24C256_IIC_HALF_PERIOD_US   (5U)      // 软件 IIC 半周期，5 μs 对应约 100 kHz。

// AT24C256 驱动结果，用于区分参数、总线、应答、越界和校验错误。
typedef enum
{
    AT24C256_RESULT_OK = 0,       // 操作成功。
    AT24C256_RESULT_PARAMETER,    // 输入参数无效。
    AT24C256_RESULT_NOT_READY,    // 驱动尚未初始化或器件未就绪。
    AT24C256_RESULT_BUS,          // 软件 IIC 总线操作失败。
    AT24C256_RESULT_NACK,         // EEPROM 未返回应答。
    AT24C256_RESULT_RANGE,        // 访问地址或长度超出 32 KB 范围。
    AT24C256_RESULT_VERIFY        // 自检写入后的读回数据不一致。
} F_At24c256_Result;

// AT24C256 硬件驱动上下文，保存软件 IIC、器件地址、写保护引脚和最近结果。
typedef struct
{
    H_Soft_Iic_Context iic;          // 当前 EEPROM 使用的软件 IIC 总线实例。
    uint32_t write_protect_pin;    // WP 对应的 FSP GPIO 编码，高电平禁止写入。
    uint8_t device_address_7bit;   // A2～A0 形成的七位 IIC 器件地址。
    F_At24c256_Result last_result;    // 最近一次读写或自检操作结果。
    bool initialized; // 器件是否已经完成总线初始化和应答探测；使用范围：当前声明作用域内使用；取值范围：false/true，false表示尚未初始化，true表示已经初始化。
} F_At24c256_Context;

/*
 * 函数名：F_At24c256_Init。
 * 说明：初始化软件 IIC、写保护 GPIO 和 AT24C256 七位器件地址，并探测器件应答。
 * 输入：context 为驱动上下文；scl_pin、sda_pin、write_protect_pin 为 GPIO 编码；device_address_7bit 为七位地址。
 * 输出：器件应答正常时返回 true，否则返回 false，并在 context 中保存错误原因。
 */
bool F_At24c256_Init(F_At24c256_Context *context,
                   uint32_t scl_pin,
                   uint32_t sda_pin,
                   uint32_t write_protect_pin,
                   uint8_t device_address_7bit);

/*
 * 函数名：F_At24c256_Read。
 * 说明：从指定 16 位 EEPROM 地址开始连续读取数据。
 * 输入：context 为驱动上下文；address 为起始地址；buffer 为输出缓冲区；length 为读取字节数。
 * 输出：读取成功时返回 true，否则返回 false，并更新最近结果。
 */
bool F_At24c256_Read(F_At24c256_Context *context,
                   uint16_t address,
                   uint8_t *buffer,
                   size_t length);

/*
 * 函数名：F_At24c256_Write。
 * 说明：按照 64 字节页边界自动拆分数据，并通过应答轮询完成连续写入。
 * 输入：context 为驱动上下文；address 为起始地址；data 为只读数据；length 为写入字节数。
 * 输出：全部页写入成功时返回 true，否则返回 false，并更新最近结果。
 */
bool F_At24c256_Write(F_At24c256_Context *context,
                    uint16_t address,
                    const uint8_t *data,
                    size_t length);

/*
 * 函数名：F_At24c256_EraseRange。
 * 说明：把指定地址范围按页写方式填充为 0xFF，不自动擦除范围之外的数据。
 * 输入：context 为驱动上下文；address 为起始地址；length 为需要填充的字节数。
 * 输出：指定范围全部写入 0xFF 时返回 true，否则返回 false。
 */
bool F_At24c256_EraseRange(F_At24c256_Context *context, uint16_t address, size_t length);

/*
 * 函数名：F_At24c256_SelfTest。
 * 说明：备份指定两个字节、写入测试图样、读回校验并恢复原数据，用于非破坏性器件自检。
 * 输入：context 为驱动上下文；test_address 为至少可连续访问两个字节的保留地址。
 * 输出：测试图样读回且原数据恢复成功时返回 true，否则返回 false。
 */
bool F_At24c256_SelfTest(F_At24c256_Context *context, uint16_t test_address);

#endif
