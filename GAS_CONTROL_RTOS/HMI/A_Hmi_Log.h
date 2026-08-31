/*
 * Version: v1.11
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明串口屏日志查询条件、状态机上下文和分页接口。
 */

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
#define A_HMI_EVENT_TO_REGULAR_BUTTON_ID       (69U)  // 事件页切换并查询常规日志的按钮ID。

#define A_HMI_REGULAR_LOG_PAGE_ID              (3U)   // 常规日志查询画面的画面ID。
#define A_HMI_REGULAR_LOG_QUERY_BUTTON_ID      (65U)  // “刷新常规”按钮控件ID。
#define A_HMI_REGULAR_LOG_CONTENT_CONTROL_ID   (66U)  // 常规日志当前页时间与内容两列控件ID。
#define A_HMI_REGULAR_LOG_STATUS_CONTROL_ID    (67U)  // 常规日志扫描及结果文本控件ID。
#define A_HMI_REGULAR_LOG_VISIBLE_COUNT        (10U)  // 常规日志每页显示的逻辑记录数量。
#define A_HMI_REGULAR_LOG_LATEST_BUTTON_ID     (123U) // 常规日志“最新页”按钮控件ID。
#define A_HMI_REGULAR_LOG_PREVIOUS_BUTTON_ID   (124U) // 常规日志“上一页”按钮控件ID。
#define A_HMI_REGULAR_LOG_NEXT_BUTTON_ID       (125U) // 常规日志“下一页”按钮控件ID。
#define A_HMI_REGULAR_LOG_PAGE_INFO_CONTROL_ID (126U) // 常规日志页码及总条数文本控件ID。
#define A_HMI_REGULAR_TO_EVENT_BUTTON_ID       (70U)  // 常规页切换并查询事件日志的按钮ID。

#define A_HMI_LOG_FILTER_PAGE_ID               (6U)   // 日志条件查询画面ID。
#define A_HMI_LOG_FILTER_START_DATE_ID         (127U) // 开始日期输入，格式YYYYMMDD。
#define A_HMI_LOG_FILTER_START_TIME_ID         (128U) // 开始时间输入，格式HHMMSS。
#define A_HMI_LOG_FILTER_END_DATE_ID           (129U) // 结束日期输入，格式YYYYMMDD。
#define A_HMI_LOG_FILTER_END_TIME_ID           (130U) // 结束时间输入，格式HHMMSS。
#define A_HMI_LOG_FILTER_ALL_TIME_BUTTON_ID    (131U) // 全部时间开关：1忽略起止时间。
#define A_HMI_LOG_FILTER_CYLINDER_TRIGGER_ID   (132U) // 气瓶下拉菜单触发按钮，只弹出菜单不改变条件。
#define A_HMI_LOG_FILTER_CYLINDER_TEXT_ID      (133U) // 当前气瓶筛选条件文本，由屏端菜单选中值或MCU回写。
#define A_HMI_LOG_FILTER_STATE_TRIGGER_ID      (134U) // 状态下拉菜单触发按钮，只弹出菜单不改变条件。
#define A_HMI_LOG_FILTER_STATE_TEXT_ID         (135U) // 当前进入状态筛选条件文本，由屏端菜单选中值或MCU回写。
#define A_HMI_LOG_FILTER_CYLINDER_MENU_ID      (149U) // 气瓶下拉菜单控件，选中索引0～6对应全部及1～6号瓶。
#define A_HMI_LOG_FILTER_STATE_MENU_ID         (150U) // 状态下拉菜单控件，选中索引0～7对应全部及七种状态。
#define A_HMI_LOG_FILTER_EVENT_BUTTON_ID       (136U) // 按当前条件查询事件日志。
#define A_HMI_LOG_FILTER_REGULAR_BUTTON_ID     (137U) // 按当前时间条件查询常规日志。
#define A_HMI_LOG_FILTER_RESET_BUTTON_ID       (138U) // 恢复全部时间、全部气瓶和全部状态。
#define A_HMI_LOG_FILTER_STATUS_TEXT_ID        (139U) // 条件输入和查询说明文本。
#define A_HMI_LOG_FILTER_BACK_BUTTON_ID        (140U) // 查询条件页返回主菜单按钮。
#define A_HMI_LOG_FILTER_TITLE_TEXT_ID         (141U) // 日志条件查询页标题文本。
#define A_HMI_LOG_FILTER_REFRESH_ALL           (0x00FFU) // 查询页八项动态控件全部刷新位。

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

// 日志条件使用的日期时间，只保存用于比较的六个字段。
typedef struct
{
    uint16_t year;  // 完整年份，范围2000～2099。
    uint8_t month;  // 月份，范围1～12。
    uint8_t day;    // 日期，按月份和闰年校验。
    uint8_t hour;   // 小时，范围0～23。
    uint8_t minute; // 分钟，范围0～59。
    uint8_t second; // 秒，范围0～59。
} A_Hmi_Log_Date_Time;

// 串口屏日志筛选条件，常规日志只使用时间条件。
typedef struct
{
    A_Hmi_Log_Date_Time start; // 包含边界的开始时间。
    A_Hmi_Log_Date_Time end;   // 包含边界的结束时间。
    uint8_t cylinder_number;   // 0表示全部，1～6表示指定事件气瓶。
    uint8_t target_state;      // 0表示全部，1～7表示事件的新状态。
    bool time_enabled; // true按起止时间筛选，false查询全部时间；使用范围：当前声明作用域内使用；取值范围：false/true，false表示禁用，true表示启用。
} A_Hmi_Log_Filter;

// 条件查询页提示状态，由日志任务分时发送。
typedef enum
{
    A_HMI_LOG_FILTER_STATUS_READY = 0, // 条件已就绪。
    A_HMI_LOG_FILTER_STATUS_INPUT_ERROR, // 日期或时间输入格式错误。
    A_HMI_LOG_FILTER_STATUS_RESET        // 条件已恢复为全部记录。
} A_Hmi_Log_Filter_Status;

// 串口屏分页日志应用上下文，保存模块引用、单类型索引、查询快照和当前页面发送进度。
typedef struct
{
    A_Hmi_Context *hmi;                                       // 已初始化的串口屏应用实例。
    A_Gas_Log_Context *log;                                   // AT24C256循环日志实例。
    const Gas_System *system;                                 // 气源系统只读实例，用于判断RTC有效性。
    A_Hmi_Log_Query_State state;                              // 当前非阻塞查询或分页步骤。
    A_Hmi_Log_Query_Type query_type;                          // RAM索引及当前页面对应的日志类型。
    A_Hmi_Log_Query_Type pending_type;                        // 最近一次刷新请求指定的日志类型。
    A_Hmi_Log_Filter edit_filter;                             // 查询页当前正在编辑的条件。
    A_Hmi_Log_Filter active_filter;                           // 当前日志快照固定使用的条件副本。
    A_Hmi_Log_Filter_Status filter_status;                    // 查询页当前待显示的输入提示。
    uint32_t snapshot_next_sequence;                          // 建立索引时保存的下一流水号，用于检测日志变化。
    uint16_t total_count;                                     // 查询快照包含的全部原始日志数量。
    uint16_t matched_count;                                   // 已扫描并写入RAM索引的当前类型日志数量。
    uint16_t scanned_count;                                   // 当前快照中已经读取并校验的原始日志数量。
    uint16_t scan_logical_index;                              // 反向扫描边界，下一条读取索引为该值减1。
    uint16_t log_index[A_HMI_LOG_INDEX_CAPACITY];             // 当前类型从新到旧排列的EEPROM逻辑索引，约占2KB RAM。
    uint16_t filter_refresh_mask;                             // 日志查询上下文待刷新位图；bit0～bit7依次表示开始日期、开始时间、结束日期、结束时间、全部时间开关、气瓶条件、状态条件和提示文本，0表示无需刷新，1表示等待刷新，bit8～bit15保留为0。
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
    bool current_record_ready; // current_record中是否保存有效日志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。
    bool row_ready; // row中是否保存等待发送的有效行；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。
    bool request_pending; // 是否收到新的事件或常规日志刷新请求；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
    bool page_request_pending; // 是否收到最新页、上一页或下一页请求；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
    bool read_failed; // 本次查询是否发生EEPROM读取或记录校验失败；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未发生失败，true表示已经发生失败。
    bool filter_error; // 日志时间筛选错误标志；使用范围：A_Hmi_Log_Context当前查询快照内；取值范围：false/true，false表示起止时间关系合法，true表示开始时间晚于结束时间。
    bool snapshot_changed; // 索引建立后是否有新日志产生，需要人员刷新；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未发生变化，true表示已经发生变化。
    bool progress_pending; // 是否有一条后台索引进度文本等待发送；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
    bool index_complete; // 当前类型的全部RAM索引是否已经建立完成；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未完成，true表示已完成。
    bool cache_valid; // 当前RAM索引是否对应一个可用的日志快照；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无效，true表示有效。
    bool page_rendered; // 日志页面显示完成标志；使用范围：A_Hmi_Log_Context当前分页流程内；取值范围：false/true，false表示尚未完整显示当前页，true表示当前记录控件已显示一页有效数据。
    bool table_is_clear; // 当前记录控件是否已经清空且尚未追加页面数据；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未清除，true表示已清除。
    bool ready; // HMI和日志模块引用是否已经配置；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。
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
 * 函数名：A_HmiLog_HandleFilterButton。
 * 说明：处理条件页选择、重置和查询按钮，并维护当前编辑条件。
 * 输入：context为日志查询上下文；button_id和value为串口屏按钮事件。
 * 输出：按钮属于日志条件模块时返回true，否则返回false。
 */
bool A_HmiLog_HandleFilterButton(A_Hmi_Log_Context *context,
                                 uint16_t button_id,
                                 uint8_t value);

/*
 * 函数名：A_HmiLog_InputTask。
 * 说明：取出Screen6的YYYYMMDD或HHMMSS数字输入，校验后更新编辑条件。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：无；非法输入会恢复旧值并显示提示。
 */
void A_HmiLog_InputTask(A_Hmi_Log_Context *context);

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

/*
 * 函数名：A_HmiLog_InvalidateCache。
 * 说明：取消当前日志扫描或翻页并清除RAM索引，供EEPROM日志开始物理清除时调用。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：无；筛选条件保留，旧日志索引和待处理读取请求全部失效。
 */
void A_HmiLog_InvalidateCache(A_Hmi_Log_Context *context);

#endif
