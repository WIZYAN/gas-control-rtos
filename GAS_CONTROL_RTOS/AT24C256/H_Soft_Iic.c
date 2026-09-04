/*
 * Version: v1.14
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现AT24C256使用的GPIO软件IIC时序和总线操作。
 */

#include "H_Soft_Iic.h"

#include <stddef.h>

#include "hal_data.h"

/*
 * 函数名：H_SoftIic_DelayHalfPeriod。
 * 说明：按照当前上下文配置的软件 IIC 半周期执行微秒级延时。
 * 输入：context 为只读软件 IIC 上下文。
 * 输出：无。
 */
static void H_SoftIic_DelayHalfPeriod(const H_Soft_Iic_Context *context)
{
    if ((context != NULL) && (context->half_period_us > 0U))
    {
        //执行短时硬件延时
        R_BSP_SoftwareDelay(context->half_period_us, BSP_DELAY_UNITS_MICROSECONDS);
    }
}

/*
 * 函数名：H_SoftIic_ConfigureOpenDrainPin。
 * 说明：将指定 GPIO 配置为初始释放的 NMOS 开漏输出，并启用数字输入功能。
 * 输入：pin 为 FSP GPIO 编码。
 * 输出：配置成功时返回 true，否则返回 false。
 */
static bool H_SoftIic_ConfigureOpenDrainPin(uint32_t pin)
{
    uint32_t configuration = IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                             IOPORT_CFG_PORT_OUTPUT_HIGH |
                             IOPORT_CFG_NMOS_ENABLE |
                             IOPORT_CFG_PIM_TTL;

    //配置GPIO引脚模式
    return (R_IOPORT_PinCfg(&g_ioport_ctrl, (bsp_io_port_pin_t) pin, configuration) == FSP_SUCCESS);
}

/*
 * 函数名：H_SoftIic_WriteLine。
 * 说明：控制开漏 GPIO，写低电平表示拉低总线，写高电平表示释放总线。
 * 输入：pin 为 FSP GPIO 编码；high 为 true 时释放线路，为 false 时拉低线路。
 * 输出：GPIO 写入成功时返回 true，否则返回 false。
 */
static bool H_SoftIic_WriteLine(uint32_t pin, bool high)
{
    bsp_io_level_t level = high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW; // 当前作用域变量，用于保存GPIO电平。

    //写入GPIO引脚电平
    return (R_IOPORT_PinWrite(&g_ioport_ctrl, (bsp_io_port_pin_t) pin, level) == FSP_SUCCESS);
}

/*
 * 函数名：H_SoftIic_ReadLine。
 * 说明：读取指定软件 IIC GPIO 的实际总线电平。
 * 输入：pin 为 FSP GPIO 编码；high 为总线电平输出指针。
 * 输出：读取成功时返回 true 并通过 high 输出电平，否则返回 false。
 */
static bool H_SoftIic_ReadLine(uint32_t pin, bool *high)
{
    bsp_io_level_t level; // 当前作用域变量，用于保存GPIO电平。

    //读取GPIO引脚电平
    if ((high == NULL) ||
        (R_IOPORT_PinRead(&g_ioport_ctrl, (bsp_io_port_pin_t) pin, &level) != FSP_SUCCESS))
    {
        return false;
    }

    *high = (level == BSP_IO_LEVEL_HIGH);
    return true;
}

/*
 * 函数名：H_SoftIic_RaiseClock。
 * 说明：释放 SCL 并等待实际时钟线变高，以兼容从机的短时间时钟延展。
 * 输入：context 为软件 IIC 上下文。
 * 输出：SCL 在超时前变高时返回 true，否则返回 false。
 */
static bool H_SoftIic_RaiseClock(H_Soft_Iic_Context *context)
{
    uint32_t retry; // 当前作用域变量，用于保存当前处理数据。
    bool high = false; // SCL采样电平标志；使用范围：当前升高时钟函数内；取值范围：false/true，false表示SCL仍为低电平，true表示SCL已经变为高电平。

    //控制开漏 GPIO
    if ((context == NULL) || !H_SoftIic_WriteLine(context->scl_pin, true))
    {
        return false;
    }

    for (retry = 0U; retry < 100U; ++retry)
    {
        //读取指定软件 IIC GPIO 的实际总线电平
        if (H_SoftIic_ReadLine(context->scl_pin, &high) && high)
        {
            //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
            H_SoftIic_DelayHalfPeriod(context);
            return true;
        }
        //执行短时硬件延时
        R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MICROSECONDS);
    }
    // SCL 持续为低电平表示总线短路或从机时钟延展超时。

    return false;
}

/*
 * 函数名：H_SoftIic_Init。
 * 说明：初始化软件 IIC 上下文，将 SCL 和 SDA 配置为释放状态的 NMOS 开漏输出，并尝试恢复总线。
 * 输入：context 为软件 IIC 上下文；scl_pin 和 sda_pin 为 FSP GPIO 编码；half_period_us 为半周期微秒数。
 * 输出：总线成功释放并处于空闲状态时返回 true，否则返回 false。
 */
bool H_SoftIic_Init(H_Soft_Iic_Context *context,
                  uint32_t scl_pin,
                  uint32_t sda_pin,
                  uint32_t half_period_us)
{
    if ((context == NULL) || (half_period_us == 0U))
    {
        return false;
    }

    context->scl_pin = scl_pin;
    context->sda_pin = sda_pin;
    context->half_period_us = half_period_us;
    context->initialized = false;

    //将指定 GPIO 配置为初始释放的 NMOS 开漏输出
    if (!H_SoftIic_ConfigureOpenDrainPin(scl_pin) || !H_SoftIic_ConfigureOpenDrainPin(sda_pin))
    {
        return false;
    }

    context->initialized = true;
    //在 SDA 被从机占用时产生最多九个 SCL 恢复脉冲
    return H_SoftIic_RecoverBus(context);
}

/*
 * 函数名：H_SoftIic_Start。
 * 说明：在当前软件 IIC 总线上产生起始条件或重复起始条件。
 * 输入：context 为已经初始化的软件 IIC 上下文。
 * 输出：成功产生起始条件时返回 true，否则返回 false。
 */
bool H_SoftIic_Start(H_Soft_Iic_Context *context)
{
    bool sda_high = false; // SDA起始前采样电平标志；使用范围：当前起始条件函数内；取值范围：false/true，false表示SDA被拉低且总线忙，true表示SDA已经释放为高电平。

    //控制开漏 GPIO；释放 SCL 并等待实际时钟线变高；读取指定软件 IIC GPIO 的实际总线电平
    if ((context == NULL) || !context->initialized ||
        !H_SoftIic_WriteLine(context->sda_pin, true) || !H_SoftIic_RaiseClock(context) ||
        !H_SoftIic_ReadLine(context->sda_pin, &sda_high) || !sda_high)
    {
        return false;
    }

    //控制开漏 GPIO
    if (!H_SoftIic_WriteLine(context->sda_pin, false))
    {
        return false;
    }
    //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
    H_SoftIic_DelayHalfPeriod(context);

    //控制开漏 GPIO
    if (!H_SoftIic_WriteLine(context->scl_pin, false))
    {
        return false;
    }
    //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
    H_SoftIic_DelayHalfPeriod(context);
    // SCL 为高时将 SDA 从高拉低形成起始条件。

    return true;
}

/*
 * 函数名：H_SoftIic_Stop。
 * 说明：在当前软件 IIC 总线上产生停止条件并释放 SCL 和 SDA。
 * 输入：context 为已经初始化的软件 IIC 上下文。
 * 输出：停止时序的所有GPIO操作成功时返回true，否则返回false。
 */
bool H_SoftIic_Stop(H_Soft_Iic_Context *context)
{
    bool success = true; // 停止时序执行结果；false表示至少一次GPIO访问失败。
    bool scl_high = false; // STOP结束后的SCL实际电平；false表示线路仍被拉低，true表示总线已释放。
    bool sda_high = false; // STOP结束后的SDA实际电平；false表示线路仍被拉低，true表示总线已释放。

    if ((context == NULL) || !context->initialized)
    {
        return false;
    }

    //控制开漏 GPIO
    if (!H_SoftIic_WriteLine(context->scl_pin, false))
    {
        success = false;
    }
    //控制开漏 GPIO
    if (!H_SoftIic_WriteLine(context->sda_pin, false))
    {
        success = false;
    }
    //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
    H_SoftIic_DelayHalfPeriod(context);
    //释放 SCL 并等待实际时钟线变高
    if (!H_SoftIic_RaiseClock(context))
    {
        success = false;
    }
    //控制开漏 GPIO
    if (!H_SoftIic_WriteLine(context->sda_pin, true))
    {
        success = false;
    }
    //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
    H_SoftIic_DelayHalfPeriod(context);
    //读取指定软件 IIC GPIO 的实际总线电平
    if (!H_SoftIic_ReadLine(context->scl_pin, &scl_high))
    {
        success = false;
    }
    //读取指定软件 IIC GPIO 的实际总线电平
    if (!H_SoftIic_ReadLine(context->sda_pin, &sda_high))
    {
        success = false;
    }
    if (!scl_high || !sda_high)
    {
        success = false;
    }
    // SCL 为高时释放 SDA 形成停止条件。
    return success;
}

/*
 * 函数名：H_SoftIic_WriteByte。
 * 说明：按照高位在前的顺序发送一个字节并读取从机应答位。
 * 输入：context 为软件 IIC 上下文；data 为需要发送的一个字节；acknowledged 为从机应答输出指针。
 * 输出：GPIO 和时序完整时返回 true，并通过 acknowledged 返回 ACK(true)或 NACK(false)；总线操作失败时返回 false。
 */
bool H_SoftIic_WriteByte(H_Soft_Iic_Context *context, uint8_t data, bool *acknowledged)
{
    uint8_t bit; // 当前写字节函数使用的位序号，范围0～7。
    bool sda_high = true; // 从机应答位采样标志；使用范围：当前写字节函数内；取值范围：false/true，false表示从机返回低电平ACK，true表示从机返回高电平NACK。

    if (acknowledged == NULL)
    {
        return false;
    }
    *acknowledged = false;
    if ((context == NULL) || !context->initialized)
    {
        return false;
    }

    for (bit = 0U; bit < 8U; ++bit)
    {
        //控制开漏 GPIO
        if (!H_SoftIic_WriteLine(context->sda_pin, (data & 0x80U) != 0U))
        {
            return false;
        }
        //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
        H_SoftIic_DelayHalfPeriod(context);
        //释放 SCL 并等待实际时钟线变高
        if (!H_SoftIic_RaiseClock(context))
        {
            return false;
        }
        //控制开漏 GPIO
        if (!H_SoftIic_WriteLine(context->scl_pin, false))
        {
            return false;
        }
        //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
        H_SoftIic_DelayHalfPeriod(context);
        data <<= 1U;
    }

    //控制开漏 GPIO
    if (!H_SoftIic_WriteLine(context->sda_pin, true))
    {
        return false;
    }
    //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
    H_SoftIic_DelayHalfPeriod(context);
    //释放 SCL 并等待实际时钟线变高；读取指定软件 IIC GPIO 的实际总线电平
    if (!H_SoftIic_RaiseClock(context) || !H_SoftIic_ReadLine(context->sda_pin, &sda_high))
    {
        return false;
    }
    //控制开漏 GPIO
    if (!H_SoftIic_WriteLine(context->scl_pin, false))
    {
        return false;
    }
    //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
    H_SoftIic_DelayHalfPeriod(context);
    // 第九个时钟周期由从机拉低 SDA 表示 ACK。

    *acknowledged = !sda_high;
    return true;
}

/*
 * 函数名：H_SoftIic_ReadByte。
 * 说明：从软件 IIC 总线读取一个字节，并根据参数向从机返回 ACK 或 NACK。
 * 输入：context 为软件 IIC 上下文；send_ack 为 true 时回送 ACK，为 false 时回送 NACK；data为字节输出指针。
 * 输出：数据位和ACK/NACK时序全部成功时返回true，否则返回false。
 */
bool H_SoftIic_ReadByte(H_Soft_Iic_Context *context, bool send_ack, uint8_t *data)
{
    uint8_t bit; // 当前读字节函数使用的位序号，范围0～7。
    uint8_t value = 0U; // 当前总线累计接收的字节，仅在完整时序成功后写给调用者。
    bool sda_high = false; // SDA数据位采样标志；使用范围：当前读字节函数内；取值范围：false/true，false表示当前总线位为0，true表示当前总线位为1。
    bool acknowledge_ok = true; // 第九个时钟及总线释放结果；false表示至少一次GPIO操作失败，true表示完整执行。

    //控制开漏 GPIO
    if ((context == NULL) || (data == NULL) || !context->initialized ||
        !H_SoftIic_WriteLine(context->sda_pin, true))
    {
        return false;
    }

    for (bit = 0U; bit < 8U; ++bit)
    {
        value <<= 1U;
        //释放 SCL 并等待实际时钟线变高；读取指定软件 IIC GPIO 的实际总线电平
        if (!H_SoftIic_RaiseClock(context) || !H_SoftIic_ReadLine(context->sda_pin, &sda_high))
        {
            return false;
        }
        if (sda_high)
        {
            value |= 0x01U;
        }
        //控制开漏 GPIO
        if (!H_SoftIic_WriteLine(context->scl_pin, false))
        {
            return false;
        }
        //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
        H_SoftIic_DelayHalfPeriod(context);
    }

    //控制开漏 GPIO
    if (!H_SoftIic_WriteLine(context->sda_pin, !send_ack))
    {
        return false;
    }
    //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
    H_SoftIic_DelayHalfPeriod(context);
    //释放 SCL 并等待实际时钟线变高
    if (!H_SoftIic_RaiseClock(context))
    {
        acknowledge_ok = false;
    }
    //控制开漏 GPIO
    if (!H_SoftIic_WriteLine(context->scl_pin, false))
    {
        acknowledge_ok = false;
    }
    //控制开漏 GPIO
    if (!H_SoftIic_WriteLine(context->sda_pin, true))
    {
        acknowledge_ok = false;
    }
    //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
    H_SoftIic_DelayHalfPeriod(context);
    // 主机在第九个时钟输出 ACK 或 NACK，随后重新释放 SDA。

    if (!acknowledge_ok)
    {
        return false;
    }
    *data = value;
    return true;
}

/*
 * 函数名：H_SoftIic_RecoverBus。
 * 说明：在 SDA 被从机占用时产生最多九个 SCL 恢复脉冲，并发送停止条件释放总线。
 * 输入：context 为已经配置 GPIO 的软件 IIC 上下文。
 * 输出：恢复后 SCL 和 SDA 均为高电平时返回 true，否则返回 false。
 */
bool H_SoftIic_RecoverBus(H_Soft_Iic_Context *context)
{
    uint8_t pulse; // 当前作用域变量，用于保存当前处理数据。
    bool scl_high = false; // 恢复后SCL采样电平标志；使用范围：当前总线恢复函数内；取值范围：false/true，false表示SCL仍被拉低，true表示SCL已经释放为高电平。
    bool sda_high = false; // 恢复后SDA采样电平标志；使用范围：当前总线恢复函数内；取值范围：false/true，false表示SDA仍被拉低，true表示SDA已经释放为高电平。
    bool scl_released; // 恢复开始时主机释放SCL的GPIO操作结果。
    bool sda_released; // 恢复开始时主机释放SDA的GPIO操作结果。

    if ((context == NULL) || !context->initialized)
    {
        return false;
    }
    //控制开漏 GPIO
    scl_released = H_SoftIic_WriteLine(context->scl_pin, true);
    //控制开漏 GPIO
    sda_released = H_SoftIic_WriteLine(context->sda_pin, true);
    if (!scl_released || !sda_released)
    {
        return false;
    }

    for (pulse = 0U; pulse < 9U; ++pulse)
    {
        //读取指定软件 IIC GPIO 的实际总线电平
        if (H_SoftIic_ReadLine(context->sda_pin, &sda_high) && sda_high)
        {
            break;
        }
        //控制开漏 GPIO
        if (!H_SoftIic_WriteLine(context->scl_pin, false))
        {
            return false;
        }
        //按照当前上下文配置的软件 IIC 半周期执行微秒级延时
        H_SoftIic_DelayHalfPeriod(context);
        //释放 SCL 并等待实际时钟线变高
        if (!H_SoftIic_RaiseClock(context))
        {
            return false;
        }
    }
    // 九个恢复时钟可使中断在字节中间的从机释放 SDA。

    return H_SoftIic_Stop(context) &&
           H_SoftIic_ReadLine(context->scl_pin, &scl_high) && scl_high &&
           H_SoftIic_ReadLine(context->sda_pin, &sda_high) && sda_high;
}

/*
 * 函数名：H_SoftIic_WriteControlPin。
 * 说明：把 EEPROM 辅助控制引脚配置为推挽输出并写入指定电平，供功能层控制 WP 引脚。
 * 输入：pin 为 FSP GPIO 编码；high 为 true 时输出高电平，为 false 时输出低电平。
 * 输出：引脚配置和电平写入均成功时返回 true，否则返回 false。
 */
bool H_SoftIic_WriteControlPin(uint32_t pin, bool high)
{
    uint32_t configuration = IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                             IOPORT_CFG_PORT_OUTPUT_HIGH;
    bsp_io_level_t level = high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW; // 当前作用域变量，用于保存GPIO电平。

    bool configure_ok; // 控制引脚配置结果；失败时仍继续尝试写入目标安全电平。
    bool write_ok; // 控制引脚电平写入结果。

    //配置GPIO引脚模式
    configure_ok = (R_IOPORT_PinCfg(&g_ioport_ctrl,
                                    (bsp_io_port_pin_t) pin,
                                    configuration) == FSP_SUCCESS);
    if (!configure_ok && !high)
    {
        return false;
    }
    // 配置失败时禁止继续主动拉低WP；目标为高电平时仍尝试恢复写保护。
    write_ok = (R_IOPORT_PinWrite(&g_ioport_ctrl,
                                  (bsp_io_port_pin_t) pin,
                                  level) == FSP_SUCCESS);
    return configure_ok && write_ok;
}

/*
 * 函数名：H_SoftIic_DelayMicroseconds。
 * 说明：对单片机微秒延时接口进行硬件层封装，供 EEPROM 功能层等待器件内部写周期。
 * 输入：microseconds 为需要等待的微秒数。
 * 输出：无。
 */
void H_SoftIic_DelayMicroseconds(uint32_t microseconds)
{
    if (microseconds > 0U)
    {
        //执行短时硬件延时
        R_BSP_SoftwareDelay(microseconds, BSP_DELAY_UNITS_MICROSECONDS);
    }
}
