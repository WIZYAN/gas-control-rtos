/*
 * Version: v1.11
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明串口屏应用层控件映射、上下文和刷新接口。
 */

#ifndef A_HMI_H
#define A_HMI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "F_Hmi.h"
#include "../MyUnitFile/gas_common.h"

#define A_HMI_MONITOR_PAGE_ID         (1U)   // 六瓶实时监控画面ID；0号画面作为主菜单。
#define A_HMI_EXHAUST_BUTTON_BASE     (1U)   // 1～6 号气瓶排气按钮控件 ID 起点。
#define A_HMI_TEST_BUTTON_BASE        (7U)   // 1～6 号气瓶测试阀开关控件 ID 起点。
#define A_HMI_DISABLE_BUTTON_BASE     (13U)  // 1～6 号气瓶停用开关控件 ID 起点。
#define A_HMI_PRESSURE_TEXT_BASE      (19U)  // 六路压力显示控件 ID 起点，使用 19～24。
#define A_HMI_STATE_TEXT_BASE         (25U)  // 六路状态显示控件 ID 起点，使用 25～30。
#define A_HMI_SUPPLY_ICON_BASE        (31U)  // 六路进气阀双色状态图标ID起点，使用31～36。
#define A_HMI_EXHAUST_ICON_BASE       (37U)  // 六路排气阀双色状态图标ID起点，使用37～42。
#define A_HMI_TEST_ICON_BASE          (43U)  // 六路测试阀双色状态图标ID起点，使用43～48。
#define A_HMI_TOTAL_PRESSURE_TEXT     (49U)  // 总压力传感器显示控件 ID。
#define A_HMI_RTC_CONTROL_ID          (50U)  // 串口屏 RTC 时间显示和编辑控件 ID；读取RTC使用全局0x82命令。
#define A_HMI_QUALIFIED_BUTTON_BASE   (51U)  // 1～6号瓶测试通过开关控件ID起点，使用51～56；1通过、0不通过。
#define A_HMI_HIGHLIGHT_ICON_BASE     (72U)  // 六路气瓶卡片高亮图标控件ID起点，使用72～77。
#define A_HMI_HIGHLIGHT_FRAME_NORMAL  (0U)   // 图标第0帧：普通透明图层，气瓶卡片不高亮。
#define A_HMI_HIGHLIGHT_FRAME_ACTIVE  (1U)   // 图标第1帧：绿色使用状态高亮图层。
#define A_HMI_HIGHLIGHT_FRAME_WARNING (2U)   // 图标第2帧：红色低压警告高亮图层。
#define A_HMI_VALVE_FRAME_CLOSED      (0U)   // 阀位图标第0帧：使用原有浅色显示“关闭”。
#define A_HMI_VALVE_FRAME_OPEN        (1U)   // 阀位图标第1帧：使用绿色显示“开启”。
#define A_HMI_PRESSURE_OVERRANGE_TEXT_BASE (101U) // 六路超量程红色压力控件ID起点，使用101～106。
#define A_HMI_TOTAL_PRESSURE_OVERRANGE_TEXT (107U) // 总压力超量程红色显示控件ID。
#define A_HMI_SYSTEM_TITLE_TEXT_ID     (114U) // 实时监控页“气源控制系统”静态文本控件ID。
#define A_HMI_REFRESH_GAP_MS          (20UL) // 相邻两个显示控件更新帧的最小间隔。
#define A_HMI_REFRESH_SLOT_COUNT      (68U)  // 压力双层、状态阀位、高亮及四组六路按钮共68个刷新槽。
#define A_HMI_RTC_READ_INTERVAL_MS    (1000UL) // 向串口屏读取一次全局 RTC 时间的周期，单位 ms。
#define A_HMI_RTC_STALE_TIME_MS       (5000UL) // 连续未收到有效 RTC 响应后判定系统时间失效的时限，单位 ms。

// 串口屏应用层上下文，集中保存协议实例和轮询刷新位置。
typedef struct
{
    F_Hmi_Context function;        // 大彩协议解析和发送功能实例。
    float pressure_refresh_mpa[GAS_PRESSURE_SENSOR_COUNT]; // 压力双层清除与写入两槽之间使用的数值快照。
    gas_pressure_quality_t pressure_refresh_quality[GAS_PRESSURE_SENSOR_COUNT]; // 与压力数值快照对应的数据质量。
    uint8_t refresh_slot;          // 下一个待刷新的显示控件槽号。
    uint8_t supply_valve_refresh_value_bits; // HMI刷新上下文使用的进气阀快照位图；bit0～bit5对应1～6号瓶，0表示关阀，1表示开阀，bit6～bit7保留为0。
    uint8_t supply_valve_refresh_pending_bits; // HMI刷新上下文使用的进气阀待回写位图；bit0～bit5对应31～36号图标，0表示无需刷新，1表示等待刷新，bit6～bit7保留为0。
    uint8_t exhaust_refresh_value_bits;       // HMI刷新上下文使用的排气阀快照位图；bit0～bit5对应1～6号瓶，0表示关阀，1表示开阀，bit6～bit7保留为0。
    uint8_t exhaust_refresh_pending_bits;     // HMI刷新上下文使用的排气按钮待回写位图；bit0～bit5对应1～6号按钮，0表示无需刷新，1表示等待刷新，bit6～bit7保留为0。
    uint8_t exhaust_valve_refresh_pending_bits; // HMI刷新上下文使用的排气阀图标待回写位图；bit0～bit5对应37～42号图标，0表示无需刷新，1表示等待刷新，bit6～bit7保留为0。
    uint8_t test_refresh_value_bits;          // HMI刷新上下文使用的测试阀快照位图；bit0～bit5对应1～6号瓶，0表示关阀，1表示开阀，bit6～bit7保留为0。
    uint8_t test_refresh_pending_bits;        // HMI刷新上下文使用的测试阀按钮待回写位图；bit0～bit5对应7～12号按钮，0表示无需刷新，1表示等待刷新，bit6～bit7保留为0。
    uint8_t test_valve_refresh_pending_bits;  // HMI刷新上下文使用的测试阀图标待回写位图；bit0～bit5对应43～48号图标，0表示无需刷新，1表示等待刷新，bit6～bit7保留为0。
    uint8_t disable_refresh_value_bits;       // HMI刷新上下文使用的停用状态快照位图；bit0～bit5对应1～6号瓶，0表示启用，1表示停用，bit6～bit7保留为0。
    uint8_t disable_refresh_pending_bits;     // HMI刷新上下文使用的停用开关待回写位图；bit0～bit5对应13～18号开关，0表示无需刷新，1表示等待刷新，bit6～bit7保留为0。
    uint8_t qualification_refresh_value_bits;   // HMI刷新上下文使用的测试合格快照位图；bit0～bit5对应1～6号瓶，0表示未合格，1表示已合格，bit6～bit7保留为0。
    uint8_t qualification_refresh_pending_bits; // HMI刷新上下文使用的合格按钮待回写位图；bit0～bit5对应51～56号按钮，0表示无需刷新，1表示等待刷新，bit6～bit7保留为0。
    uint32_t next_refresh_ms;      // 下一个显示帧允许发送的时间。
    uint32_t next_rtc_read_ms;     // 下一次允许发送 RTC 读取命令的时间。
    bool supply_valve_refresh_initialized; // 进气阀快照初始化标志；使用范围：A_Hmi_Context刷新状态内；取值范围：false/true，false表示尚未建立同步快照，true表示已经建立同步快照。
    bool exhaust_refresh_initialized; // 排气阀快照初始化标志；使用范围：A_Hmi_Context刷新状态内；取值范围：false/true，false表示尚未建立同步快照，true表示已经建立同步快照。
    bool test_refresh_initialized; // 测试阀快照初始化标志；使用范围：A_Hmi_Context刷新状态内；取值范围：false/true，false表示尚未建立同步快照，true表示已经建立同步快照。
    bool disable_refresh_initialized; // 停用状态快照初始化标志；使用范围：A_Hmi_Context刷新状态内；取值范围：false/true，false表示尚未建立同步快照，true表示已经建立同步快照。
    bool qualification_refresh_initialized; // 测试结论快照初始化标志；使用范围：A_Hmi_Context刷新状态内；取值范围：false/true，false表示尚未建立同步快照，true表示已经建立同步快照。
    bool ready; // 串口屏 SCI9 和协议层是否初始化成功；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。
} A_Hmi_Context;

/*
 * 函数名：A_Hmi_Init。
 * 说明：初始化大彩 DC10600PM101 串口屏应用实例。
 * 输入：context 为 HMI 应用层上下文输入输出指针。
 * 输出：初始化成功时返回 true，否则返回 false。
 */
bool A_Hmi_Init(A_Hmi_Context *context);

/*
 * 函数名：A_Hmi_Task。
 * 说明：周期解析按钮和RTC响应、更新系统时间，并每秒请求一次串口屏全局RTC。
 * 输入：context 为 HMI 应用层上下文；system 为气源系统输入输出指针；now_ms 为当前毫秒计数。
 * 输出：无；按钮事件保存在内部队列，RTC响应写入 system 的系统日期时间。
 */
void A_Hmi_Task(A_Hmi_Context *context, Gas_System *system, uint32_t now_ms);

/*
 * 函数名：A_Hmi_TakeButtonEvent。
 * 说明：取出一条串口屏按钮或下拉菜单选择事件。
 * 输入：context 为 HMI 上下文；button_id 为控件 ID 输出指针；value 为按钮状态或菜单选中项索引输出指针。
 * 输出：存在待处理事件时返回 true，否则返回 false。
 */
bool A_Hmi_TakeButtonEvent(A_Hmi_Context *context, uint16_t *button_id, uint8_t *value);

/*
 * 函数名：A_Hmi_RequestExhaustSync。
 * 说明：请求优先把指定气瓶的MCU实际排气命令回写到串口屏1～6号排气按钮。
 * 输入：context为HMI上下文；index为从0开始的气瓶索引。
 * 输出：无；参数有效时设置待同步位，实际发送由A_Hmi_Refresh分时完成。
 */
void A_Hmi_RequestExhaustSync(A_Hmi_Context *context, uint8_t index);

/*
 * 函数名：A_Hmi_RequestTestSync。
 * 说明：请求优先把指定气瓶的MCU实际测试阀命令回写到串口屏7～12号测试阀按钮。
 * 输入：context为HMI上下文；index为从0开始的气瓶索引。
 * 输出：无；参数有效时设置待同步位，实际发送由A_Hmi_Refresh分时完成。
 */
void A_Hmi_RequestTestSync(A_Hmi_Context *context, uint8_t index);

/*
 * 函数名：A_Hmi_RequestDisableSync。
 * 说明：请求优先把指定气瓶的MCU停用状态回写到串口屏13～18号停用开关。
 * 输入：context为HMI上下文；index为从0开始的气瓶索引。
 * 输出：无；参数有效时设置待同步位，实际发送由A_Hmi_Refresh分时完成。
 */
void A_Hmi_RequestDisableSync(A_Hmi_Context *context, uint8_t index);

/*
 * 函数名：A_Hmi_RequestQualificationSync。
 * 说明：请求优先把指定气瓶的MCU测试通过标志回写到串口屏51～56号开关。
 * 输入：context为HMI上下文；index为从0开始的气瓶索引。
 * 输出：无；参数有效时设置待同步位，实际发送由A_Hmi_Refresh分时完成。
 */
void A_Hmi_RequestQualificationSync(A_Hmi_Context *context, uint8_t index);

/*
 * 函数名：A_Hmi_TakeTextEvent。
 * 说明：取出一条串口屏文本输入控件上传的ASCII参数文本。
 * 输入：context为HMI上下文；page_id和control_id为控件标识输出；text为输出缓存；capacity为容量。
 * 输出：存在完整事件时返回文本长度，否则返回0。
 */
size_t A_Hmi_TakeTextEvent(A_Hmi_Context *context,
                           uint16_t *page_id,
                           uint16_t *control_id,
                           char *text,
                           size_t capacity);

/*
 * 函数名：A_Hmi_PeekTextEvent。
 * 说明：查看串口屏待处理文本输入的画面和控件ID，便于按页面分发给功能模块。
 * 输入：context为HMI上下文；page_id和control_id为控件标识输出指针。
 * 输出：存在待处理文本事件时返回true，否则返回false。
 */
bool A_Hmi_PeekTextEvent(const A_Hmi_Context *context,
                         uint16_t *page_id,
                         uint16_t *control_id);

/*
 * 函数名：A_Hmi_SendText。
 * 说明：向指定串口屏画面的文本控件发送ASCII或GBK字节文本。
 * 输入：context为HMI上下文；page_id和control_id指定控件；text为文本；length为字节数。
 * 输出：成功启动异步发送时返回true，否则返回false。
 */
bool A_Hmi_SendText(A_Hmi_Context *context,
                    uint16_t page_id,
                    uint16_t control_id,
                    const char *text,
                    size_t length);

/*
 * 函数名：A_Hmi_Refresh。
 * 说明：分时刷新压力、气瓶状态、十八路双色阀位、四组六路操作按钮及卡片高亮。
 * 输入：context 为 HMI 上下文；system 为只读气源系统；now_ms 为当前毫秒计数。
 * 输出：无；每次最多启动一帧异步发送。
 */
void A_Hmi_Refresh(A_Hmi_Context *context, const Gas_System *system, uint32_t now_ms);

#endif
