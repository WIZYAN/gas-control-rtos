#ifndef A_GAS_CONFIG_H
#define A_GAS_CONFIG_H

#include <stdbool.h>

#include "gas_common.h"
#include "../AT24C256/A_Storage.h"

#define A_GAS_CONFIG_STORAGE_ADDRESS (0x0000U) // AT24C256 中运行参数记录的起始字节地址。
#define A_GAS_CONFIG_RECORD_VERSION  (0x0003U) // EEPROM 当前13项三阀参数记录的数据结构版本。
#define A_GAS_CONFIG_REGISTER_COUNT  (10U)     // 外部CAN和Modbus继续开放的原有参数数量，不含三项HMI安全参数。

// 运行参数校验结果，用于区分单项越界和参数之间的逻辑关系错误。
typedef enum
{
    A_GAS_CONFIG_VALID = 0,        // 所有参数范围和相互关系均正确。
    A_GAS_CONFIG_INVALID_RANGE,    // 至少一个参数超出可编码或可执行范围。
    A_GAS_CONFIG_INVALID_RELATION  // 压力阈值等参数之间的先后关系不正确。
} A_Gas_Config_Validation;

/*
 * 函数名：A_GasConfig_LoadDefaults。
 * 说明：把编译期默认值写入一个运行参数结构体。
 * 输入：config 为待初始化的运行参数输出指针。
 * 输出：无；参数有效时通过 config 输出全部默认值。
 */
void A_GasConfig_LoadDefaults(Gas_Config *config);

/*
 * 函数名：A_GasConfig_Validate。
 * 说明：检查运行参数的单项范围和压力阈值关系。
 * 输入：config 为只读运行参数指针。
 * 输出：返回参数有效、范围错误或关系错误的校验结果。
 */
A_Gas_Config_Validation A_GasConfig_Validate(const Gas_Config *config);

/*
 * 函数名：A_GasConfig_Load。
 * 说明：从 AT24C256 读取带版本和 CRC16 的运行参数记录并完成校验。
 * 输入：storage 为已经初始化的存储上下文；config 为运行参数输出指针。
 * 输出：记录标识、版本、CRC 和参数均有效时返回 true，否则返回 false。
 */
bool A_GasConfig_Load(A_Storage_Context *storage, Gas_Config *config);

/*
 * 函数名：A_GasConfig_Save。
 * 说明：把运行参数编码为单页记录并写入 AT24C256，随后读回校验。
 * 输入：storage 为已经初始化的存储上下文；config 为待保存的只读运行参数。
 * 输出：参数有效且写入、读回全部成功时返回 true，否则返回 false。
 */
bool A_GasConfig_Save(A_Storage_Context *storage, const Gas_Config *config);

#endif
