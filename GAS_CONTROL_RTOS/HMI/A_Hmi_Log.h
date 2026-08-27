#ifndef A_HMI_LOG_H
#define A_HMI_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "A_Hmi.h"
#include "../MyUnitFile/A_Gas_Log.h"

#define A_HMI_EVENT_LOG_PAGE_ID                (2U)   // 事件日志查询画面的画面ID。
#define A_HMI_EVENT_LOG_QUERY_BUTTON_ID        (61U)  // “刷新事件”按钮控件ID。
#define A_HMI_EVENT_LOG_RECORD_CONTROL_ID      (62U)  // 事件日志当前页数据记录控件ID。
#define A_HMI_EVENT_LOG_STATUS_CONTROL_ID      (63U)  // 事件日志扫描及结果文本控件ID。
#define A_HMI_EVENT_LOG_VISIBLE_COUNT          (15U)  // 事件日志每页显示的逻辑记录数量。
#define A_HMI_EVENT_LOG_LATEST_BUTTON_ID       (119U) // 事件日志“最新页”按钮控件ID。
#define A_HMI_EVENT_LOG_PREVIOUS_BUTTON_ID     (120U) // 事件日志“上一页”按钮控件ID。
#define A_HMI_EVENT_LOG_NEXT_BUTTON_ID         (121U) // 事件日志“下一页”按钮控件ID。
#define A_HMI_EVENT_LOG_PAGE_INFO_CONTROL_ID   (122U) // 事件日志页码及总条数文本控件ID。

#define A_HMI_REGULAR_LOG_PAGE_ID              (3U)   // 常规日志查询画面的画面ID。
#define A_HMI_REGULAR_LOG_QUERY_BUTTON_ID      (65U)  // “刷新常规”按钮控件ID。
#define A_HMI_REGULAR_LOG_CONTENT_CONTROL_ID   (66U)  // 常规日志当前页时间与内容两列控件ID。
#define A_HMI_REGULAR_LOG_STATUS_CONTROL_ID    (67U)  // 常规日志扫描及结果文本控件ID。
#define A_HMI_REGULAR_LOG_VISIBLE_COUNT        (10U)  // 常规日志每页显示的逻辑记录数量。
#define A_HMI_REGULAR_LOG_LATEST_BUTTON_ID     (123U) // 常规日志“最新页”按钮控件ID。
#define A_HMI_REGULAR_LOG_PREVIOUS_BUTTON_ID   (124U) // 常规日志“上一页”按钮控件ID。
#define A_HMI_REGULAR_LOG_NEXT_BUTTON_ID       (125U) // 常规日志“下一页”按钮控件ID。
#define A_HMI_REGULAR_LOG_PAGE_INFO_CONTROL_ID (126U) // 常规日志页码及总条数文本控件ID。

#define A_HMI_LOG_ROW_MAX_SIZE                 (180U) // 单条数据记录GBK文本的最大字节数。
#define A_HMI_LOG_STATUS_MAX_SIZE              (48U)  // 扫描、结果和分页状态文本的最大字节数。
#define A_HMI_LOG_PROGRESS_INTERVAL            (32U)  // 后台索引每扫描指定原始记录数更新一次进度。
#define A_HMI_LOG_INDEX_CAPACITY                A_GAS_LOG_RECORD_CAPACITY // 单类型日志索引最大数量。

// 串口屏日志查询类型，用于区分事件表格和常规表格的数据来源、页面容量及格式。
typedef enum
{
    A_HMI_LOG_QUERY_EVENT = 0, // 查询气瓶状态变化事件，每页显示15条。
    A_HMI_LOG_QUERY_REGULAR    // 查询半小时常规记录，每页显示10条且每条占两行。
} A_Hmi_Log_Query_Type;

// 串口屏日志翻页命令，页码从最新端开始递增。
typedef enum
{
    A_HMI_LOG_PAGE_LATEST = 0, // 返回第1页，也就是当前查询快照中的最新日志页。
    A_HMI_LOG_PAGE_PREVIOUS,   // 页码减1，查看比当前页更新的一页。
    A_HMI_LOG_PAGE_NEXT        // 页码加1，查看比当前页更早的一页。
} A_Hmi_Log_Page_Command;

// 串口屏分页日志状态，用于把清表、索引、页读取和逐行发送拆分到多个控制周期。
typedef enum
{
    A_HMI_LOG_QUERY_IDLE = 0,   // 当前没有索引或页面发送任务。
    A_HMI_LOG_QUERY_CLEAR,      // 刷新查询开始时清空目标记录控件。
    A_HMI_LOG_QUERY_SCAN,       // 从最新端分步扫描EEPROM并建立当前类型的RAM索引。
    A_HMI_LOG_QUERY_PAGE_CLEAR, // 翻页前清空上一页记录。
    A_HMI_LOG_QUERY_PAGE_READ,  // 根据RAM索引读取当前页的一条EEPROM日志。
    A_HMI_LOG_QUERY_PAGE_ROWS,  // 格式化并发送当前日志的一行或两行文本。
    A_HMI_LOG_QUERY_PAGE_INFO,  // 发送当前页码、总页数、统计状态和日志条数。
    A_HMI_LOG_QUERY_STATUS      // 发送扫描结果、日志变化、RTC无效或读取错误提示。
} A_Hmi_Log_Query_State;

// 串口屏分页日志应用上下文，保存模块引用、单类型索引、查询快照和当前页面发送进度。
typedef struct
{
    A_Hmi_Context *hmi;                                       // 已初始化的串口屏应用实例。
    A_Gas_Log_Context *log;                                   // AT24C256循环日志实例。
    const Gas_System *system;                                 // 气源系统只读实例，用于判断RTC有效性。
    A_Hmi_Log_Query_State state;                              // 当前非阻塞查询或分页步骤。
    A_Hmi_Log_Query_Type query_type;                          // RAM索引及当前页面对应的日志类型。
    A_Hmi_Log_Query_Type pending_type;                        // 最近一次刷新请求指定的日志类型。
    uint32_t snapshot_next_sequence;                          // 建立索引时保存的下一流水号，用于检测日志变化。
    uint16_t total_count;                                     // 查询快照包含的全部原始日志数量。
    uint16_t matched_count;                                   // 已扫描并写入RAM索引的当前类型日志数量。
    uint16_t scanned_count;                                   // 当前快照中已经读取并校验的原始日志数量。
    uint16_t scan_logical_index;                              // 反向扫描边界，下一条读取索引为该值减1。
    uint16_t log_index[A_HMI_LOG_INDEX_CAPACITY];             // 当前类型从新到旧排列的EEPROM逻辑索引，约占2KB RAM。
    uint16_t current_page;                                    // 当前已显示的零起始页码，第1页在内部保存为0。
    uint16_t requested_page;                                  // 最近一次翻页命令要求显示的零起始页码。
    uint16_t page_start_index;                                // 当前页在log_index数组中的起始下标。
    uint16_t page_end_index;                                  // 当前页在log_index数组中的结束下标，不包含该下标。
    uint16_t page_cursor;                                     // 当前页下一条待读取记录在log_index中的下标。
    uint16_t sent_count;                                      // 当前页已经完整发送的逻辑记录数量。
    uint8_t regular_line_phase;                               // 常规日志发送阶段：0为压力行，1为状态行。
    uint8_t current_record[A_GAS_LOG_RECORD_SIZE];             // 当前根据分页索引读取的32字节日志。
    char row[A_HMI_LOG_ROW_MAX_SIZE];                         // 当前已经格式化但可能尚未发出的GBK数据行。
    size_t row_length;                                        // 当前数据行的有效字节数。
    bool current_record_ready;                                // current_record中是否保存有效日志。
    bool row_ready;                                           // row中是否保存等待发送的有效行。
    bool request_pending;                                     // 是否收到新的事件或常规日志刷新请求。
    bool page_request_pending;                                // 是否收到最新页、上一页或下一页请求。
    bool read_failed;                                         // 本次查询是否发生EEPROM读取或记录校验失败。
    bool snapshot_changed;                                    // 索引建立后是否有新日志产生，需要人员刷新。
    bool progress_pending;                                    // 是否有一条后台索引进度文本等待发送。
    bool index_complete;                                      // 当前类型的全部RAM索引是否已经建立完成。
    bool cache_valid;                                         // 当前RAM索引是否对应一个可用的日志快照。
    bool page_rendered;                                       // 当前记录控件中是否已经显示一页有效数据。
    bool table_is_clear;                                      // 当前记录控件是否已经清空且尚未追加页面数据。
    bool ready;                                               // HMI和日志模块引用是否已经配置。
} A_Hmi_Log_Context;

/*
 * 函数名：A_HmiLog_Init。
 * 说明：初始化串口屏分页日志实例，并关联HMI、EEPROM日志和气源系统状态。
 * 输入：context为日志查询上下文；hmi为HMI应用实例；log为EEPROM日志实例；system为气源系统只读实例。
 * 输出：参数有效时返回true，否则返回false。
 */
bool A_HmiLog_Init(A_Hmi_Log_Context *context,
                   A_Hmi_Context *hmi,
                   A_Gas_Log_Context *log,
                   const Gas_System *system);

/*
 * 函数名：A_HmiLog_Request。
 * 说明：请求刷新指定类型日志，建立从新到旧的RAM索引并优先显示第1页。
 * 输入：context为日志查询上下文；query_type为事件日志或常规日志类型。
 * 输出：模块已初始化且查询类型有效时返回true，否则返回false。
 */
bool A_HmiLog_Request(A_Hmi_Log_Context *context,
                      A_Hmi_Log_Query_Type query_type);

/*
 * 函数名：A_HmiLog_RequestPage。
 * 说明：请求显示指定日志类型的最新页、上一页或下一页；没有对应索引时自动从第1页开始刷新。
 * 输入：context为日志查询上下文；query_type为事件或常规日志；command为翻页命令。
 * 输出：请求参数有效并已被接收时返回true，否则返回false。
 */
bool A_HmiLog_RequestPage(A_Hmi_Log_Context *context,
                          A_Hmi_Log_Query_Type query_type,
                          A_Hmi_Log_Page_Command command);

/*
 * 函数名：A_HmiLog_Task。
 * 说明：非阻塞执行索引建立、当前页读取、记录格式转换和串口发送，每次调用最多读一条或发一行。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：无；索引和分页进度保存在context中。
 */
void A_HmiLog_Task(A_Hmi_Log_Context *context);

/*
 * 函数名：A_HmiLog_IsBusy。
 * 说明：查询日志索引或当前页发送是否仍在进行，用于暂缓普通监控画面刷新以避免争用SCI9。
 * 输入：context为只读日志查询上下文。
 * 输出：存在待处理请求或查询尚未结束时返回true，否则返回false。
 */
bool A_HmiLog_IsBusy(const A_Hmi_Log_Context *context);

#endif
