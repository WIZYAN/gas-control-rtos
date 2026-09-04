/*
 * Version: v1.14
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明气源控制总上下文、业务状态和外部操作接口。
 */

#ifndef A_GAS_CONTROL_H
#define A_GAS_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "gas_common.h"
#include "A_Gas_Config.h"
#include "A_Gas_Log.h"
#include "H_Gas_Platform.h"
#include "F_Valve_Control.h"
#include "../AT24C256/A_Storage.h"
#include "../CAN/A_Can.h"
#include "../HMI/A_Hmi.h"
#include "../HMI/A_Hmi_Config.h"
#include "../HMI/A_Hmi_Log.h"
#include "../modbus/A_Modbus.h"
#include "../modbus_poll/A_Modbus_Poll.h"

// 气源控制应用层上下文，拥有单个系统实例及其所需的全部功能层上下文，不依赖模块全局变量。
typedef struct
{
    Gas_Config config;                       // 当前实际用于控制并从 EEPROM 加载的运行参数。
    Gas_System system;                       // 六瓶气源的完整业务状态。
    H_Gas_Platform_Context platform;         // 硬件平台、毫秒时基和阀门输出状态，应用层直接持有。
    A_Modbus_Poll_Context sensor_poll;       // SCI1 内部传感器 Modbus 轮询实例。
    A_Can_Context external_can;              // CAN0外部通讯实例，默认使用250 kbit/s扩展帧。
    A_Modbus_Context external_modbus;        // 保留的SCI0/RS485外部Modbus从站实例。
    gas_external_comm_mode_t external_comm_mode; // 当前实际启用的外部通讯模式。
    A_Hmi_Context hmi;                       // SCI9 大彩串口屏实例。
    A_Hmi_Config_Context hmi_config;         // 密码保护的11项可见参数编辑、完整参数校验和反馈实例。
    A_Hmi_Log_Context hmi_log;               // 串口屏事件15条、常规10条分页索引查询实例。
    A_Storage_Context storage_service;       // 软件 IIC 和 AT24C256 存储实例。
    A_Gas_Log_Context log_service;           // AT24C256常规记录和状态事件循环日志实例。
    uint8_t total_test_pending_open_mask;     // 等待总测试阀稳定后开启的分路位图；bit0～bit5对应1～6号测试阀。
    uint32_t test_open_not_before_ms[GAS_CYLINDER_COUNT]; // 各分路测试阀允许开启的最早毫秒时间。
    uint8_t switch_old_index;                 // 自动切瓶流程中的原工作瓶索引。
    uint8_t switch_new_index;                 // 自动切瓶流程中的目标备用瓶索引。
    uint8_t switch_low_sample_count;           // 当前低压确认累计的独立有效样本数。
    uint32_t switch_low_last_sample_ms;        // 已计入低压确认的最后压力样本时间戳。
    uint32_t switch_enter_ms;                  // 进入当前切瓶子状态的毫秒时间。
    uint32_t switch_low_start_ms;              // 本次低压确认开始的毫秒时间。
    bool emergency_close_pending;            // 紧急全关重试标志；使用范围：气源控制任务；false表示无待重试请求，true表示上次全关失败需每周期继续尝试。
} A_Gas_Control_Context;

/*
 * 函数名：A_GasControl_Init。
 * 说明：初始化一个完整的六瓶气源控制应用实例。
 * 输入：context 为待初始化的应用上下文输入输出指针。
 * 输出：无；通过 context 输出全部初始状态。
 */
void A_GasControl_Init(A_Gas_Control_Context *context);

/*
 * 函数名：A_GasControl_Task。
 * 说明：周期执行压力轮询、外部 Modbus、串口屏、人工阀门、停用控制、切瓶和输出安全检查。
 * 输入：context 为应用上下文输入输出指针。
 * 输出：无；通过 context 输出本周期处理结果。
 */
void A_GasControl_Task(A_Gas_Control_Context *context);

/*
 * 函数名：A_GasControl_SetExternalCommMode。
 * 说明：在六瓶全部停用且十九路阀门关闭时切换CAN或RS485外部通讯，并把成功模式保存到EEPROM。
 * 输入：context为应用上下文；mode为目标通讯模式，0表示CAN、1表示RS485/Modbus。
 * 输出：目标接口成功启用并完成持久化时返回true；维护条件、初始化或存储失败时返回false并恢复原模式。
 */
bool A_GasControl_SetExternalCommMode(A_Gas_Control_Context *context,
                                      gas_external_comm_mode_t mode);

/*
 * 函数名：A_GasControl_StartExhaust。
 * 说明：通过串口屏在自动模式的允许状态启动排气阀，并按当前参数到时自动关闭；允许测试阀同时开启。
 * 输入：context 为应用上下文；index 为从 0 开始的气瓶索引。
 * 输出：状态和互锁允许启动时返回 true，否则返回 false。
 */
bool A_GasControl_StartExhaust(A_Gas_Control_Context *context, uint8_t index);

/*
 * 函数名：A_GasControl_StopExhaust。
 * 说明：立即关闭指定气瓶排气阀并取消本次人工排气截止时间，供CAN远程关闭操作使用。
 * 输入：context为应用上下文；index为从0开始的气瓶索引。
 * 输出：排气阀已经关闭或成功关闭时返回true，参数或硬件无效时返回false。
 */
bool A_GasControl_StopExhaust(A_Gas_Control_Context *context, uint8_t index);

/*
 * 函数名：A_GasControl_SetTestValve。
 * 说明：请求打开或关闭指定气瓶测试阀；内部先联动VAL_CAL总测试阀，再按当前超时参数自动关闭分路。
 * 输入：context 为应用上下文；index 为从 0 开始的气瓶索引。
 * 输出：关闭已完成或开启请求已安全接收时返回true，参数、状态或互锁不允许时返回false。
 */
bool A_GasControl_SetTestValve(A_Gas_Control_Context *context, uint8_t index, bool on);

/*
 * 函数名：A_GasControl_SetCylinderDisabled。
 * 说明：设置或解除指定气瓶的停用状态；停用时立即关闭该瓶三只电磁阀。
 * 输入：context 为应用上下文；index 为从 0 开始的气瓶索引；disabled 为目标停用状态。
 * 输出：状态允许且操作完成时返回 true，否则返回 false。
 */
bool A_GasControl_SetCylinderDisabled(A_Gas_Control_Context *context,
                                      uint8_t index,
                                      bool disabled);

/*
 * 函数名：A_GasControl_SetQualificationPassed。
 * 说明：设置指定气瓶由人员确认的测试合格标志，作为待测试进入待用的必要条件。
 * 输入：context 为应用上下文；index 为从0开始的气瓶索引；passed 为测试是否通过。
 * 输出：参数有效并完成设置时返回 true，否则返回 false。
 */
bool A_GasControl_SetQualificationPassed(A_Gas_Control_Context *context,
                                         uint8_t index,
                                         bool passed);

/*
 * 函数名：A_GasControl_GetSystem。
 * 说明：取得指定应用实例的只读业务状态。
 * 输入：context 为只读应用上下文指针。
 * 输出：返回只读系统状态指针；参数无效时返回 NULL。
 */
const Gas_System *A_GasControl_GetSystem(const A_Gas_Control_Context *context);

#endif
