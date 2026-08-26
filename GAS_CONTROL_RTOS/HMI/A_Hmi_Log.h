#ifndef A_HMI_LOG_H
#define A_HMI_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "A_Hmi.h"
#include "../MyUnitFile/A_Gas_Log.h"

#define A_HMI_EVENT_LOG_PAGE_ID               (2U)   // 事件日志查询画面的画面ID。
#define A_HMI_EVENT_LOG_QUERY_BUTTON_ID       (61U)  // “刷新事件”按钮控件ID。
#define A_HMI_EVENT_LOG_RECORD_CONTROL_ID     (62U)  // 事件日志数据记录控件ID。
#define A_HMI_EVENT_LOG_STATUS_CONTROL_ID     (63U)  // 事件日志查询结果文本控件ID。
#define A_HMI_EVENT_LOG_VISIBLE_COUNT         (15U)  // 事件表格一屏可见的记录行数。
#define A_HMI_EVENT_LOG_MAX_RECORD_COUNT      A_GAS_LOG_RECORD_CAPACITY // 事件表格可滑动浏览的最大记录数。

#define A_HMI_REGULAR_LOG_PAGE_ID             (3U)   // 常规日志查询画面的画面ID。
#define A_HMI_REGULAR_LOG_QUERY_BUTTON_ID     (65U)  // “刷新常规”按钮控件ID。
#define A_HMI_REGULAR_LOG_CONTENT_CONTROL_ID  (66U)  // 常规日志时间与内容两列统一控件ID。
#define A_HMI_REGULAR_LOG_STATUS_CONTROL_ID   (67U)  // 常规日志查询结果文本控件ID。
#define A_HMI_REGULAR_LOG_VISIBLE_COUNT       (10U)  // 常规表格一屏可见的逻辑记录数量。
#define A_HMI_REGULAR_LOG_MAX_ROW_COUNT       (A_GAS_LOG_RECORD_CAPACITY * 2U) // 常规表格可保存的压力行和状态行总数。

#define A_HMI_LOG_ROW_MAX_SIZE                (180U) // 单条数据记录GBK文本的最大字节数。
#define A_HMI_LOG_PROGRESS_INTERVAL           (32U)  // 扫描期间每处理指定条数后刷新一次进度。

// 串口屏日志查询类型，用于区分事件表格和常规表格的数据来源及格式。
typedef enum
{
    A_HMI_LOG_QUERY_EVENT = 0, // 查询并滑动显示当前全部气瓶状态变化事件。
    A_HMI_LOG_QUERY_REGULAR    // 查询并滑动显示当前全部半小时常规记录。
} A_Hmi_Log_Query_Type;

// 串口屏日志查询状态，用于把清表、扫描、读记录和逐行发送拆分到多个控制周期。
typedef enum
{
    A_HMI_LOG_QUERY_IDLE = 0, // 当前没有查询任务。
    A_HMI_LOG_QUERY_CLEAR,    // 等待发送清空目标数据记录控件指令。
    A_HMI_LOG_QUERY_SCAN,     // 从最新端分步扫描EEPROM快照并筛选指定类型。
    A_HMI_LOG_QUERY_ROWS,     // 分步格式化并发送当前匹配记录。
    A_HMI_LOG_QUERY_STATUS    // 等待发送本次查询结果摘要。
} A_Hmi_Log_Query_State;

// 串口屏日志查询应用上下文，保存模块引用、类型过滤结果、日志快照和当前待发数据行。
typedef struct
{
    A_Hmi_Context *hmi;                                      // 已初始化的串口屏应用实例。
    A_Gas_Log_Context *log;                                  // AT24C256循环日志实例。
    const Gas_System *system;                                // 气源系统只读实例，用于提示RTC无效导致的日志暂停。
    A_Hmi_Log_Query_State state;                             // 当前非阻塞查询步骤。
    A_Hmi_Log_Query_Type query_type;                         // 当前正在执行的查询类型。
    A_Hmi_Log_Query_Type pending_type;                       // 最近一次按钮请求指定的查询类型。
    uint32_t snapshot_next_sequence;                         // 查询开始时的下一流水号，用于检测查询期间日志变化。
    uint16_t total_count;                                    // 查询开始时EEPROM中的全部有效日志数量。
    uint16_t matched_count;                                  // 从最新端反向扫描后找到的当前类型日志数量。
    uint16_t scanned_count;                                  // 当前快照中已经读取并校验的原始日志数量。
    uint16_t scan_logical_index;                             // 反向扫描边界，下一条读取索引为该值减1。
    uint16_t sent_count;                                     // 已完整发送到串口屏的逻辑记录数量。
    uint8_t regular_line_phase;                              // 常规记录发送阶段：0压力行，1状态行。
    uint8_t current_record[A_GAS_LOG_RECORD_SIZE];            // 当前已读取且需要格式化的32字节日志。
    char row[A_HMI_LOG_ROW_MAX_SIZE];                        // 当前已经格式化但可能尚未发出的GBK数据行。
    size_t row_length;                                       // 当前数据行的有效字节数。
    bool current_record_ready;                               // current_record中是否保存有效日志。
    bool row_ready;                                          // row中是否保存等待发送的有效行。
    bool request_pending;                                    // 是否收到新的事件或常规日志查询请求。
    bool read_failed;                                        // 本次查询是否发生EEPROM读取或记录校验失败。
    bool snapshot_changed;                                   // 查询期间日志是否新增，用于提示用户重新刷新。
    bool progress_pending;                                   // 是否有一条扫描进度文本等待发送。
    bool ready;                                              // HMI和日志模块引用是否已经配置。
} A_Hmi_Log_Context;

/*
 * 函数名：A_HmiLog_Init。
 * 说明：初始化串口屏日志查询实例，并关联HMI、EEPROM日志和气源系统状态。
 * 输入：context为日志查询上下文；hmi为HMI应用实例；log为EEPROM日志实例；system为气源系统只读实例。
 * 输出：参数有效时返回true，否则返回false。
 */
bool A_HmiLog_Init(A_Hmi_Log_Context *context,
                   A_Hmi_Context *hmi,
                   A_Gas_Log_Context *log,
                   const Gas_System *system);

/*
 * 函数名：A_HmiLog_Request。
 * 说明：请求按指定类型重新加载全部日志，实际EEPROM读取和串口发送由任务分步完成。
 * 输入：context为日志查询上下文；query_type为事件日志或常规日志类型。
 * 输出：模块已初始化且查询类型有效时返回true，否则返回false。
 */
bool A_HmiLog_Request(A_Hmi_Log_Context *context,
                      A_Hmi_Log_Query_Type query_type);

/*
 * 函数名：A_HmiLog_Task。
 * 说明：非阻塞执行清表、类型扫描、记录格式转换和串口发送，每次调用最多读取或发送一行数据。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：无；查询进度保存在context中。
 */
void A_HmiLog_Task(A_Hmi_Log_Context *context);

/*
 * 函数名：A_HmiLog_IsBusy。
 * 说明：查询日志刷新是否仍在进行，用于暂缓普通监控画面刷新以避免争用SCI9。
 * 输入：context为只读日志查询上下文。
 * 输出：存在待处理请求或查询尚未结束时返回true，否则返回false。
 */
bool A_HmiLog_IsBusy(const A_Hmi_Log_Context *context);

#endif
