#ifndef GAS_COMMON_H
#define GAS_COMMON_H

#include <stdbool.h>
#include <stdint.h>

// 本文件是唯一允许没有同名 .c 文件的公共头文件，只保存跨层共享的宏、枚举和数据结构，不包含执行逻辑。

#define GAS_CYLINDER_COUNT                 (6U)      // 系统管理的气瓶总数。
#define GAS_PRESSURE_SENSOR_COUNT          (7U)      // 内部 Modbus 总线上的压力传感器总数，包含六瓶压力和一路总压力。
#define GAS_TOTAL_PRESSURE_SENSOR_INDEX    (6U)      // 总压力传感器在轮询地址表中的索引。
#define GAS_NO_ACTIVE_CYLINDER             (0xFFU)   // 当前没有工作瓶时使用的无效索引。
#define GAS_DEFAULT_EXTERNAL_COMM_MODE     (0U)      // 外部通讯默认模式，0表示CAN，1表示RS485/Modbus。

// EEPROM中没有有效记录时使用的运行参数默认值；外部Modbus可在六瓶全部停用的维护状态下修改并持久化。
#define GAS_DEFAULT_SWITCH_PRESSURE_MPA       (1.2F)    // 工作瓶触发低压切换的默认压力，单位 MPa。
#define GAS_DEFAULT_SWITCH_RELEASE_MPA        (1.3F)    // 低压确认退出回差的默认压力，单位 MPa。
#define GAS_DEFAULT_VALVE_PULL_IN_TIME_MS     (200UL)   // 电磁阀默认12 V强吸合时间，单位ms。
#define GAS_DEFAULT_READY_MIN_PRESSURE_MPA    (1.5F)    // 备用瓶允许投入使用的默认最低压力，单位 MPa。
#define GAS_DEFAULT_PRESSURE_MAX_MPA          (25.0F)   // 压力数据合法性检查的默认上限，单位 MPa。
#define GAS_DEFAULT_LOW_CONFIRM_TIME_MS       (1000UL)  // 工作瓶低压持续确认的默认时间，单位 ms。
#define GAS_DEFAULT_LOW_CONFIRM_SAMPLES       (3U)      // 低压切换需要累计的默认独立样本数。
#define GAS_DEFAULT_VALVE_CLOSE_WAIT_MS       (500UL)   // 关闭旧阀后的默认机械稳定时间，单位 ms。
#define GAS_DEFAULT_VALVE_OPEN_WAIT_MS        (500UL)   // 打开新阀后的默认机械稳定时间，单位 ms。
#define GAS_DEFAULT_PRESSURE_FRESH_MS         (2500UL)  // 压力样本保持有效的默认时限，单位 ms。
#define GAS_DEFAULT_LOW_WARNING_PRESSURE_MPA  (2.0F)    // 工作瓶进入低压警告状态的默认压力，单位 MPa。
#define GAS_DEFAULT_MANUAL_EXHAUST_TIME_MS    (5000UL)  // 串口屏人工排气默认持续时间，单位 ms。
#define GAS_DEFAULT_TEST_VALVE_MAX_TIME_MS    (60000UL) // 测试阀单次连续开启的默认安全上限，单位 ms。
#define GAS_VALVE_BOOST_MIN_INTERVAL_MS       (500UL)   // 同一气瓶组相邻两次 12 V 强吸合脉冲的最短间隔，单位 ms。

// 仅允许通过密码保护的串口屏参数页修改的三项安全范围；外部CAN和RS485不映射这些地址。
#define GAS_LOW_WARNING_PRESSURE_MIN_MPA      (1.5F)    // 低压警告压力允许设置的最小值，单位 MPa。
#define GAS_LOW_WARNING_PRESSURE_MAX_MPA      (5.0F)    // 低压警告压力允许设置的最大值，单位 MPa。
#define GAS_MANUAL_EXHAUST_TIME_MIN_MS        (3000UL)  // 人工排气允许设置的最短时间，单位 ms。
#define GAS_MANUAL_EXHAUST_TIME_MAX_MS        (65535UL) // 人工排气允许设置的最长时间，单位 ms，受EEPROM 16位字段限制。
#define GAS_TEST_VALVE_MAX_TIME_MIN_MS        (5000UL)  // 测试阀超时允许设置的最小值，单位 ms。
#define GAS_TEST_VALVE_MAX_TIME_MAX_MS        (60000UL) // 测试阀超时允许设置的最大值，单位 ms。

// Modbus 单寄存器参数的公共编码限制。
#define GAS_CONFIG_PRESSURE_SCALE             (1000.0F) // 压力参数寄存器使用 MPa 乘以 1000 的定点格式。
#define GAS_CONFIG_PRESSURE_MAX_ENCODED_MPA   (65.535F) // 单个无符号 16 位寄存器可编码的最大压力。
#define GAS_PRESSURE_DISPLAY_MAX_MPA          (99.999F) // 串口屏允许显示的诊断压力上限，超过该值仍按非法数据处理。
#define GAS_CONFIG_TIME_MAX_MS                (65535UL) // 单个无符号 16 位寄存器可编码的最大时间。
#define GAS_CONFIG_SAMPLE_MAX_COUNT           (255U)    // 低压样本计数受系统 8 位累计变量限制的最大值。

// 已确认：1～6 号气瓶压力传感器地址依次为 1～6，总压力传感器地址为 7。
#define GAS_SENSOR_ADDRESS_1               (1U)      // 1 号气瓶压力传感器的 Modbus 地址。
#define GAS_SENSOR_ADDRESS_2               (2U)      // 2 号气瓶压力传感器的 Modbus 地址。
#define GAS_SENSOR_ADDRESS_3               (3U)      // 3 号气瓶压力传感器的 Modbus 地址。
#define GAS_SENSOR_ADDRESS_4               (4U)      // 4 号气瓶压力传感器的 Modbus 地址。
#define GAS_SENSOR_ADDRESS_5               (5U)      // 5 号气瓶压力传感器的 Modbus 地址。
#define GAS_SENSOR_ADDRESS_6               (6U)      // 6 号气瓶压力传感器的 Modbus 地址。
#define GAS_SENSOR_ADDRESS_TOTAL           (7U)      // 总压力传感器的 Modbus 地址。

// 已确认：功能码 0x04，压力位于 PDU 地址 0，占两个寄存器。
// 数据为 IEEE-754 float32，高字寄存器在前，字节序 AB CD，数值单位按 MPa 使用。
#ifndef GAS_SENSOR_PROTOCOL_CONFIGURED
#define GAS_SENSOR_PROTOCOL_CONFIGURED     (1U)      // 压力传感器协议参数已经确认并允许启用轮询。
#endif
#define GAS_SENSOR_FUNCTION_CODE           (0x04U)   // 读取压力使用的 Modbus 读取输入寄存器功能码。
#define GAS_SENSOR_PRESSURE_REGISTER       (0x0000U) // 压力数据的 Modbus PDU 起始地址。
#define GAS_SENSOR_PRESSURE_REG_COUNT      (2U)      // 单个 float32 压力占用的寄存器数量。
#define GAS_SENSOR_UART_BAUDRATE           (9600UL)  // 内部压力传感器总线波特率。
#define GAS_SENSOR_UART_MAX_ERROR_X1000    (5000UL)  // FSP 波特率计算允许的最大误差参数。
#define GAS_SENSOR_RESPONSE_TIMEOUT_MS     (200UL)   // 单次传感器请求的响应超时时间，单位 ms。
#define GAS_SENSOR_POLL_INTERVAL_MS        (100UL)   // 相邻两次传感器轮询的启动间隔，单位 ms。
#define GAS_SENSOR_COMM_WARN_COUNT         (3U)      // 连续通信失败达到警告状态的次数。
#define GAS_SENSOR_COMM_FAULT_COUNT        (10U)     // 连续通信失败判定传感器故障的次数。

// 压力数据格式：
// 0：16 位无符号整数原始值 × GAS_SENSOR_PRESSURE_SCALE。
// 1：16 位有符号整数原始值 × GAS_SENSOR_PRESSURE_SCALE。
// 2：IEEE-754 float32，高字寄存器在前，字节序 ABCD。
// 3：IEEE-754 float32，低字寄存器在前，字节序 CDAB。
#define GAS_SENSOR_DATA_FORMAT             (2U)      // 压力格式编号 2 表示 float32、AB CD 字节序。
#define GAS_SENSOR_PRESSURE_SCALE          (1.0F)    // 非浮点格式使用的压力换算比例，当前保持为 1。

// 阀门 GPIO 总使能。实际输出极性和线束映射经实机核对前保持为 0。
// 置 0 时，软件允许执行关阀命令，但拒绝任何开阀命令。
#ifndef GAS_BOARD_VALVE_OUTPUTS_ENABLED
#define GAS_BOARD_VALVE_OUTPUTS_ENABLED    (1U)      // 实机阀门输出总使能，0 时禁止所有开阀命令。
#endif
#define GAS_BOARD_VALVE_ACTIVE_LEVEL       (1U)      // 阀门及 VAL_Px 控制信号的有效 GPIO 电平。

// 压力传感器 RS485 使用 PRE_EN 同时控制 SP3485 的低有效接收使能和高有效发送使能。
// PRE_EN 为高电平时发送、低电平时接收；PRE_RES 与 SCI0_485RES 仅控制 120 Ω 匹配电阻。
#define GAS_SENSOR_RS485_TX_LEVEL           (1U)      // PRE_EN 切换到发送方向时的电平。
#define GAS_SENSOR_RS485_RX_LEVEL           (0U)      // PRE_EN 切换到接收方向时的电平。
#define GAS_RS485_TERMINATION_ENABLE_LEVEL  (1U)      // RS485 匹配电阻使能信号的有效电平。

// 气源控制运行参数；应用层持有当前实例，并从 AT24C256 加载或使用默认值。
typedef struct
{
    float switch_pressure_mpa;       // 工作瓶触发低压切换的压力，单位 MPa。
    float switch_release_mpa;        // 低压确认退出回差的压力，单位 MPa。
    uint32_t valve_pull_in_time_ms;   // 电磁阀使用 12 V 强吸合的时间，单位 ms。
    float ready_min_pressure_mpa;     // 气瓶允许作为备用瓶的最低压力，单位 MPa。
    float pressure_max_mpa;           // 压力传感器数据合法性检查上限，单位 MPa。
    uint32_t low_confirm_time_ms;     // 工作瓶低压持续确认时间，单位 ms。
    uint16_t low_confirm_samples;     // 触发切瓶需要累计的独立低压样本数。
    uint32_t valve_close_wait_ms;     // 关闭旧阀后的机械稳定等待时间，单位 ms。
    uint32_t valve_open_wait_ms;      // 打开新阀后的机械稳定等待时间，单位 ms。
    uint32_t pressure_fresh_ms;       // 压力数据保持有效的新鲜度时限，单位 ms。
    float low_warning_pressure_mpa;   // 工作瓶进入低压警告状态的压力阈值，单位 MPa。
    uint32_t manual_exhaust_time_ms;  // 串口屏人工排气阀单次持续开启时间，单位 ms。
    uint32_t test_valve_max_time_ms;  // 测试阀单次连续开启的自动关闭上限，单位 ms。
} Gas_Config;

// 外部上位机通讯模式；枚举值同时作为EEPROM记录和后续串口屏选择值。
typedef enum
{
    GAS_EXTERNAL_COMM_CAN = 0,    // CAN0通讯，250 kbit/s、29位扩展数据帧。
    GAS_EXTERNAL_COMM_RS485 = 1   // SCI0/RS485 Modbus RTU从站通讯。
} gas_external_comm_mode_t;

// CAN与RS485/Modbus共用的启动和停止命令值。
typedef enum
{
    GAS_EXTERNAL_COMMAND_NONE = 0,       // 当前没有待处理命令。
    GAS_EXTERNAL_COMMAND_START_AUTO = 1, // 启动自动供气。
    GAS_EXTERNAL_COMMAND_STOP = 2        // 停止自动供气并关闭全部阀门。
} gas_external_command_t;

// CAN与RS485/Modbus共用的命令执行结果值。
typedef enum
{
    GAS_EXTERNAL_RESULT_IDLE = 0,            // 当前没有待处理命令。
    GAS_EXTERNAL_RESULT_PENDING = 1,         // 命令已经接收，等待业务层执行。
    GAS_EXTERNAL_RESULT_SUCCESS = 2,         // 命令执行成功。
    GAS_EXTERNAL_RESULT_INVALID_COMMAND = 3, // 命令值不受支持。
    GAS_EXTERNAL_RESULT_REJECTED = 4         // 当前系统状态拒绝执行命令。
} gas_external_result_t;

// CAN与RS485/Modbus共用的参数提交结果值。
typedef enum
{
    GAS_EXTERNAL_CONFIG_IDLE = 0,             // 当前没有参数处理请求。
    GAS_EXTERNAL_CONFIG_PENDING = 1,          // 参数等待业务层保存和应用。
    GAS_EXTERNAL_CONFIG_SUCCESS = 2,          // 参数保存并应用成功。
    GAS_EXTERNAL_CONFIG_INVALID_RANGE = 3,    // 至少一个参数超出允许范围。
    GAS_EXTERNAL_CONFIG_INVALID_RELATION = 4, // 参数之间的逻辑关系错误。
    GAS_EXTERNAL_CONFIG_STORAGE_FAILED = 5,   // EEPROM保存或读回失败。
    GAS_EXTERNAL_CONFIG_SYSTEM_BUSY = 6,      // 六瓶未全部停用或仍存在开阀命令。
    GAS_EXTERNAL_CONFIG_INVALID_KEY = 7       // 参数提交键值错误。
} gas_external_config_result_t;

// CAN与RS485/Modbus共用的日志读取结果值。
typedef enum
{
    GAS_EXTERNAL_LOG_IDLE = 0,            // 当前没有日志读取操作。
    GAS_EXTERNAL_LOG_PENDING = 1,         // 请求已接收，等待EEPROM读取。
    GAS_EXTERNAL_LOG_SUCCESS = 2,         // 日志数据窗口已经更新。
    GAS_EXTERNAL_LOG_INVALID_COMMAND = 3, // 日志命令值无效。
    GAS_EXTERNAL_LOG_INVALID_INDEX = 4,   // 日志逻辑序号无效。
    GAS_EXTERNAL_LOG_READ_FAILED = 5,     // EEPROM读取或记录校验失败。
    GAS_EXTERNAL_LOG_BUSY = 6             // 上一条日志请求尚未完成。
} gas_external_log_result_t;

// 气瓶业务状态；V1.03在末尾追加状态7，保留原有1～6的CAN、Modbus和EEPROM编码兼容性。
typedef enum
{
    GAS_CYL_INIT = 1,       // 初始化：等待有效压力和人员测试合格确认并执行状态判断。
    GAS_CYL_READY = 2,      // 待用：压力不低于备用瓶最低压力且测试合格，可按顺序投入供气。
    GAS_CYL_ACTIVE = 3,     // 使用：该路供气阀开启并作为当前工作瓶。
    GAS_CYL_LOW_REPLACE = 4,// 低压待换：压力不足，禁止自动选用并持续等待压力恢复。
    GAS_CYL_LOW_WARNING = 5,// 低压警告：工作瓶低于警告阈值但尚未完成自动切换。
    GAS_CYL_DISABLED = 6,   // 停用：维护人员停用该路，三个电磁阀必须全部关闭。
    GAS_CYL_WAIT_TEST = 7   // 待测试：压力已经恢复合格，等待工作人员确认测试通过后进入待用。
} gas_cylinder_state_t;

// 压力数据质量，用于区分有效数据、陈旧数据和物理量程异常。
typedef enum
{
    GAS_PRESSURE_INVALID = 0,  // 尚未取得有效压力或通信连续失败。
    GAS_PRESSURE_VALID,        // 压力已通过协议解析、量程和新鲜度检查。
    GAS_PRESSURE_STALE,        // 压力曾经有效，但已经超过新鲜度时限。
    GAS_PRESSURE_OUT_OF_RANGE  // 压力超出配置上限但仍可用于红色诊断显示，不参与自动控制。
} gas_pressure_quality_t;

// 系统内部运行状态；V1.08不提供人工切换，STOPPED只用于严重故障锁定。
typedef enum
{
    GAS_MODE_STOPPED = 0, // 内部故障锁定，全部阀门保持关闭并等待排障重启。
    GAS_MODE_AUTO         // 系统自动监测压力并执行顺序切瓶。
} gas_mode_t;

// 自动切瓶子状态，用于实现不阻塞主循环的“低压确认、先关后开”流程。
typedef enum
{
    GAS_SWITCH_IDLE = 0,    // 正常供气并等待低压条件。
    GAS_SWITCH_LOW_CONFIRM, // 对低压进行时间和样本数量确认。
    GAS_SWITCH_FIND_NEXT,   // 按固定顺序查找下一只合格备用瓶。
    GAS_SWITCH_CLOSE_OLD,   // 关闭原工作瓶供气阀。
    GAS_SWITCH_DEAD_TIME,   // 等待旧阀关断稳定时间，两路均不供气。
    GAS_SWITCH_OPEN_NEW,    // 打开新工作瓶供气阀。
    GAS_SWITCH_VERIFY_NEW,  // 等待新阀开启稳定时间。
    GAS_SWITCH_NO_BACKUP    // 没有合格备用瓶，保留旧瓶供气并报警。
} gas_switch_state_t;

// 系统报警位，每一位对应一种可同时存在的异常或操作提示。
enum
{
    GAS_ALARM_NONE                = 0UL,        // 无报警。
    GAS_ALARM_NO_BACKUP           = (1UL << 0), // 没有可用备用瓶。
    GAS_ALARM_ACTIVE_SENSOR       = (1UL << 1), // 当前工作瓶压力数据异常。
    GAS_ALARM_SENSOR_COMM         = (1UL << 2), // 至少一个压力传感器通信异常。
    GAS_ALARM_VALVE_INTERLOCK     = (1UL << 3), // 阀门命令违反供排气互锁条件。
    GAS_ALARM_MULTIPLE_SUPPLY     = (1UL << 4), // 检测到多个供气阀同时开启。
    GAS_ALARM_MANUAL_VALVE_ABORTED= (1UL << 5), // 人工排气或测试阀操作被互锁拒绝或异常中止。
    GAS_ALARM_EXTERNAL_CAN        = (1UL << 6), // 外部CAN初始化、总线或收发队列异常。
    GAS_ALARM_PLATFORM_NOT_READY  = (1UL << 7), // 硬件平台或安全使能尚未就绪。
    GAS_ALARM_STORAGE             = (1UL << 8), // AT24C256 初始化或访问异常。
    GAS_ALARM_EXTERNAL_MODBUS     = (1UL << 9), // 外部 SCI0 Modbus 初始化或通信异常。
    GAS_ALARM_HMI_COMM            = (1UL << 10) // SCI9 串口屏初始化或通信异常。
};

// 单只气瓶的业务数据，集中保存压力、通信、阀门命令和生命周期状态。
typedef struct
{
    float pressure_mpa;                      // 最近一次成功解析且可显示的压力值，单位 MPa。
    gas_pressure_quality_t pressure_quality; // 当前压力数据质量。
    uint32_t pressure_timestamp_ms;           // 最近一次成功解析压力样本的毫秒时间戳。
    uint8_t modbus_address;                   // 对应压力传感器的 Modbus 从站地址。
    uint8_t comm_fail_count;                  // 连续通信或数据解析失败次数。
    bool supply_cmd;                          // 供气阀的软件命令状态，true 表示开阀。
    bool exhaust_cmd;                         // 排气阀的软件命令状态，true 表示开阀。
    bool test_cmd;                            // 测试阀的软件命令状态，true 表示开阀。
    bool qualification_passed;                // 人员确认的气瓶测试合格标志，true才允许从待测试进入待用。
    uint8_t recovery_sample_count;             // 低压待换后累计的独立合格压力样本数，用于确认新瓶压力已经稳定恢复。
    uint32_t recovery_sample_timestamp_ms;     // 最近一次计入恢复确认的压力样本时间戳，避免同一样本被任务循环重复累计。
    uint32_t exhaust_deadline_ms;             // 人工排气阀按照当前运行参数自动关闭的截止时间。
    uint32_t test_deadline_ms;                // 测试阀按照当前运行参数自动关闭的截止时间。
    gas_cylinder_state_t state;               // 当前气瓶业务状态。
} Gas_Cylinder;

// 总压力传感器运行数据，仅用于采集、显示和通信诊断，不参与六瓶状态判断及自动切换。
typedef struct
{
    float pressure_mpa;                      // 最近一次成功解析且可显示的总压力值，单位 MPa。
    gas_pressure_quality_t pressure_quality; // 总压力数据质量。
    uint32_t pressure_timestamp_ms;           // 最近一次成功解析总压力样本的毫秒时间戳。
    uint8_t modbus_address;                   // 总压力传感器的 Modbus 从站地址，当前为 7。
    uint8_t comm_fail_count;                  // 总压力传感器连续通信或数据解析失败次数。
} Gas_Total_Pressure;

// 系统日期时间，由串口屏内部 RTC 提供，供后续日志记录和外部通信统一使用。
typedef struct
{
    uint16_t year;              // 完整年份，范围 2000～2099。
    uint8_t month;              // 月，范围 1～12。
    uint8_t day;                // 日，范围 1～31，并按实际月份校验。
    uint8_t week;               // 星期，0 表示星期日，1～6 表示星期一至星期六。
    uint8_t hour;               // 时，24 小时制，范围 0～23。
    uint8_t minute;             // 分，范围 0～59。
    uint8_t second;             // 秒，范围 0～59。
    uint32_t source_timestamp_ms; // 最近一次收到串口屏 RTC 响应时的系统毫秒时间戳。
    bool valid;                 // 日期时间已经通过 BCD、范围和日期合法性校验时为 true。
} Gas_Date_Time;

// 六瓶气源系统的业务状态，由应用层持有并通过参数传递给功能层处理。
typedef struct
{
    Gas_Cylinder cylinder[GAS_CYLINDER_COUNT]; // 六只气瓶的独立状态数据。
    Gas_Total_Pressure total_pressure;          // 地址 7 总压力传感器的独立运行数据。
    Gas_Date_Time date_time;                    // 从串口屏 RTC 周期读取的系统日期时间。
    gas_mode_t mode;                           // 当前系统运行模式。
    gas_switch_state_t switch_state;           // 自动切瓶状态机当前状态。
    uint8_t active_index;                      // 当前工作瓶索引，0～5；无工作瓶时为 GAS_NO_ACTIVE_CYLINDER。
    uint8_t switch_old_index;                  // 本次切换的原工作瓶索引。
    uint8_t switch_new_index;                  // 本次切换的目标备用瓶索引。
    uint8_t low_sample_count;                  // 当前低压确认累计的独立有效样本数。
    uint32_t low_last_sample_ms;               // 已计入低压确认的最后一个压力样本时间戳。
    uint32_t switch_enter_ms;                  // 进入当前切瓶子状态时的毫秒时间戳。
    uint32_t low_start_ms;                     // 本次低压确认开始的毫秒时间戳。
    uint32_t alarm_bits;                       // 当前系统报警位集合。
    bool platform_ready;                       // 硬件平台和安全使能是否允许进入自动模式。
} Gas_System;

#endif
