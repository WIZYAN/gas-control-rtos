#ifndef A_GAS_LOG_H
#define A_GAS_LOG_H

#include <stdbool.h>
#include <stdint.h>

#include "gas_common.h"
#include "../AT24C256/A_Storage.h"

#define A_GAS_LOG_HEADER_A_ADDRESS       (0x0040U) // 日志管理头A的EEPROM起始地址，独占一个64字节页。
#define A_GAS_LOG_HEADER_B_ADDRESS       (0x0080U) // 日志管理头B的EEPROM起始地址，独占一个64字节页。
#define A_GAS_LOG_DATA_START_ADDRESS     (0x00C0U) // 第一条日志记录的EEPROM起始地址。
#define A_GAS_LOG_HEADER_SIZE            (32U)     // 单份日志管理头占用的字节数。
#define A_GAS_LOG_RECORD_SIZE            (32U)     // 常规记录和事件记录统一占用的字节数。
#define A_GAS_LOG_PHYSICAL_SLOT_COUNT    (1018U)   // 日志数据区包含的32字节物理槽位总数。
#define A_GAS_LOG_RECORD_CAPACITY        (1017U)   // 对外有效记录上限，保留一个槽位用于掉电安全提交。
#define A_GAS_LOG_FORMAT_VERSION         (1U)      // 当前32字节日志记录格式版本。
#define A_GAS_LOG_HEADER_VERSION         (1U)      // 当前日志管理头格式版本。

// 日志记录类型，保存在每条32字节记录的第0字节。
typedef enum
{
    A_GAS_LOG_TYPE_REGULAR = 1, // 常规记录：时间、七路压力和六瓶状态。
    A_GAS_LOG_TYPE_EVENT = 2    // 事件记录：气瓶状态变化及变化时的系统信息。
} A_Gas_Log_Type;

// 事件原因编码，为后续增加阀门、报警等事件保留统一扩展入口。
typedef enum
{
    A_GAS_LOG_EVENT_STATE_CHANGED = 1 // 气瓶业务状态发生变化。
} A_Gas_Log_Event_Reason;

// 日志管理应用层上下文，保存EEPROM循环位置、双管理头状态和六瓶状态快照。
typedef struct
{
    A_Storage_Context *storage; // 当前日志模块使用的EEPROM应用层实例。
    uint32_t generation; // 当前有效管理头的更新代数，用于选择较新的掉电恢复副本。
    uint32_t next_sequence; // 下一条日志将使用的流水号，溢出后允许自然回绕。
    uint32_t last_regular_key; // 最近一次常规记录对应的半小时时段键，防止重复记录。
    uint16_t write_index; // 下一条日志写入的物理槽位索引，范围0～1017。
    uint16_t valid_count; // 当前有效日志数量，最大为1017。
    gas_cylinder_state_t previous_state[GAS_CYLINDER_COUNT]; // 最近已记录的六瓶状态快照。
    uint8_t active_header_copy; // 当前有效管理头副本，0表示A，1表示B。
    bool ready; // 管理头已经加载或创建且允许记录日志时为true。
} A_Gas_Log_Context;

/*
 * 函数名：A_GasLog_Init。
 * 说明：加载双副本日志管理头，或在首次使用时创建空循环日志，并建立六瓶状态快照。
 * 输入：context 为日志上下文；storage 为已初始化EEPROM实例；system 为当前只读气源状态。
 * 输出：日志区成功恢复或初始化时返回true，否则返回false。
 */
bool A_GasLog_Init(A_Gas_Log_Context *context,
                   A_Storage_Context *storage,
                   const Gas_System *system);

/*
 * 函数名：A_GasLog_Task。
 * 说明：在时间有效时记录六瓶状态变化，并按半小时时段保存一条常规运行记录。
 * 输入：context 为日志上下文；system 为包含时间、压力和状态的只读气源系统。
 * 输出：本周期无须写入或全部处理成功时返回true，EEPROM写入失败时返回false。
 */
bool A_GasLog_Task(A_Gas_Log_Context *context, const Gas_System *system);

/*
 * 函数名：A_GasLog_ReadRecord。
 * 说明：按照从最旧到最新的逻辑顺序读取并校验一条32字节原始日志，供后续通信模块调用。
 * 输入：context 为日志上下文；logical_index 为零起始逻辑序号；record 为32字节输出缓存。
 * 输出：索引有效且记录版本、类型和CRC16全部正确时返回true，否则返回false。
 */
bool A_GasLog_ReadRecord(A_Gas_Log_Context *context,
                         uint16_t logical_index,
                         uint8_t record[A_GAS_LOG_RECORD_SIZE]);

/*
 * 函数名：A_GasLog_GetCount。
 * 说明：查询循环日志区当前保存的有效记录数量。
 * 输入：context 为只读日志上下文。
 * 输出：返回有效记录数量；上下文未就绪时返回0。
 */
uint16_t A_GasLog_GetCount(const A_Gas_Log_Context *context);

/*
 * 函数名：A_GasLog_IsReady。
 * 说明：查询日志管理模块是否已经成功初始化。
 * 输入：context 为只读日志上下文。
 * 输出：日志模块可用时返回true，否则返回false。
 */
bool A_GasLog_IsReady(const A_Gas_Log_Context *context);

#endif
