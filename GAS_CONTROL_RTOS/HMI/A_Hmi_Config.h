/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明串口屏参数设置和日志清除模块的数据与接口。
 */

#ifndef A_HMI_CONFIG_H
#define A_HMI_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "F_Hmi.h"
#include "../MyUnitFile/A_Gas_Config.h"

#define A_HMI_CONFIG_PAGE_ID              (4U)  // 密码保护的运行参数设置画面ID。
#define A_HMI_CONFIG_DIALOG_PAGE_ID       (5U)  // 参数逐项修改的独立确认画面ID。
#define A_HMI_CONFIG_MENU_BUTTON_ID       (78U) // 主菜单进入参数设置画面的按钮ID。
#define A_HMI_CONFIG_TEXT_BASE            (80U) // 11个参数文本输入控件起点，使用80～90。
#define A_HMI_CONFIG_TEXT_COUNT           (11U) // 串口屏参数页显示和编辑的参数总数。
#define A_HMI_CONFIG_STATUS_TEXT_ID       (93U) // 参数校验和保存结果文本控件ID。
#define A_HMI_CONFIG_DEFAULT_BUTTON_ID    (97U) // 建立默认候选值并自动打开确认子画面的按钮ID。
#define A_HMI_CONFIG_BACK_BUTTON_ID       (98U) // 返回主菜单并结束本次编辑的按钮ID。
#define A_HMI_CONFIG_CONFIRM_BUTTON_ID    (108U) // 确认写入EEPROM并立即应用候选参数的按钮ID。
#define A_HMI_CONFIG_CANCEL_BUTTON_ID     (109U) // 取消本次保存确认且不改变当前运行参数的按钮ID。
#define A_HMI_CONFIG_DIALOG_NAME_TEXT_ID  (110U) // 独立确认画面参数名称文本控件ID。
#define A_HMI_CONFIG_DIALOG_OLD_TEXT_ID   (111U) // 独立确认画面当前值文本控件ID。
#define A_HMI_CONFIG_DIALOG_NEW_TEXT_ID   (112U) // 独立确认画面候选值文本控件ID。
#define A_HMI_CONFIG_DIALOG_INFO_TEXT_ID  (113U) // 独立确认画面校验结果文本控件ID。
#define A_HMI_LOG_CLEAR_BUTTON_ID         (142U) // 参数页“清除全部日志”维护按钮ID。
#define A_HMI_LOG_CLEAR_PAGE_ID           (7U)   // 独立日志清除确认和进度画面ID。
#define A_HMI_LOG_CLEAR_TITLE_TEXT_ID     (143U) // 日志清除画面标题文本控件ID。
#define A_HMI_LOG_CLEAR_COUNT_TEXT_ID     (144U) // 清除前或失败后的当前日志数量文本控件ID。
#define A_HMI_LOG_CLEAR_WARNING_TEXT_ID   (145U) // 日志不可恢复警告文本控件ID。
#define A_HMI_LOG_CLEAR_CONFIRM_BUTTON_ID (146U) // 确认物理清除全部日志的瞬时按钮ID。
#define A_HMI_LOG_CLEAR_BACK_BUTTON_ID    (147U) // 返回参数页按钮ID；清除已开始时不会取消后台任务。
#define A_HMI_LOG_CLEAR_STATUS_TEXT_ID    (148U) // 日志清除等待、进度和最终结果文本控件ID。

// 气源业务层完成参数保存后返回给串口屏参数模块的结果。
typedef enum
{
    A_HMI_CONFIG_RESULT_SUCCESS = 0,      // EEPROM保存成功，参数已经投入运行。
    A_HMI_CONFIG_RESULT_INVALID_RANGE,    // 至少一项参数超出允许范围。
    A_HMI_CONFIG_RESULT_INVALID_RELATION, // 压力阈值先后关系错误。
    A_HMI_CONFIG_RESULT_STORAGE_FAILED    // EEPROM写入或读回校验失败。
} A_Hmi_Config_Result;

// 日志清除画面的业务状态，由气源应用层把EEPROM状态机结果映射后回报。
typedef enum
{
    A_HMI_LOG_CLEAR_WAIT_CONFIRM = 0, // 已显示不可恢复警告，等待人员二次确认。
    A_HMI_LOG_CLEAR_BUSY,             // 正在提交空索引或逐页擦除、读回校验。
    A_HMI_LOG_CLEAR_SUCCESS,          // 全部旧日志物理数据已清除。
    A_HMI_LOG_CLEAR_FAILED            // EEPROM操作失败，需要人员重新执行。
} A_Hmi_Log_Clear_Status;

// 串口屏参数页状态由气源主上下文持有，所有编辑值先暂存，保存成功前不影响运行控制。
typedef struct
{
    F_Hmi_Context *transport;    // 复用SCI9协议解析和发送队列的HMI功能实例。
    H_Hmi_Context *hardware;     // 与transport配对的SCI9硬件实例，仅在发送时显式传入。
    Gas_Config edit_config;      // 参数页11项可见参数及2项内部固定参数组成的完整编辑缓存。
    uint16_t refresh_mask;       // 参数编辑上下文待刷新位图；bit0～bit10依次表示切换压力、待用压力、低压警告、压力上限、吸合时间、低压确认时间、低压样本数、关阀等待、开阀等待、排气时长和测试阀最长时长，bit11表示状态提示，bit12～bit15依次表示确认页名称、旧值、新值和说明；0表示无需刷新，1表示等待刷新。
    uint8_t status;              // 当前提示文本编号，由模块内部解释。
    uint8_t pending_field;       // 当前待确认字段序号，0～10为单项，0xFF表示恢复全部默认值。
    char pending_old_text[F_HMI_TEXT_MAX_SIZE + 1U]; // 确认子画面显示的当前生效参数文本。
    char pending_new_text[F_HMI_TEXT_MAX_SIZE + 1U]; // 确认子画面显示的候选参数或非法原始输入文本。
    uint16_t log_clear_count;     // 日志清除画面最近一次显示的有效日志数量。
    uint8_t log_clear_progress;   // 日志物理清除进度，范围0～100。
    uint8_t log_clear_refresh_mask; // 日志清除上下文待刷新位图；bit0表示日志数量，bit1表示等待、进度或结果，0表示无需刷新，1表示等待刷新，bit2～bit7保留为0。
    A_Hmi_Log_Clear_Status log_clear_status; // 当前日志清除确认、执行或结果状态。
    bool active; // 参数页已经通过密码进入并处于编辑会话；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未激活，true表示已激活。
    bool confirm_pending; // 候选参数已通过校验，等待人员点击“确认修改”；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
    bool save_pending; // 存在一份等待气源业务层处理的保存请求；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
    bool log_clear_dialog_active; // 已经从密码参数页进入日志清除独立画面；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未激活，true表示已激活。
    bool log_clear_request_pending; // 人员二次确认后等待气源业务层接收清除请求；使用范围：当前声明作用域内使用；取值范围：false/true，false表示无待处理事项，true表示存在待处理事项。
} A_Hmi_Config_Context;

/*
 * 函数名：A_HmiConfig_Init。
 * 说明：初始化串口屏参数编辑模块并关联已有HMI通信实例。
 * 输入：context为参数模块上下文；transport和hardware为配对的HMI协议及SCI9硬件实例。
 * 输出：参数有效时返回true，否则返回false。
 */
bool A_HmiConfig_Init(A_Hmi_Config_Context *context,
                      F_Hmi_Context *transport,
                      H_Hmi_Context *hardware);

/*
 * 函数名：A_HmiConfig_Open。
 * 说明：进入参数页时复制完整运行参数、应用两项本地固定规则，并安排11个输入控件分时刷新。
 * 输入：context为参数模块上下文；config为当前生效参数。
 * 输出：参数有效时返回true，否则返回false。
 */
bool A_HmiConfig_Open(A_Hmi_Config_Context *context,
                      const Gas_Config *config);

/*
 * 函数名：A_HmiConfig_HandleButton。
 * 说明：识别确认子画面、恢复默认及返回按钮，确认前不直接写EEPROM。
 * 输入：context为参数模块上下文；button_id和value为按钮事件；current_config为当前生效参数。
 * 输出：按钮属于参数模块并被识别时返回true，否则返回false。
 */
bool A_HmiConfig_HandleButton(A_Hmi_Config_Context *context,
                              uint16_t button_id,
                              uint8_t value,
                              const Gas_Config *current_config);

/*
 * 函数名：A_HmiConfig_InputTask。
 * 说明：取出一条参数文本上传事件，建立单字段候选值并安排确认子画面刷新。
 * 输入：context为参数模块上下文；current_config为当前参数。
 * 输出：无；编辑缓存、提示状态和刷新位图可能被更新。
 */
void A_HmiConfig_InputTask(A_Hmi_Config_Context *context,
                           const Gas_Config *current_config);

/*
 * 函数名：A_HmiConfig_TakeSaveRequest。
 * 说明：根据11项可见输入补齐自动回差和固定新鲜度，向业务层交付完整13项参数。
 * 输入：context为参数模块上下文；config为候选参数输出指针。
 * 输出：存在待处理保存请求时返回true，否则返回false。
 */
bool A_HmiConfig_TakeSaveRequest(A_Hmi_Config_Context *context, Gas_Config *config);

/*
 * 函数名：A_HmiConfig_ReportResult。
 * 说明：接收气源业务层保存结果，并在成功时用实际生效参数同步编辑缓存。
 * 输入：context为参数模块上下文；result为保存结果；current_config为当前实际生效参数。
 * 输出：无；更新提示文本并安排参数页刷新。
 */
void A_HmiConfig_ReportResult(A_Hmi_Config_Context *context,
                              A_Hmi_Config_Result result,
                              const Gas_Config *current_config);

/*
 * 函数名：A_HmiConfig_Task。
 * 说明：按照待刷新位图分时发送一个参数值或确认/结果提示，避免阻塞SCI9。
 * 输入：context为参数模块上下文。
 * 输出：无；每次最多启动一帧异步发送。
 */
void A_HmiConfig_Task(A_Hmi_Config_Context *context);

/*
 * 函数名：A_HmiConfig_IsActive。
 * 说明：查询参数编辑页面是否处于活动会话，用于暂停监控画面后台刷新。
 * 输入：context为只读参数模块上下文。
 * 输出：参数页活动时返回true，否则返回false。
 */
bool A_HmiConfig_IsActive(const A_Hmi_Config_Context *context);

/*
 * 函数名：A_HmiConfig_OpenLogClear。
 * 说明：从密码参数页进入日志清除确认画面，并显示清除前有效日志数量。
 * 输入：context为参数模块上下文；log_count为当前事件和常规日志总数。
 * 输出：参数页会话有效且成功建立确认画面状态时返回true，否则返回false。
 */
bool A_HmiConfig_OpenLogClear(A_Hmi_Config_Context *context, uint16_t log_count);

/*
 * 函数名：A_HmiConfig_HandleLogClearButton。
 * 说明：处理日志清除画面的确认和返回按钮；确认只产生一次后台清除请求。
 * 输入：context为参数模块上下文；button_id和value为串口屏按钮事件。
 * 输出：按钮属于日志清除画面时返回true，否则返回false。
 */
bool A_HmiConfig_HandleLogClearButton(A_Hmi_Config_Context *context,
                                      uint16_t button_id,
                                      uint8_t value);

/*
 * 函数名：A_HmiConfig_TakeLogClearRequest。
 * 说明：取出人员已经二次确认的日志物理清除请求，防止同一次触摸重复执行。
 * 输入：context为参数模块上下文。
 * 输出：存在新的清除请求时返回true，否则返回false。
 */
bool A_HmiConfig_TakeLogClearRequest(A_Hmi_Config_Context *context);

/*
 * 函数名：A_HmiConfig_ReportLogClear。
 * 说明：接收日志模块的清除状态、进度和当前数量，并安排Screen7及参数页提示刷新。
 * 输入：context为参数模块上下文；status为清除状态；progress为0～100进度；log_count为当前日志数量。
 * 输出：无；只有内容发生变化时才加入串口屏刷新队列。
 */
void A_HmiConfig_ReportLogClear(A_Hmi_Config_Context *context,
                                A_Hmi_Log_Clear_Status status,
                                uint8_t progress,
                                uint16_t log_count);

#endif
