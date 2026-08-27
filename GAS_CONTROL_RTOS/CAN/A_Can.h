#ifndef A_CAN_H
#define A_CAN_H

#include <stdbool.h>
#include <stdint.h>

#include "F_Can_Protocol.h"
#include "gas_common.h"

#define A_CAN_SOFTWARE_VERSION             (0x0205UL) // CAN地址表版本；0x0205增加直接参数写入、详细错误码和气瓶人工控制地址。
#define A_CAN_CONFIG_DEFAULT_KEY           (0x00005AA5UL) // 恢复默认参数的确认键值。
#define A_CAN_CONFIG_VERSION_VALUE         (0x00000002UL) // 当前三阀运行参数结构版本。
#define A_CAN_LOG_RECORD_SIZE              (32U) // 外部CAN读取的单条日志固定长度。

// 只读float32数据地址，线上数据为IEEE-754原始位模式并按小端放入data[4]～data[7]。
#define A_CAN_ADDRESS_PRESSURE_BASE        (0x0000U) // 六路气瓶压力，地址0x0000～0x0005。
#define A_CAN_ADDRESS_TOTAL_PRESSURE       (0x0006U) // 总压力传感器压力。

// 只读uint32数据地址。
#define A_CAN_ADDRESS_STATE_BASE           (0x0100U) // 六路气瓶状态，地址0x0100～0x0105。
#define A_CAN_ADDRESS_QUALITY_BASE         (0x0106U) // 六路压力质量，地址0x0106～0x010B。
#define A_CAN_ADDRESS_TOTAL_QUALITY        (0x010CU) // 总压力质量。
#define A_CAN_ADDRESS_SYSTEM_MODE          (0x010DU) // 系统运行模式。
#define A_CAN_ADDRESS_ACTIVE_BOTTLE        (0x010EU) // 工作瓶号，1～6；没有工作瓶时为0。
#define A_CAN_ADDRESS_SWITCH_STATE         (0x010FU) // 自动切瓶子状态。
#define A_CAN_ADDRESS_EXHAUST_MASK         (0x0110U) // 六路排气阀命令位图。
#define A_CAN_ADDRESS_ALARM_BITS           (0x0111U) // 32位系统报警位。
#define A_CAN_ADDRESS_PLATFORM_READY       (0x0112U) // 硬件平台就绪标志。
#define A_CAN_ADDRESS_SOFTWARE_VERSION     (0x0113U) // 软件版本。
#define A_CAN_ADDRESS_QUALIFIED_MASK       (0x0114U) // 六瓶测试合格位图。
#define A_CAN_ADDRESS_SUPPLY_MASK          (0x0115U) // 六路进气阀命令位图。
#define A_CAN_ADDRESS_TEST_MASK            (0x0116U) // 六路测试阀命令位图。
#define A_CAN_ADDRESS_COMM_MODE            (0x0117U) // 当前外部通讯模式，0为CAN、1为RS485。
#define A_CAN_ADDRESS_COMMAND_RESULT       (0x0118U) // 最近一次控制命令结果。
#define A_CAN_ADDRESS_CONFIG_RESULT        (0x0119U) // 最近一次参数处理结果。
#define A_CAN_ADDRESS_CONFIG_VERSION       (0x011AU) // 参数结构版本。
#define A_CAN_ADDRESS_LOG_RESULT           (0x011BU) // 最近一次日志读取结果。
#define A_CAN_ADDRESS_LOG_COUNT            (0x011CU) // 当前有效日志数量。
#define A_CAN_ADDRESS_LOG_CAPACITY         (0x011DU) // 最大日志数量。
#define A_CAN_ADDRESS_LOG_RECORD_SIZE      (0x011EU) // 单条日志字节数。
#define A_CAN_ADDRESS_LOG_DATA_BASE        (0x011FU) // 32字节只读日志窗口，地址0x011F～0x0126。
#define A_CAN_ADDRESS_LAST_WRITE_ADDRESS   (0x0127U) // 最近一次CAN写请求的数据地址。
#define A_CAN_ADDRESS_LAST_WRITE_RESULT    (0x0128U) // 最近一次CAN写操作的完整结果，低字节为基础码、次低字节为详细码。
#define A_CAN_ADDRESS_LAST_WRITE_VALUE     (0x0129U) // 最近一次CAN写请求携带的32位原始值。
#define A_CAN_ADDRESS_WRITE_SEQUENCE       (0x012AU) // CAN写请求累计序号，每接收一条格式正确的写请求加1。

// 可读写float32运行参数地址。
#define A_CAN_ADDRESS_SWITCH_PRESSURE      (0x0200U) // 低压切瓶阈值，单位MPa。
#define A_CAN_ADDRESS_SWITCH_RELEASE       (0x0201U) // 低压释放回差阈值，单位MPa。
#define A_CAN_ADDRESS_READY_MIN_PRESSURE   (0x0202U) // 进入待用的最低压力，单位MPa。
#define A_CAN_ADDRESS_PRESSURE_MAX         (0x0203U) // 压力物理上限，单位MPa。

// 可读写uint32运行参数、控制及日志地址。
#define A_CAN_ADDRESS_VALVE_PULL_IN_TIME   (0x0300U) // 12V强吸合时间，单位ms。
#define A_CAN_ADDRESS_LOW_CONFIRM_TIME     (0x0301U) // 低压持续确认时间，单位ms。
#define A_CAN_ADDRESS_LOW_CONFIRM_SAMPLES  (0x0302U) // 低压确认样本数。
#define A_CAN_ADDRESS_VALVE_CLOSE_WAIT     (0x0303U) // 关闭旧阀后的等待时间，单位ms。
#define A_CAN_ADDRESS_VALVE_OPEN_WAIT      (0x0304U) // 打开新阀后的等待时间，单位ms。
#define A_CAN_ADDRESS_PRESSURE_FRESH       (0x0305U) // 压力数据新鲜度，单位ms。
#define A_CAN_ADDRESS_COMMAND              (0x0306U) // 启停控制命令。
#define A_CAN_ADDRESS_CONFIG_COMMIT        (0x0307U) // V1.06废弃的参数提交地址；读取为0，写入返回地址已废弃。
#define A_CAN_ADDRESS_CONFIG_DEFAULT       (0x0308U) // 恢复默认参数确认键。
#define A_CAN_ADDRESS_LOG_COMMAND          (0x0309U) // 日志操作命令，1表示读取。
#define A_CAN_ADDRESS_LOG_INDEX            (0x030AU) // 从最旧记录起算的零起始日志序号。
#define A_CAN_ADDRESS_EXHAUST_CONTROL_BASE (0x030BU) // 1～6号排气阀控制，地址0x030B～0x0310，0立即关闭、1定时开启。
#define A_CAN_ADDRESS_TEST_CONTROL_BASE    (0x0311U) // 1～6号测试阀控制，地址0x0311～0x0316，0关闭、1开启。
#define A_CAN_ADDRESS_DISABLE_CONTROL_BASE (0x0317U) // 1～6号停用控制，地址0x0317～0x031C，0重新启用、1停用。
#define A_CAN_ADDRESS_QUALIFY_CONTROL_BASE (0x031DU) // 1～6号测试结论，地址0x031D～0x0322，0未通过、1通过。

// CAN写响应值，写响应data[4]～data[7]均使用该32位结果码。
typedef enum
{
    A_CAN_WRITE_SUCCESS = 0,          // 写入已经完成并最终生效。
    A_CAN_WRITE_ADDRESS_ERROR = 1,    // 地址不存在、只读或已经废弃。
    A_CAN_WRITE_VALUE_ERROR = 2,      // 数据格式或单项数值范围无效。
    A_CAN_WRITE_EXECUTION_ERROR = 3   // 参数关系、状态、互锁、存储或忙状态拒绝执行。
} A_Can_Write_Result;

// CAN写响应详细码，放在功能码6响应data[5]，用于区分同一基础错误下的具体原因。
typedef enum
{
    A_CAN_WRITE_DETAIL_NONE = 0,              // 操作成功，没有附加错误。
    A_CAN_WRITE_DETAIL_ADDRESS_NOT_FOUND = 1, // 数据地址没有定义。
    A_CAN_WRITE_DETAIL_READ_ONLY = 2,         // 地址存在但不允许写入。
    A_CAN_WRITE_DETAIL_DEPRECATED = 3,        // 地址仅为旧版兼容保留，当前版本不再执行。
    A_CAN_WRITE_DETAIL_VALUE_TOO_LOW = 4,     // 写入值低于允许下限。
    A_CAN_WRITE_DETAIL_VALUE_TOO_HIGH = 5,    // 写入值高于允许上限。
    A_CAN_WRITE_DETAIL_VALUE_FORMAT = 6,      // float32为NaN或无穷大，或数据格式无效。
    A_CAN_WRITE_DETAIL_RELATION_CONFLICT = 7, // 与其他正式运行参数的大小关系冲突。
    A_CAN_WRITE_DETAIL_STATE_DISALLOWED = 8,  // 当前系统或气瓶状态不允许执行该操作。
    A_CAN_WRITE_DETAIL_VALVE_INTERLOCK = 9,   // 供气阀与人工阀门等安全互锁拒绝操作。
    A_CAN_WRITE_DETAIL_SWITCHING_BUSY = 10,   // 自动切瓶正处于机械关阀、死区、开阀或验证阶段。
    A_CAN_WRITE_DETAIL_STORAGE_FAILED = 11,   // EEPROM写入或读回校验失败，参数没有生效。
    A_CAN_WRITE_DETAIL_REQUEST_BUSY = 12,     // 上一条需要业务层处理的CAN写请求尚未结束。
    A_CAN_WRITE_DETAIL_HARDWARE_NOT_READY = 13// 阀门硬件或平台尚未就绪。
} A_Can_Write_Detail;

// CAN人工控制请求类型，由CAN应用层解析地址后交给气源业务层执行。
typedef enum
{
    A_CAN_CONTROL_NONE = 0,       // 当前没有CAN人工控制请求。
    A_CAN_CONTROL_EXHAUST,        // 定时开启或立即关闭排气阀。
    A_CAN_CONTROL_TEST,           // 开启或关闭测试阀。
    A_CAN_CONTROL_DISABLE,        // 停用或重新启用气瓶。
    A_CAN_CONTROL_QUALIFICATION   // 设置测试未通过或测试通过。
} A_Can_Control_Type;

// CAN应用层向气源业务层输出的一条人工控制请求。
typedef struct
{
    A_Can_Control_Type type; // 控制类型。
    uint8_t index;           // 从0开始的气瓶索引。
    bool enabled;            // true表示开启、停用或通过；false表示关闭、启用或未通过。
} A_Can_Control_Request;

// CAN外部通讯应用层上下文，集中保存协议、暂存参数、命令和日志窗口。
typedef struct
{
    H_Can_Context hardware;                 // CAN0硬件层实例。
    F_Can_Protocol_Context function;        // 自定义29位扩展帧协议功能层实例。
    Gas_Config staged_config;               // 当前已经保存并实际生效的10项CAN公开参数镜像。
    gas_external_command_t pending_command; // 等待气源应用层执行的启停命令。
    gas_external_result_t command_result;   // 最近一次命令执行结果。
    Gas_Config pending_config;              // 已校验并等待保存应用的参数。
    gas_external_config_result_t config_result; // 最近一次参数处理结果。
    uint16_t pending_log_index;              // 等待EEPROM读取的日志逻辑序号。
    uint16_t selected_log_index;             // 上位机最近写入的日志逻辑序号。
    gas_external_log_result_t log_result;    // 最近一次日志读取结果。
    uint16_t log_count;                      // 当前有效日志数量。
    uint16_t log_capacity;                   // 日志区域最大记录数量。
    uint8_t log_record[A_CAN_LOG_RECORD_SIZE]; // 最近读取成功的32字节原始日志。
    uint16_t read_response_address;          // 连续读响应下一帧对应的数据地址。
    uint16_t read_response_remaining;        // 当前连续读请求尚未加入发送队列的响应数量。
    uint8_t read_response_target_type;       // 连续读响应的目标节点类型，即原请求源类型。
    uint8_t read_response_target_address;    // 连续读响应的目标节点地址，即原请求源地址。
    uint32_t response_drop_count;            // 应用层响应队列满造成的累计丢弃数量。
    A_Can_Control_Request pending_control;   // 等待气源业务层执行的单瓶人工控制请求。
    uint16_t deferred_write_address;         // 等待最终结果应答的CAN写地址。
    uint8_t deferred_write_target_type;      // 最终写应答的目标设备类型，即原请求源类型。
    uint8_t deferred_write_target_address;   // 最终写应答的目标设备地址，即原请求源地址。
    uint32_t deferred_write_result;          // 已完成但尚未进入发送队列的完整写结果。
    uint16_t last_write_address;             // 最近一次格式正确的CAN写请求地址。
    uint32_t last_write_result;              // 最近一次完成或拒绝的CAN写结果。
    uint32_t last_write_value;               // 最近一次格式正确的CAN写请求原始值。
    uint32_t write_sequence;                 // 格式正确的CAN写请求累计序号。
    bool command_pending;                    // 存在待执行控制命令。
    bool config_pending;                     // 存在待保存应用的参数。
    bool control_pending;                    // 存在待执行业务层人工控制请求。
    bool deferred_write_pending;             // 存在等待业务层最终处理结果的写请求。
    bool deferred_write_response_ready;      // 最终结果已经产生，等待加入协议发送队列。
    bool log_read_pending;                   // 存在待读取的日志请求。
    bool ready;                              // CAN应用层已经初始化。
} A_Can_Context;

/*
 * 函数名：A_Can_Init。
 * 说明：初始化CAN0、自定义协议和当前运行参数镜像。
 * 输入：context为待初始化上下文；config为当前有效运行参数。
 * 输出：硬件层和功能层均初始化成功时返回true，否则返回false。
 */
bool A_Can_Init(A_Can_Context *context, const Gas_Config *config);

/*
 * 函数名：A_Can_Deinit。
 * 说明：关闭CAN0并清除应用层就绪状态，供停止状态切换外部通讯使用。
 * 输入：context为CAN应用层上下文输入输出指针。
 * 输出：CAN已经关闭或成功关闭时返回true，否则返回false。
 */
bool A_Can_Deinit(A_Can_Context *context);

/*
 * 函数名：A_Can_Task。
 * 说明：周期解析CAN读写请求、访问地址表并组织非阻塞响应。
 * 输入：context为CAN应用层上下文；system为只读气源系统状态；comm_mode为当前外部通讯模式。
 * 输出：无；请求、结果及发送队列状态保存在context中。
 */
void A_Can_Task(A_Can_Context *context,
                const Gas_System *system,
                gas_external_comm_mode_t comm_mode);

/*
 * 函数名：A_Can_TakeCommand。
 * 说明：取出并清除一条CAN控制命令。
 * 输入：context为CAN上下文；command为命令输出指针。
 * 输出：存在待处理命令时返回true，否则返回false。
 */
bool A_Can_TakeCommand(A_Can_Context *context, gas_external_command_t *command);

/*
 * 函数名：A_Can_SetCommandResult。
 * 说明：更新CAN只读整数地址0x0118公布的控制命令结果。
 * 输入：context为CAN上下文；result为命令结果。
 * 输出：无；结果保存到context。
 */
void A_Can_SetCommandResult(A_Can_Context *context, gas_external_result_t result);

/*
 * 函数名：A_Can_UpdateConfig。
 * 说明：用当前生效参数同时刷新CAN读值和下一轮写入暂存区。
 * 输入：context为CAN上下文；config为当前有效运行参数。
 * 输出：参数有效且更新成功时返回true，否则返回false。
 */
bool A_Can_UpdateConfig(A_Can_Context *context, const Gas_Config *config);

/*
 * 函数名：A_Can_TakeConfigRequest。
 * 说明：取出一份已经校验、等待业务层保存应用的运行参数。
 * 输入：context为CAN上下文；config为参数输出指针。
 * 输出：存在待处理参数请求时返回true，否则返回false。
 */
bool A_Can_TakeConfigRequest(A_Can_Context *context, Gas_Config *config);

/*
 * 函数名：A_Can_SetConfigResult。
 * 说明：更新CAN只读整数地址0x0119公布的参数处理结果。
 * 输入：context为CAN上下文；result为参数处理结果。
 * 输出：无；结果保存到context。
 */
void A_Can_SetConfigResult(A_Can_Context *context, gas_external_config_result_t result);

/*
 * 函数名：A_Can_TakeControlRequest。
 * 说明：取出并清除一条已经通过地址和值检查的CAN人工控制请求。
 * 输入：context为CAN上下文；request为控制请求输出指针。
 * 输出：存在待处理控制请求时返回true，否则返回false。
 */
bool A_Can_TakeControlRequest(A_Can_Context *context, A_Can_Control_Request *request);

/*
 * 函数名：A_Can_CompleteControlRequest。
 * 说明：保存CAN人工控制最终执行结果，并组织与原请求对应的功能码6响应。
 * 输入：context为CAN上下文；result为基础结果码；detail为详细原因码。
 * 输出：无；完整结果保存在诊断地址并等待协议层发送。
 */
void A_Can_CompleteControlRequest(A_Can_Context *context,
                                  A_Can_Write_Result result,
                                  A_Can_Write_Detail detail);

/*
 * 函数名：A_Can_TakeLogReadRequest。
 * 说明：取出并清除一条CAN日志读取请求。
 * 输入：context为CAN上下文；logical_index为日志逻辑序号输出指针。
 * 输出：存在待处理请求时返回true，否则返回false。
 */
bool A_Can_TakeLogReadRequest(A_Can_Context *context, uint16_t *logical_index);

/*
 * 函数名：A_Can_UpdateLogInfo。
 * 说明：更新CAN地址表中的日志数量和容量信息。
 * 输入：context为CAN上下文；valid_count为有效记录数；capacity为最大记录数。
 * 输出：无；信息保存到context。
 */
void A_Can_UpdateLogInfo(A_Can_Context *context, uint16_t valid_count, uint16_t capacity);

/*
 * 函数名：A_Can_SetLogRecord。
 * 说明：把一条32字节EEPROM原始日志复制到八个连续CAN数据地址窗口。
 * 输入：context为CAN上下文；record为32字节只读记录。
 * 输出：复制成功时返回true，否则返回false。
 */
bool A_Can_SetLogRecord(A_Can_Context *context, const uint8_t record[A_CAN_LOG_RECORD_SIZE]);

/*
 * 函数名：A_Can_SetLogReadResult。
 * 说明：更新CAN日志读取结果，非成功结果同时清空旧日志窗口。
 * 输入：context为CAN上下文；result为日志读取结果。
 * 输出：无；结果和日志窗口保存在context中。
 */
void A_Can_SetLogReadResult(A_Can_Context *context, gas_external_log_result_t result);

/*
 * 函数名：A_Can_IsReady。
 * 说明：查询CAN外部通讯应用层是否已经成功初始化。
 * 输入：context为只读CAN上下文。
 * 输出：初始化成功时返回true，否则返回false。
 */
bool A_Can_IsReady(const A_Can_Context *context);

/*
 * 函数名：A_Can_HasFault。
 * 说明：查询CAN应用层及其下层是否存在影响外部通讯的故障。
 * 输入：context为只读CAN应用层上下文。
 * 输出：应用层未就绪或CAN0故障时返回true，否则返回false。
 */
bool A_Can_HasFault(const A_Can_Context *context);

#endif
