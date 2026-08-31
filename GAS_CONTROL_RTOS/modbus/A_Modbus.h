/*
 * Version: v1.11
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明外部Modbus应用层寄存器、结果码和业务接口。
 */

// 本文件声明 SCI0/RS485 外部 Modbus 从站应用层接口。
#ifndef A_MODBUS_H
#define A_MODBUS_H

#include <stdbool.h>
#include <stdint.h>

#include "F_Modbus.h"
#include "gas_common.h"

#define A_MODBUS_SLAVE_ADDRESS            (1U)      // 外部 SCI0 Modbus 从站地址。
#define A_MODBUS_SOFTWARE_VERSION         (0x0202U) // 外部寄存器映射版本；0x0202废弃整机人工启停命令。

// 输入寄存器偏移定义，外部主站使用功能码 04 和 PDU 地址 0x0000～0x0025 读取。
#define A_MODBUS_INPUT_PRESSURE_BASE      (0U)  // 六路 float32 压力输入寄存器的起始偏移。
#define A_MODBUS_INPUT_STATE_BASE         (12U) // 六路气瓶状态输入寄存器的起始偏移。
#define A_MODBUS_INPUT_QUALITY_BASE       (18U) // 六路压力质量输入寄存器的起始偏移。
#define A_MODBUS_INPUT_MODE               (24U) // 系统运行模式输入寄存器偏移。
#define A_MODBUS_INPUT_ACTIVE_BOTTLE      (25U) // 当前工作瓶号输入寄存器偏移。
#define A_MODBUS_INPUT_SWITCH_STATE       (26U) // 自动切瓶状态输入寄存器偏移。
#define A_MODBUS_INPUT_EXHAUST_STATE      (27U) // 六路排气阀当前命令位图，bit0～bit5对应1～6号瓶。
#define A_MODBUS_INPUT_ALARM_HIGH         (28U) // 32 位报警值高 16 位寄存器偏移。
#define A_MODBUS_INPUT_ALARM_LOW          (29U) // 32 位报警值低 16 位寄存器偏移。
#define A_MODBUS_INPUT_PLATFORM_READY     (30U) // 硬件平台就绪标志寄存器偏移。
#define A_MODBUS_INPUT_VERSION            (31U) // 软件版本寄存器偏移。
#define A_MODBUS_INPUT_TOTAL_PRESSURE_BASE (32U) // 总压力 float32 输入寄存器的起始偏移。
#define A_MODBUS_INPUT_TOTAL_QUALITY       (34U) // 总压力数据质量输入寄存器偏移。
#define A_MODBUS_INPUT_QUALIFIED_MASK      (35U) // 六瓶测试合格标志位图，bit0～bit5对应1～6号瓶。
#define A_MODBUS_INPUT_SUPPLY_MASK         (36U) // 六路进气阀当前命令位图，bit0～bit5对应1～6号瓶。
#define A_MODBUS_INPUT_TEST_MASK           (37U) // 六路测试阀当前命令位图，bit0～bit5对应1～6号瓶。

// 保持寄存器偏移定义，外部主站使用 PDU 地址0x0100～0x0125读写命令、运行参数和日志。
#define A_MODBUS_HOLDING_COMMAND          (0U) // V1.08废弃的整机启停寄存器；保留地址并拒绝非零写入。
#define A_MODBUS_HOLDING_RESULT           (1U) // 外部命令执行结果保持寄存器偏移。
#define A_MODBUS_HOLDING_CONFIG_COMMIT    (2U) // 参数校验、应用和保存命令寄存器偏移。
#define A_MODBUS_HOLDING_CONFIG_RESULT    (3U) // 参数处理结果寄存器偏移。
#define A_MODBUS_HOLDING_CONFIG_VERSION   (4U) // 参数结构版本寄存器偏移。
#define A_MODBUS_HOLDING_CONFIG_DEFAULT   (5U) // 恢复默认参数命令寄存器偏移。
#define A_MODBUS_HOLDING_CONFIG_BASE      (6U) // 10 项运行参数的起始寄存器偏移。

// 日志读取保持寄存器偏移，记录数据按照每个寄存器高字节在前的顺序连续输出。
#define A_MODBUS_HOLDING_LOG_COMMAND      (16U) // 日志读取命令寄存器偏移，对应PDU地址0x0110。
#define A_MODBUS_HOLDING_LOG_INDEX        (17U) // 从最旧记录开始的零起始逻辑序号，对应PDU地址0x0111。
#define A_MODBUS_HOLDING_LOG_RESULT       (18U) // 日志读取结果寄存器偏移，对应PDU地址0x0112。
#define A_MODBUS_HOLDING_LOG_COUNT        (19U) // 有效日志数量寄存器偏移，对应PDU地址0x0113。
#define A_MODBUS_HOLDING_LOG_CAPACITY     (20U) // 日志容量寄存器偏移，对应PDU地址0x0114。
#define A_MODBUS_HOLDING_LOG_RECORD_SIZE  (21U) // 单条日志字节数寄存器偏移，对应PDU地址0x0115。
#define A_MODBUS_HOLDING_LOG_DATA_BASE    (22U) // 16个日志数据寄存器起始偏移，对应PDU地址0x0116。
#define A_MODBUS_LOG_RECORD_SIZE          (32U) // 外部接口传输的单条日志固定字节数。
#define A_MODBUS_LOG_REGISTER_COUNT       (16U) // 32字节日志占用的连续16位寄存器数量。

// 运行参数相对于 A_MODBUS_HOLDING_CONFIG_BASE 的连续偏移。
#define A_MODBUS_CONFIG_SWITCH_PRESSURE       (0U) // 切瓶压力，MPa×1000。
#define A_MODBUS_CONFIG_SWITCH_RELEASE        (1U) // 回差释放压力，MPa×1000。
#define A_MODBUS_CONFIG_VALVE_PULL_IN_TIME    (2U) // 12 V 吸合时间，单位 ms。
#define A_MODBUS_CONFIG_READY_MIN_PRESSURE    (3U) // 备用瓶最低压力，MPa×1000。
#define A_MODBUS_CONFIG_PRESSURE_MAX          (4U) // 压力合法性上限，MPa×1000。
#define A_MODBUS_CONFIG_LOW_CONFIRM_TIME      (5U) // 低压确认时间，单位 ms。
#define A_MODBUS_CONFIG_LOW_CONFIRM_SAMPLES   (6U) // 低压确认样本数。
#define A_MODBUS_CONFIG_VALVE_CLOSE_WAIT      (7U) // 关阀稳定等待时间，单位 ms。
#define A_MODBUS_CONFIG_VALVE_OPEN_WAIT       (8U) // 开阀稳定等待时间，单位 ms。
#define A_MODBUS_CONFIG_PRESSURE_FRESH        (9U) // 压力数据新鲜度时限，单位 ms。

#define A_MODBUS_CONFIG_COMMIT_KEY         (0xA55AU) // 写入参数提交寄存器的确认键值。
#define A_MODBUS_CONFIG_DEFAULT_KEY        (0x5AA5U) // 写入恢复默认寄存器的确认键值。
#define A_MODBUS_CONFIG_VERSION_VALUE      (0x0002U) // 外部寄存器公布的三阀参数结构版本。

// 旧版整机启停操作码，仅用于识别并拒绝旧上位机命令，V1.08不再执行。
typedef enum
{
    A_MODBUS_COMMAND_NONE = 0,             // 不执行命令。
    A_MODBUS_COMMAND_START_AUTO = 1, // 启动自动供气。
    A_MODBUS_COMMAND_STOP = 2        // 停止并关闭全部阀门。
} a_modbus_command_t;

// 命令结果寄存器使用的状态码。
typedef enum
{
    A_MODBUS_RESULT_IDLE = 0,            // 当前没有待处理命令。
    A_MODBUS_RESULT_PENDING = 1,         // 命令已经接收，等待或正在执行。
    A_MODBUS_RESULT_SUCCESS = 2,         // 命令执行成功。
    A_MODBUS_RESULT_INVALID_COMMAND = 3, // 命令码不在支持范围内。
    A_MODBUS_RESULT_REJECTED = 4         // 当前系统状态不允许执行该命令。
} a_modbus_result_t;

// 运行参数提交结果寄存器使用的状态码。
typedef enum
{
    A_MODBUS_CONFIG_RESULT_IDLE = 0,             // 当前没有参数处理请求。
    A_MODBUS_CONFIG_RESULT_PENDING = 1,          // 参数已校验，等待业务层保存和应用。
    A_MODBUS_CONFIG_RESULT_SUCCESS = 2,          // 参数已保存到 EEPROM 并投入运行。
    A_MODBUS_CONFIG_RESULT_INVALID_RANGE = 3,    // 至少一个参数超出允许范围。
    A_MODBUS_CONFIG_RESULT_INVALID_RELATION = 4, // 参数之间的阈值关系不正确。
    A_MODBUS_CONFIG_RESULT_STORAGE_FAILED = 5,   // AT24C256 保存或读回校验失败。
    A_MODBUS_CONFIG_RESULT_SYSTEM_BUSY = 6,      // 未满足六瓶全部停用且十八路阀关闭的维护条件。
    A_MODBUS_CONFIG_RESULT_INVALID_KEY = 7       // 参数提交或恢复默认键值错误。
} a_modbus_config_result_t;

// 外部主站写入日志命令寄存器的操作码。
typedef enum
{
    A_MODBUS_LOG_COMMAND_NONE = 0, // 不执行日志操作。
    A_MODBUS_LOG_COMMAND_READ = 1  // 读取日志序号寄存器指定的一条记录。
} A_Modbus_Log_Command;

// 日志读取结果寄存器使用的状态码。
typedef enum
{
    A_MODBUS_LOG_RESULT_IDLE = 0,            // 没有日志读取操作。
    A_MODBUS_LOG_RESULT_PENDING = 1,         // 请求已接收，等待应用层读取EEPROM。
    A_MODBUS_LOG_RESULT_SUCCESS = 2,         // 32字节日志已经写入数据窗口。
    A_MODBUS_LOG_RESULT_INVALID_COMMAND = 3, // 日志命令码无效。
    A_MODBUS_LOG_RESULT_INVALID_INDEX = 4,   // 指定逻辑序号不在有效日志范围内。
    A_MODBUS_LOG_RESULT_READ_FAILED = 5,     // EEPROM读取或日志CRC校验失败。
    A_MODBUS_LOG_RESULT_BUSY = 6             // 上一条日志请求尚未由应用层处理。
} A_Modbus_Log_Result;

// 外部Modbus应用层上下文，拥有SCI0硬件层、从站功能层以及待处理参数和日志请求。
typedef struct
{
    H_Modbus_Context hardware; // 外部 SCI0/RS485 硬件层实例。
    F_Modbus_Context function; // 外部 Modbus RTU 从站功能层实例。
    Gas_Config pending_config; // 已通过寄存器解码和校验、等待业务层应用的运行参数。
    Gas_Config current_config; // 当前完整13项参数镜像；外部解码只覆盖开放的原有10项。
    bool config_pending; // 存在尚未由气源业务层取走的参数提交请求；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
    uint16_t pending_log_index; // 等待日志模块读取的零起始逻辑序号。
    bool log_read_pending; // 存在尚未由气源应用层取走的日志读取请求；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
    bool ready; // 外部 Modbus 已经完成初始化的标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。
} A_Modbus_Context;

/*
 * 函数名：A_Modbus_Init。
 * 说明：初始化外部 SCI0 Modbus 从站及应用寄存器表。
 * 输入：context 为待初始化的外部 Modbus 应用层上下文；config 为当前有效运行参数。
 * 输出：全部层级初始化成功时返回 true，否则返回 false。
 */
bool A_Modbus_Init(A_Modbus_Context *context, const Gas_Config *config);

/*
 * 函数名：A_Modbus_Deinit。
 * 说明：关闭外部SCI0/RS485 Modbus并清除应用层就绪状态。
 * 输入：context为外部Modbus应用层上下文输入输出指针。
 * 输出：接口已经关闭或成功关闭时返回true，否则返回false。
 */
bool A_Modbus_Deinit(A_Modbus_Context *context);

/*
 * 函数名：A_Modbus_Refresh。
 * 说明：将六瓶压力与状态、总压力、阀门位图、测试合格位图和系统状态刷新到外部输入寄存器。
 * 输入：context 为外部 Modbus 应用层上下文；system 为只读气源系统状态。
 * 输出：无；输入寄存器表在 context 中被更新。
 */
void A_Modbus_Refresh(A_Modbus_Context *context, const Gas_System *system);

/*
 * 函数名：A_Modbus_Task。
 * 说明：周期处理外部主站请求，并把合法写寄存器操作转换为控制、运行参数或日志读取请求。
 * 输入：context 为外部 Modbus 应用层上下文输入输出指针。
 * 输出：无；待执行控制、参数和日志请求及对应结果状态写入context。
 */
void A_Modbus_Task(A_Modbus_Context *context);

/*
 * 函数名：A_Modbus_SetCommandResult。
 * 说明：将气源应用层的命令执行结果写入外部 Modbus 结果寄存器。
 * 输入：context 为外部 Modbus 应用层上下文；result 为待写入的结果状态码。
 * 输出：无；结果保存到保持寄存器 0x0101。
 */
void A_Modbus_SetCommandResult(A_Modbus_Context *context, a_modbus_result_t result);

/*
 * 函数名：A_Modbus_UpdateConfigRegisters。
 * 说明：把当前有效运行参数刷新到外部 Modbus 参数保持寄存器。
 * 输入：context 为外部 Modbus 应用层上下文；config 为只读运行参数。
 * 输出：参数和上下文有效时返回 true，否则返回 false。
 */
bool A_Modbus_UpdateConfigRegisters(A_Modbus_Context *context, const Gas_Config *config);

/*
 * 函数名：A_Modbus_TakeConfigRequest。
 * 说明：取出一份已经通过寄存器解码和参数校验的待应用运行参数。
 * 输入：context 为外部 Modbus 上下文；config 为运行参数输出指针。
 * 输出：存在待处理参数请求时返回 true，否则返回 false。
 */
bool A_Modbus_TakeConfigRequest(A_Modbus_Context *context, Gas_Config *config);

/*
 * 函数名：A_Modbus_SetConfigResult。
 * 说明：更新外部 Modbus 参数处理结果寄存器。
 * 输入：context 为外部 Modbus 上下文；result 为参数处理结果码。
 * 输出：无；结果写入 PDU 地址 0x0103。
 */
void A_Modbus_SetConfigResult(A_Modbus_Context *context, a_modbus_config_result_t result);

/*
 * 函数名：A_Modbus_TakeLogReadRequest。
 * 说明：取出并清除一条由外部主站提交的日志读取请求。
 * 输入：context 为外部Modbus上下文；logical_index为零起始日志逻辑序号输出指针。
 * 输出：存在待处理日志请求时返回true，否则返回false。
 */
bool A_Modbus_TakeLogReadRequest(A_Modbus_Context *context, uint16_t *logical_index);

/*
 * 函数名：A_Modbus_UpdateLogInfo。
 * 说明：刷新外部保持寄存器中的有效日志数量、容量和单条记录长度。
 * 输入：context 为外部Modbus上下文；valid_count为有效数量；capacity为最大数量。
 * 输出：无；三个只读信息寄存器被更新。
 */
void A_Modbus_UpdateLogInfo(A_Modbus_Context *context,
                            uint16_t valid_count,
                            uint16_t capacity);

/*
 * 函数名：A_Modbus_SetLogRecord。
 * 说明：把一条32字节原始日志按高字节在前顺序写入16个日志数据保持寄存器。
 * 输入：context 为外部Modbus上下文；record为32字节只读日志缓存。
 * 输出：全部数据寄存器更新成功时返回true，否则返回false。
 */
bool A_Modbus_SetLogRecord(A_Modbus_Context *context,
                           const uint8_t record[A_MODBUS_LOG_RECORD_SIZE]);

/*
 * 函数名：A_Modbus_SetLogReadResult。
 * 说明：更新日志读取结果，失败结果同时清空日志数据窗口以避免误用上一次数据。
 * 输入：context 为外部Modbus上下文；result为日志读取结果码。
 * 输出：无；结果和必要的数据窗口内容写入保持寄存器。
 */
void A_Modbus_SetLogReadResult(A_Modbus_Context *context, A_Modbus_Log_Result result);

/*
 * 函数名：A_Modbus_IsReady。
 * 说明：查询外部 Modbus 是否已经成功初始化。
 * 输入：context 为只读外部 Modbus 应用层上下文指针。
 * 输出：初始化完成时返回 true，否则返回 false。
 */
bool A_Modbus_IsReady(const A_Modbus_Context *context);

/*
 * 函数名：A_Modbus_HasFault。
 * 说明：查询外部Modbus应用层及其下层是否存在影响通讯的故障。
 * 输入：context为只读外部Modbus应用层上下文。
 * 输出：应用层未就绪或SCI0硬件故障时返回true，否则返回false。
 */
bool A_Modbus_HasFault(const A_Modbus_Context *context);

#endif
