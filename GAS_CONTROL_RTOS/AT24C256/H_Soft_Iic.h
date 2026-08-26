#ifndef H_SOFT_IIC_H
#define H_SOFT_IIC_H

#include <stdbool.h>
#include <stdint.h>

// 软件 IIC 硬件层上下文，保存总线使用的 GPIO 和时序参数，不使用模块全局变量。
typedef struct
{
    uint32_t scl_pin;         // 软件 IIC 时钟线对应的 FSP GPIO 编码。
    uint32_t sda_pin;         // 软件 IIC 数据线对应的 FSP GPIO 编码。
    uint32_t half_period_us;  // 软件 IIC 半个时钟周期，单位微秒。
    bool initialized;        // 两个 GPIO 是否已经配置为 NMOS 开漏输出。
} H_Soft_Iic_Context;

/*
 * 函数名：H_SoftIic_Init。
 * 说明：初始化软件 IIC 上下文，将 SCL 和 SDA 配置为释放状态的 NMOS 开漏输出，并尝试恢复总线。
 * 输入：context 为软件 IIC 上下文；scl_pin 和 sda_pin 为 FSP GPIO 编码；half_period_us 为半周期微秒数。
 * 输出：总线成功释放并处于空闲状态时返回 true，否则返回 false。
 */
bool H_SoftIic_Init(H_Soft_Iic_Context *context,
                  uint32_t scl_pin,
                  uint32_t sda_pin,
                  uint32_t half_period_us);

/*
 * 函数名：H_SoftIic_Start。
 * 说明：在当前软件 IIC 总线上产生起始条件或重复起始条件。
 * 输入：context 为已经初始化的软件 IIC 上下文。
 * 输出：成功产生起始条件时返回 true，否则返回 false。
 */
bool H_SoftIic_Start(H_Soft_Iic_Context *context);

/*
 * 函数名：H_SoftIic_Stop。
 * 说明：在当前软件 IIC 总线上产生停止条件并释放 SCL 和 SDA。
 * 输入：context 为已经初始化的软件 IIC 上下文。
 * 输出：无返回值；总线被恢复到空闲状态。
 */
void H_SoftIic_Stop(H_Soft_Iic_Context *context);

/*
 * 函数名：H_SoftIic_WriteByte。
 * 说明：按照高位在前的顺序发送一个字节并读取从机应答位。
 * 输入：context 为软件 IIC 上下文；data 为需要发送的一个字节。
 * 输出：从机返回低电平 ACK 时返回 true，未应答或时钟线异常时返回 false。
 */
bool H_SoftIic_WriteByte(H_Soft_Iic_Context *context, uint8_t data);

/*
 * 函数名：H_SoftIic_ReadByte。
 * 说明：从软件 IIC 总线读取一个字节，并根据参数向从机返回 ACK 或 NACK。
 * 输入：context 为软件 IIC 上下文；send_ack 为 true 时回送 ACK，为 false 时回送 NACK。
 * 输出：返回从总线读取的一个字节；上下文无效时返回 0xFF。
 */
uint8_t H_SoftIic_ReadByte(H_Soft_Iic_Context *context, bool send_ack);

/*
 * 函数名：H_SoftIic_RecoverBus。
 * 说明：在 SDA 被从机占用时产生最多九个 SCL 恢复脉冲，并发送停止条件释放总线。
 * 输入：context 为已经配置 GPIO 的软件 IIC 上下文。
 * 输出：恢复后 SCL 和 SDA 均为高电平时返回 true，否则返回 false。
 */
bool H_SoftIic_RecoverBus(H_Soft_Iic_Context *context);

/*
 * 函数名：H_SoftIic_WriteControlPin。
 * 说明：把 EEPROM 辅助控制引脚配置为推挽输出并写入指定电平，供功能层控制 WP 引脚。
 * 输入：pin 为 FSP GPIO 编码；high 为 true 时输出高电平，为 false 时输出低电平。
 * 输出：引脚配置和电平写入均成功时返回 true，否则返回 false。
 */
bool H_SoftIic_WriteControlPin(uint32_t pin, bool high);

/*
 * 函数名：H_SoftIic_DelayMicroseconds。
 * 说明：对单片机微秒延时接口进行硬件层封装，供 EEPROM 功能层等待器件内部写周期。
 * 输入：microseconds 为需要等待的微秒数。
 * 输出：无。
 */
void H_SoftIic_DelayMicroseconds(uint32_t microseconds);

#endif
