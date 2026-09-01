/*
 * Version: v1.12
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明大彩串口屏协议层帧、事件队列和发送接口。
 */

#ifndef F_HMI_H
#define F_HMI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "H_Hmi.h"

#define F_HMI_FRAME_MAX_SIZE (64U)  // 单帧大彩接收指令允许的最大缓存长度。
#define F_HMI_TX_MAX_SIZE    (192U) // 日志表格单行和普通文本共用的最大发送帧长度。
#define F_HMI_TEXT_MAX_SIZE  (16U)  // 参数输入控件允许上传的最大ASCII文本长度，另留一字节结束符。
#define F_HMI_TEXT_EVENT_QUEUE_SIZE (8U) // 连续失焦和查询时暂存的文本输入事件数量。

// 一条由B1 11文本控件上传的完整输入事件。
typedef struct
{
    uint16_t page_id;                              // 文本输入事件所属画面ID。
    uint16_t control_id;                           // 文本输入控件ID。
    char value[F_HMI_TEXT_MAX_SIZE + 1U];          // ASCII内容，始终追加字符串结束符。
    uint8_t length;                                // value中的有效ASCII字节数。
} F_Hmi_Text_Event;

// 大彩串口屏 RTC 响应解析结果，年份已经由两位 BCD 转换为 2000～2099 完整年份。
typedef struct
{
    uint16_t year;  // 完整年份，范围 2000～2099。
    uint8_t month;  // 月，范围 1～12。
    uint8_t day;    // 日，范围由实际月份决定。
    uint8_t week;   // 星期，0 表示星期日，1～6 表示星期一至星期六。
    uint8_t hour;   // 时，范围 0～23。
    uint8_t minute; // 分，范围 0～59。
    uint8_t second; // 秒，范围 0～59。
} F_Hmi_Rtc_Time;

// 大彩指令集解析和发送功能上下文，由上层实例持有并通过指针传递。
typedef struct
{
    H_Hmi_Context hardware;                 // SCI9 硬件层实例。
    uint8_t rx_frame[F_HMI_FRAME_MAX_SIZE]; // 当前正在组装的接收帧。
    uint16_t rx_length;                     // 当前接收帧已有字节数。
    bool receiving; // 是否已经收到 0xEE 帧头；使用范围：当前声明作用域内使用；取值范围：false/true，false表示当前未接收协议帧，true表示当前正在接收协议帧。
    bool button_pending; // 是否存在尚未取走的按钮或下拉菜单选择事件；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
    uint16_t button_id;                     // 最近一次按钮或下拉菜单控件 ID。
    uint8_t button_value;                   // 按钮上传状态值；下拉菜单时为选中项索引。
    F_Hmi_Text_Event text_queue[F_HMI_TEXT_EVENT_QUEUE_SIZE]; // 文本输入FIFO，保持串口到达顺序。
    uint8_t text_queue_head;                // FIFO当前待读事件位置。
    uint8_t text_queue_count;               // FIFO内尚未被应用层取走的事件数量。
    F_Hmi_Rtc_Time rtc_time;                // 最近一次通过校验的串口屏 RTC 时间。
    bool rtc_time_pending; // 是否存在尚未被应用层取走的 RTC 时间；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
    uint8_t tx_frame[F_HMI_TX_MAX_SIZE];    // 异步发送期间保持不变的发送缓冲区。
} F_Hmi_Context;

/*
 * 函数名：F_Hmi_Init。
 * 说明：初始化大彩协议解析功能和 SCI9 硬件层。
 * 输入：context 为 HMI 功能层上下文输入输出指针。
 * 输出：初始化成功时返回 true，否则返回 false。
 */
bool F_Hmi_Init(F_Hmi_Context *context);

/*
 * 函数名：F_Hmi_Task。
 * 说明：从 SCI9 环形缓冲区取字节并解析大彩按钮和下拉菜单控件上传帧。
 * 输入：context 为 HMI 功能层上下文输入输出指针。
 * 输出：无；解析成功后锁存按钮/下拉菜单事件，并按顺序写入文本输入FIFO。
 */
void F_Hmi_Task(F_Hmi_Context *context);

/*
 * 函数名：F_Hmi_TakeButtonEvent。
 * 说明：取出一条已经解析完成的按钮或下拉菜单选择事件。
 * 输入：context 为功能层上下文；button_id 为控件 ID 输出指针；value 为按钮状态或菜单选中项索引输出指针。
 * 输出：存在待处理事件时返回 true，否则返回 false。
 */
bool F_Hmi_TakeButtonEvent(F_Hmi_Context *context, uint16_t *button_id, uint8_t *value);

/*
 * 函数名：F_Hmi_TakeTextEvent。
 * 说明：取出一条由大彩文本输入控件通过B1 11上传的ASCII文本事件。
 * 输入：context为功能层上下文；page_id和control_id为控件标识输出指针；text为输出缓存；capacity为容量。
 * 输出：存在事件且输出缓存足够时返回文本长度；没有事件或参数无效时返回0。
 */
size_t F_Hmi_TakeTextEvent(F_Hmi_Context *context,
                           uint16_t *page_id,
                           uint16_t *control_id,
                           char *text,
                           size_t capacity);

/*
 * 函数名：F_Hmi_PeekTextEvent。
 * 说明：查看文本输入FIFO队首事件的画面和控件ID，但不取走文本。
 * 输入：context为功能层上下文；page_id和control_id为控件标识输出指针。
 * 输出：存在待处理文本事件时返回true，否则返回false。
 */
bool F_Hmi_PeekTextEvent(const F_Hmi_Context *context,
                         uint16_t *page_id,
                         uint16_t *control_id);

/*
 * 函数名：F_Hmi_SendReadRtc。
 * 说明：按大彩协议发送读取串口屏全局 RTC 的 0x82 指令；RTC 控件 ID 不包含在此全局命令中。
 * 输入：context 为 HMI 功能层上下文输入输出指针。
 * 输出：成功启动异步发送时返回 true，否则返回 false。
 */
bool F_Hmi_SendReadRtc(F_Hmi_Context *context);

/*
 * 函数名：F_Hmi_TakeRtcTime。
 * 说明：取出一条已经完成 BCD 和日期合法性校验的 0xF7 RTC 响应。
 * 输入：context 为 HMI 功能层上下文；rtc_time 为 RTC 时间输出指针。
 * 输出：存在待处理的有效 RTC 时间时返回 true，否则返回 false。
 */
bool F_Hmi_TakeRtcTime(F_Hmi_Context *context, F_Hmi_Rtc_Time *rtc_time);

/*
 * 函数名：F_Hmi_SendText。
 * 说明：按大彩 B1 10 指令格式更新指定画面和控件的 ASCII 文本。
 * 输入：context 为功能层上下文；page_id 为画面 ID；control_id 为控件 ID；text 为文本；length 为文本长度。
 * 输出：成功启动异步发送时返回 true，否则返回 false。
 */
bool F_Hmi_SendText(F_Hmi_Context *context,
                    uint16_t page_id,
                    uint16_t control_id,
                    const char *text,
                    size_t length);

/*
 * 函数名：F_Hmi_SendButtonState。
 * 说明：按大彩B1 10指令强制设置指定按钮控件的弹起或按下状态。
 * 输入：context为功能层上下文；page_id和control_id指定按钮；pressed为false弹起、true按下。
 * 输出：成功启动异步发送时返回true，串口忙或参数无效时返回false。
 */
bool F_Hmi_SendButtonState(F_Hmi_Context *context,
                           uint16_t page_id,
                           uint16_t control_id,
                           bool pressed);

/*
 * 函数名：F_Hmi_SendIconFrame。
 * 说明：按大彩B1 23指令设置指定图标控件当前显示帧，用于状态图层切换。
 * 输入：context为功能层上下文；page_id为画面ID；control_id为图标控件ID；frame_id为从0开始的帧索引。
 * 输出：成功启动异步发送时返回true，串口忙或参数无效时返回false。
 */
bool F_Hmi_SendIconFrame(F_Hmi_Context *context,
                         uint16_t page_id,
                         uint16_t control_id,
                         uint8_t frame_id);

/*
 * 函数名：F_Hmi_SendRecordClear。
 * 说明：按大彩B1 53指令清空指定数据记录控件中的现有行。
 * 输入：context为功能层上下文；page_id为画面ID；control_id为数据记录控件ID。
 * 输出：成功启动异步发送时返回true，串口忙或参数无效时返回false。
 */
bool F_Hmi_SendRecordClear(F_Hmi_Context *context,
                           uint16_t page_id,
                           uint16_t control_id);

/*
 * 函数名：F_Hmi_SendRecordAdd。
 * 说明：按大彩B1 52指令向指定通用表格添加一行以分号分隔的GBK文本。
 * 输入：context为功能层上下文；page_id和control_id指定表格；record为行文本；length为字节数。
 * 输出：成功启动异步发送时返回true，串口忙、参数无效或帧过长时返回false。
 */
bool F_Hmi_SendRecordAdd(F_Hmi_Context *context,
                         uint16_t page_id,
                         uint16_t control_id,
                         const char *record,
                         size_t length);

#endif
