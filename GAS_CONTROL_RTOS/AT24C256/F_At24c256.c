/*
 * Version: v1.14
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
 * 函数名：F_At24c256_HandleBusError。
 * 说明：对已确认的软件 IIC 总线错误执行一次有界恢复，但不重试当前 EEPROM 操作。
 * 输入：context 为驱动上下文。
 * 输出：始终返回 false，并把最近结果保存为 AT24C256_RESULT_BUS。
 */
static bool F_At24c256_HandleBusError(F_At24c256_Context *context)
{
    if (context != NULL)
    {
        (void) H_SoftIic_RecoverBus(&context->iic);
    }
    return F_At24c256_SetResult(context, AT24C256_RESULT_BUS);
}

/*
 * 函数名：F_At24c256_SetWriteProtect。
 * 说明：设置 AT24C256 的 WP 引脚，高电平禁止写入，低电平允许写入。
 * 输入：context 为驱动上下文；protect 为 true 时启用写保护，否则允许写入。
 * 输出：GPIO 配置和电平写入成功时返回 true，否则返回 false。
 */
static bool F_At24c256_SetWriteProtect(F_At24c256_Context *context, bool protect)
{
    bool success; // WP引脚配置和电平写入结果。

    if (context == NULL)
    {
        return false;
    }

    success = H_SoftIic_WriteControlPin(context->write_protect_pin, protect);
    if (!success)
    {
        context->write_protect_fault = true;
    }
    return success;
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
 * 函数名：F_At24c256_WriteBusByte。
 * 说明：发送一个字节，并把底层总线结果和从机应答转换为 AT24C256 结果码。
 * 输入：context 为驱动上下文；data 为需要发送的字节。
 * 输出：返回 OK、NACK、BUS 或 PARAMETER。
 */
static F_At24c256_Result F_At24c256_WriteBusByte(F_At24c256_Context *context, uint8_t data)
{
    bool acknowledged = false; // 从机应答标志；false 表示 NACK，true 表示 ACK。

    if (context == NULL)
    {
        return AT24C256_RESULT_PARAMETER;
    }
    if (!H_SoftIic_WriteByte(&context->iic, data, &acknowledged))
    {
        return AT24C256_RESULT_BUS;
    }
    return acknowledged ? AT24C256_RESULT_OK : AT24C256_RESULT_NACK;
}

/*
 * 函数名：F_At24c256_SendDeviceAddress。
 * 说明：向软件 IIC 总线发送 AT24C256 器件地址和读写位。
 * 输入：context 为驱动上下文；read 为 true 时发送读地址，否则发送写地址。
 * 输出：返回 OK、NACK、BUS 或 PARAMETER，用于区分从机应答和总线失败。
 */
static F_At24c256_Result F_At24c256_SendDeviceAddress(F_At24c256_Context *context, bool read)
{
    uint8_t address_byte; // 当前作用域变量，用于保存存储或寄存器地址。

    if (context == NULL)
    {
        return AT24C256_RESULT_PARAMETER;
    }

    address_byte = (uint8_t) ((context->device_address_7bit << 1U) | (read ? 1U : 0U));
    return F_At24c256_WriteBusByte(context, address_byte);
}

/*
 * 函数名：F_At24c256_EndFailedTransfer。
 * 说明：在字节发送失败后尝试发送停止条件，并对已知总线错误执行一次恢复。
 * 输入：context 为驱动上下文；transfer_result 为字节发送结果。
 * 输出：始终返回 false，并保留 NACK 或上报 BUS。
 */
static bool F_At24c256_EndFailedTransfer(F_At24c256_Context *context,
                                         F_At24c256_Result transfer_result)
{
    bool stop_succeeded; // 停止条件执行结果；false 表示 GPIO 或时序故障。

    if (context == NULL)
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_PARAMETER);
    }

    stop_succeeded = H_SoftIic_Stop(&context->iic);
    if ((transfer_result == AT24C256_RESULT_BUS) || !stop_succeeded)
    {
        return F_At24c256_HandleBusError(context);
    }
    return F_At24c256_SetResult(context, transfer_result);
}

/*
 * 函数名：F_At24c256_ProbeReady。
 * 说明：发送一次写方向器件地址并检查 AT24C256 是否已经结束内部写周期。
 * 输入：context 为驱动上下文。
 * 输出：器件返回 ACK 时返回 true，否则返回 false。
 */
static bool F_At24c256_ProbeReady(F_At24c256_Context *context)
{
    F_At24c256_Result address_result; // 器件地址发送结果，用于区分 ACK、NACK 和总线失败。
    bool stop_succeeded; // 停止条件执行结果；false 表示 GPIO 或时序故障。

    if (context == NULL)
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_PARAMETER);
    }
    if (!H_SoftIic_Start(&context->iic))
    {
        return F_At24c256_HandleBusError(context);
    }

    address_result = F_At24c256_SendDeviceAddress(context, false);
    stop_succeeded = H_SoftIic_Stop(&context->iic);
    if ((address_result == AT24C256_RESULT_BUS) || !stop_succeeded)
    {
        return F_At24c256_HandleBusError(context);
    }
    // EEPROM 内部写周期期间会对器件地址返回 NACK，可用该行为判断写入是否真正结束。
    return F_At24c256_SetResult(context, address_result);
}

/*
 * 函数名：F_At24c256_WaitWriteComplete。
 * 说明：通过器件地址应答轮询等待 AT24C256 内部页写周期完成。
 * 输入：context 为驱动上下文。
 * 输出：限定次数内检测到器件 ACK 时返回 true；总线失败或等待超时时返回 false。
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
        if ((context == NULL) || (context->last_result == AT24C256_RESULT_BUS))
        {
            return false;
        }
        H_SoftIic_DelayMicroseconds(AT24C256_WRITE_POLL_DELAY_US);
    }
    // 应答轮询避免依赖不确定的固定长延时，并限制异常器件造成的阻塞时间。

    return F_At24c256_SetResult(context, AT24C256_RESULT_TIMEOUT);
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
    F_At24c256_Result transfer_result; // 当前发送字节的 ACK/NACK/总线结果。

    if ((context == NULL) || (data == NULL) || (length == 0U))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_PARAMETER);
    }
    if (!H_SoftIic_Start(&context->iic))
    {
        return F_At24c256_HandleBusError(context);
    }

    transfer_result = F_At24c256_SendDeviceAddress(context, false);
    if (transfer_result != AT24C256_RESULT_OK)
    {
        return F_At24c256_EndFailedTransfer(context, transfer_result);
    }
    transfer_result = F_At24c256_WriteBusByte(context, (uint8_t) (address >> 8U));
    if (transfer_result != AT24C256_RESULT_OK)
    {
        return F_At24c256_EndFailedTransfer(context, transfer_result);
    }
    transfer_result = F_At24c256_WriteBusByte(context, (uint8_t) address);
    if (transfer_result != AT24C256_RESULT_OK)
    {
        return F_At24c256_EndFailedTransfer(context, transfer_result);
    }

    for (i = 0U; i < length; ++i)
    {
        transfer_result = F_At24c256_WriteBusByte(context, data[i]);
        if (transfer_result != AT24C256_RESULT_OK)
        {
            return F_At24c256_EndFailedTransfer(context, transfer_result);
        }
    }

    if (!H_SoftIic_Stop(&context->iic))
    {
        return F_At24c256_HandleBusError(context);
    }
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

    if (!F_At24c256_SetWriteProtect(context, true))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_BUS);
    }
    if (!H_SoftIic_Init(&context->iic, scl_pin, sda_pin, AT24C256_IIC_HALF_PERIOD_US))
    {
        // H_SoftIic_Init 在 GPIO 配置成功后已自行执行一次有界总线恢复。
        return F_At24c256_SetResult(context, AT24C256_RESULT_BUS);
    }

    if (!F_At24c256_ProbeReady(context))
    {
        return false;
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
    F_At24c256_Result transfer_result; // 当前发送字节的 ACK/NACK/总线结果。

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

    if (!H_SoftIic_Start(&context->iic))
    {
        return F_At24c256_HandleBusError(context);
    }

    transfer_result = F_At24c256_SendDeviceAddress(context, false);
    if (transfer_result != AT24C256_RESULT_OK)
    {
        return F_At24c256_EndFailedTransfer(context, transfer_result);
    }
    transfer_result = F_At24c256_WriteBusByte(context, (uint8_t) (address >> 8U));
    if (transfer_result != AT24C256_RESULT_OK)
    {
        return F_At24c256_EndFailedTransfer(context, transfer_result);
    }
    transfer_result = F_At24c256_WriteBusByte(context, (uint8_t) address);
    if (transfer_result != AT24C256_RESULT_OK)
    {
        return F_At24c256_EndFailedTransfer(context, transfer_result);
    }
    if (!H_SoftIic_Start(&context->iic))
    {
        (void) H_SoftIic_Stop(&context->iic);
        return F_At24c256_HandleBusError(context);
    }

    transfer_result = F_At24c256_SendDeviceAddress(context, true);
    if (transfer_result != AT24C256_RESULT_OK)
    {
        return F_At24c256_EndFailedTransfer(context, transfer_result);
    }
    // 先用写方向设置 16 位字地址，再发送重复起始切到读方向，期间不能插入停止条件。

    for (i = 0U; i < length; ++i)
    {
        if (!H_SoftIic_ReadByte(&context->iic,
                                (i + 1U) < length,
                                &buffer[i]))
        {
            (void) H_SoftIic_Stop(&context->iic);
            return F_At24c256_HandleBusError(context);
        }
    }
    // 连续读取时对中间字节回 ACK，最后一个字节回 NACK，通知 EEPROM 结束顺序读。
    if (!H_SoftIic_Stop(&context->iic))
    {
        return F_At24c256_HandleBusError(context);
    }

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
    F_At24c256_Result write_result; // 页写失败码，用于恢复写保护后保留原始错误。

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
        (void) F_At24c256_SetWriteProtect(context, true);
        // 解除写保护失败时立即尽力恢复高电平，避免WP停留在可写状态。
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
            write_result = context->last_result;
            if (!F_At24c256_SetWriteProtect(context, true))
            {
                return F_At24c256_SetResult(context, write_result);
            }
            return F_At24c256_SetResult(context, write_result);
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
    uint8_t pattern[2]; // 根据原数据生成且每字节必然变化的测试图样，避免固定图样碰撞造成误判。
    uint8_t original[2]; // 当前作用域变量，用于保存当前处理数据数组。
    uint8_t verified[2]; // 当前作用域变量，用于保存当前处理数据数组。
    uint8_t restored[2]; // 恢复原数据后的读回值，用于确认自检没有破坏保留地址。
    F_At24c256_Result test_result; // 测试写和读回阶段的结果，恢复原数据后重新写回上下文。

    if ((context == NULL) || !F_At24c256_AddressInRange(test_address, sizeof(pattern)))
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_PARAMETER);
    }
    if (!F_At24c256_Read(context, test_address, original, sizeof(original)))
    {
        return false;
    }
    pattern[0] = (uint8_t) (original[0] ^ 0xFFU);
    pattern[1] = (uint8_t) (original[1] ^ 0xA5U);

    if (!F_At24c256_Write(context, test_address, pattern, sizeof(pattern)))
    {
        test_result = context->last_result;
    }
    else if (!F_At24c256_Read(context, test_address, verified, sizeof(verified)))
    {
        test_result = context->last_result;
    }
    else
    {
        test_result = (memcmp(pattern, verified, sizeof(pattern)) == 0) ?
                      AT24C256_RESULT_OK : AT24C256_RESULT_VERIFY;
    }

    // 只要已进入测试写阶段，即使写入或读回失败也必须尝试恢复原数据。
    if (!F_At24c256_Write(context, test_address, original, sizeof(original)))
    {
        return false;
    }
    if (!F_At24c256_Read(context, test_address, restored, sizeof(restored)))
    {
        return false;
    }
    if (memcmp(original, restored, sizeof(original)) != 0)
    {
        return F_At24c256_SetResult(context, AT24C256_RESULT_VERIFY);
    }
    // 自检先保存原数据并在校验后恢复、读回确认，测试地址仍应在存储规划中明确保留。

    return F_At24c256_SetResult(context, test_result);
}
