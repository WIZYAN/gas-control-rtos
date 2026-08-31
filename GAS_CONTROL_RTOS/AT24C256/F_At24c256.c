/*
 * Version: v1.11
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现AT24C256页写、随机读和写完成轮询功能。
 */

#include "F_At24c256.h"

#include <string.h>

#define AT24C256_WRITE_POLL_COUNT       (20U)  // 页写完成应答轮询的最大次数。
#define AT24C256_WRITE_POLL_DELAY_US    (500U) // 相邻两次页写应答轮询的间隔，单位 μs。

/*
 * 函数名：F_At24c256_SetResult。
 * 说明：保存 AT24C256 最近一次操作结果，并把成功结果转换为 true。
 * 输入：context 为驱动上下文；result 为需要保存的结果码。
 * 输出：result 为 AT24C256_RESULT_OK 时返回 true，否则返回 false。
 */
static bool F_At24c256_SetResult(F_At24c256_Context *context, F_At24c256_Result result)
{
    if (context != NULL)
    {
        context->last_result = result;
    }
    return (result == AT24C256_RESULT_OK);
}

/*
 * 函数名：F_At24c256_SetWriteProtect。
 * 说明：设置 AT24C256 的 WP 引脚，高电平禁止写入，低电平允许写入。
 * 输入：context 为驱动上下文；protect 为 true 时启用写保护，否则允许写入。
 * 输出：GPIO 配置和电平写入成功时返回 true，否则返回 false。
 */
static bool F_At24c256_SetWriteProtect(const F_At24c256_Context *context, bool protect)
{
    if (context == NULL)
    {
        return false;
    }

    return H_SoftIic_WriteControlPin(context->write_protect_pin, protect);
}

/*
 * 函数名：F_At24c256_AddressInRange。
 * 说明：检查起始地址和长度是否完整落在 AT24C256 的 32 KB 地址范围内。
 * 输入：address 为起始地址；length 为访问长度。
 * 输出：访问范围合法时返回 true，否则返回 false。
 */
static bool F_At24c256_AddressInRange(uint16_t address, size_t length)
{
    size_t start = address; // 当前作用域变量，用于保存起始边界。

    return (length <= AT24C256_CAPACITY_BYTES) &&
           (start <= AT24C256_CAPACITY_BYTES) &&
           (length <= (AT24C256_CAPACITY_BYTES - start));
}

/*
 * 函数名：F_At24c256_SendDeviceAddress。
 * 说明：向软件 IIC 总线发送 AT24C256 器件地址和读写位。
 * 输入：context 为驱动上下文；read 为 true 时发送读地址，否则发送写地址。
 * 输出：器件返回 ACK 时返回 true，否则返回 false。
 */
static bool F_At24c256_SendDeviceAddress(F_At24c256_Context *context, bool read)
{
    uint8_t address_byte; // 当前作用域变量，用于保存存储或寄存器地址。

    if (context == NULL)
    {
        return false;
    }

    address_byte = (uint8_t) ((context->device_address_7bit << 1U) | (read ? 1U : 0U));
    return H_SoftIic_WriteByte(&context->iic, address_byte);
}

/*
 * 函数名：F_At24c256_ProbeReady。
 * 说明：发送一次写方向器件地址并检查 AT24C256 是否已经结束内部写周期。
 * 输入：context 为驱动上下文。
 * 输出：器件返回 ACK 时返回 true，否则返回 false。
 */
static bool F_At24c256_ProbeReady(F_At24c256_Context *context)
{
    bool acknowledged; // EEPROM应答标志；使用范围：当前器件就绪探测函数内；取值范围：false/true，false表示器件返回NACK或发送失败，true表示器件地址已收到ACK。

    if ((context == NULL) || !H_SoftIic_Start(&context->iic))
    {
        return false;
    }

    acknowledged = F_At24c256_SendDeviceAddress(context, false);
    H_SoftIic_Stop(&context->iic);
    // EEPROM 内部写周期期间会对器件地址返回 NACK，可用该行为判断写入是否真正结束。
    return acknowledged;
}

/*
 * 函数名：F_At24c256_WaitWriteComplete。
 * 说明：通过器件地址应答轮询等待 AT24C256 内部页写周期完成。
 * 输入：context 为驱动上下文。
 * 输出：限定次数内检测到器件 ACK 时返回 true，否则返回 false。
 */
static bool F_At24c256_WaitWriteComplete(F_At24c256_Context *context)
{
    uint8_t retry; // 当前作用域变量，用于保存当前处理数据。

    for (retry = 0U; retry < AT24C256_WRITE_POLL_COUNT; ++retry)
    {
        if (F_At24c256_ProbeReady(context))
        {
            return true;
        }
        H_SoftIic_DelayMicroseconds(AT24C256_WRITE_POLL_DELAY_US);
    }
    // 应答轮询避免依赖不确定的固定长延时，并限制异常器件造成的阻塞时间。

    return false;
}

/*
 * 函数名：F_At24c256_WritePageFragment。
 * 说明：把不跨越 64 字节页边界的一段数据写入 AT24C256，并等待内部写周期完成。
 * 输入：context 为驱动上下文；address 为页内起始地址；data 为数据；length 为本次写入长度。
 * 输出：地址、数据和内部写周期全部成功时返回 true，否则返回 false。
 */
static bool F_At24c256_WritePageFragment(F_At24c256_Context *context,
                              uint16_t address,
                              const uint8_t *data,
                              size_t length)
{
    size_t i; // 当前作用域变量，用于保存当前处理数据。

    if ((context == NULL) || (data == NULL) || (length == 0U) ||
        !H_SoftIic_Start(&context->iic) || !F_At24c256_SendDeviceAddress(context, false) ||
        !H_SoftIic_WriteByte(&context->iic, (uint8_t) (address >> 8U)) ||
        !H_SoftIic_WriteByte(&context->iic, (uint8_t) address))
    {
        H_SoftIic_Stop((context != NULL) ? &context->iic : NULL);
        return false;
    }

    for (i = 0U; i < length; ++i)
    {
        if (!H_SoftIic_WriteByte(&context->iic, data[i]))
        {
            H_SoftIic_Stop(&context->iic);
            return false;
        }
    }

    H_SoftIic_Stop(&context->iic);
    return F_At24c256_WaitWriteComplete(context);
}

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
                   uint8_t device_address_7bit)
{
    if ((context == NULL) || (device_address_7bit > 0x7FU))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_PARAMETER);
    }

    (void) memset(context, 0, sizeof(*context));
    context->write_protect_pin = write_protect_pin;
    context->device_address_7bit = device_address_7bit;

    if (!F_At24c256_SetWriteProtect(context, true) ||
        !H_SoftIic_Init(&context->iic, scl_pin, sda_pin, AT24C256_IIC_HALF_PERIOD_US))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_BUS);
    }

    if (!F_At24c256_ProbeReady(context))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_NACK);
    }

    context->initialized = true;
    return F_At24c256_SetResult(context, AT24C256_RESULT_OK);
}

/*
 * 函数名：F_At24c256_Read。
 * 说明：从指定 16 位 EEPROM 地址开始连续读取数据。
 * 输入：context 为驱动上下文；address 为起始地址；buffer 为输出缓冲区；length 为读取字节数。
 * 输出：读取成功时返回 true，否则返回 false，并更新最近结果。
 */
bool F_At24c256_Read(F_At24c256_Context *context,
                   uint16_t address,
                   uint8_t *buffer,
                   size_t length)
{
    size_t i; // 当前作用域变量，用于保存当前处理数据。

    if ((context == NULL) || (buffer == NULL) || (length == 0U))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_PARAMETER);
    }
    if (!context->initialized)
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_NOT_READY);
    }
    if (!F_At24c256_AddressInRange(address, length))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_RANGE);
    }

    if (!H_SoftIic_Start(&context->iic) || !F_At24c256_SendDeviceAddress(context, false) ||
        !H_SoftIic_WriteByte(&context->iic, (uint8_t) (address >> 8U)) ||
        !H_SoftIic_WriteByte(&context->iic, (uint8_t) address) ||
        !H_SoftIic_Start(&context->iic) || !F_At24c256_SendDeviceAddress(context, true))
    {
        H_SoftIic_Stop(&context->iic);
        return F_At24c256_SetResult(context, AT24C256_RESULT_NACK);
    }
    // 先用写方向设置 16 位字地址，再发送重复起始切到读方向，期间不能插入停止条件。

    for (i = 0U; i < length; ++i)
    {
        buffer[i] = H_SoftIic_ReadByte(&context->iic, (i + 1U) < length);
    }
    // 连续读取时对中间字节回 ACK，最后一个字节回 NACK，通知 EEPROM 结束顺序读。
    H_SoftIic_Stop(&context->iic);

    return F_At24c256_SetResult(context, AT24C256_RESULT_OK);
}

/*
 * 函数名：F_At24c256_Write。
 * 说明：按照 64 字节页边界自动拆分数据，并通过应答轮询完成连续写入。
 * 输入：context 为驱动上下文；address 为起始地址；data 为只读数据；length 为写入字节数。
 * 输出：全部页写入成功时返回 true，否则返回 false，并更新最近结果。
 */
bool F_At24c256_Write(F_At24c256_Context *context,
                    uint16_t address,
                    const uint8_t *data,
                    size_t length)
{
    size_t remaining = length; // 当前作用域变量，用于保存当前处理数据。
    size_t offset = 0U; // 当前作用域变量，用于保存数据偏移量。

    if ((context == NULL) || (data == NULL) || (length == 0U))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_PARAMETER);
    }
    if (!context->initialized)
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_NOT_READY);
    }
    if (!F_At24c256_AddressInRange(address, length))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_RANGE);
    }
    if (!F_At24c256_SetWriteProtect(context, false))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_BUS);
    }
    // 仅在实际写入期间拉低 WP；成功、失败路径都会尽快恢复写保护。

    while (remaining > 0U)
    {
        size_t page_offset = (size_t) address % AT24C256_PAGE_SIZE_BYTES; // 当前作用域变量，用于保存数据偏移量。
        size_t fragment = AT24C256_PAGE_SIZE_BYTES - page_offset; // 当前作用域变量，用于保存当前处理数据。

        if (fragment > remaining)
        {
            fragment = remaining;
        }
        if (!F_At24c256_WritePageFragment(context, address, &data[offset], fragment))
        {
            (void) F_At24c256_SetWriteProtect(context, true);
            return F_At24c256_SetResult(context, AT24C256_RESULT_NACK);
        }

        address = (uint16_t) (address + fragment);
        offset += fragment;
        remaining -= fragment;
    }
    // 每次写入长度均限制在当前页剩余空间内，避免 AT24C256 页内地址回卷覆盖前部数据。

    if (!F_At24c256_SetWriteProtect(context, true))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_BUS);
    }
    return F_At24c256_SetResult(context, AT24C256_RESULT_OK);
}

/*
 * 函数名：F_At24c256_EraseRange。
 * 说明：把指定地址范围按页写方式填充为 0xFF，不自动擦除范围之外的数据。
 * 输入：context 为驱动上下文；address 为起始地址；length 为需要填充的字节数。
 * 输出：指定范围全部写入 0xFF 时返回 true，否则返回 false。
 */
bool F_At24c256_EraseRange(F_At24c256_Context *context, uint16_t address, size_t length)
{
    uint8_t erased[AT24C256_PAGE_SIZE_BYTES]; // 当前作用域变量，用于保存当前处理数据数组。
    size_t remaining = length; // 当前作用域变量，用于保存当前处理数据。

    if ((context == NULL) || (length == 0U) || !F_At24c256_AddressInRange(address, length))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_PARAMETER);
    }

    (void) memset(erased, 0xFF, sizeof(erased));
    while (remaining > 0U)
    {
        size_t fragment = (remaining > sizeof(erased)) ? sizeof(erased) : remaining; // 当前作用域变量，用于保存当前处理数据。

        if (!F_At24c256_Write(context, address, erased, fragment))
        {
            return false;
        }
        address = (uint16_t) (address + fragment);
        remaining -= fragment;
    }

    return F_At24c256_SetResult(context, AT24C256_RESULT_OK);
}

/*
 * 函数名：F_At24c256_SelfTest。
 * 说明：备份指定两个字节、写入测试图样、读回校验并恢复原数据，用于非破坏性器件自检。
 * 输入：context 为驱动上下文；test_address 为至少可连续访问两个字节的保留地址。
 * 输出：测试图样读回且原数据恢复成功时返回 true，否则返回 false。
 */
bool F_At24c256_SelfTest(F_At24c256_Context *context, uint16_t test_address)
{
    const uint8_t pattern[2] = {0x4FU, 0x4BU}; // 当前作用域变量，用于保存当前处理数据数组。
    uint8_t original[2]; // 当前作用域变量，用于保存当前处理数据数组。
    uint8_t verified[2]; // 当前作用域变量，用于保存当前处理数据数组。
    bool result; // EEPROM自检图样校验标志；使用范围：当前自检函数内；取值范围：false/true，false表示读回图样不一致，true表示读回图样一致。

    if ((context == NULL) || !F_At24c256_AddressInRange(test_address, sizeof(pattern)) ||
        !F_At24c256_Read(context, test_address, original, sizeof(original)) ||
        !F_At24c256_Write(context, test_address, pattern, sizeof(pattern)) ||
        !F_At24c256_Read(context, test_address, verified, sizeof(verified)))
    {
        return false;
    }

    result = (memcmp(pattern, verified, sizeof(pattern)) == 0);
    if (!F_At24c256_Write(context, test_address, original, sizeof(original)))
    {
        return false;
    }
    // 自检先保存原数据并在校验后恢复，测试地址仍应在存储规划中明确保留。

    return F_At24c256_SetResult(context, result ? AT24C256_RESULT_OK : AT24C256_RESULT_VERIFY);
}
