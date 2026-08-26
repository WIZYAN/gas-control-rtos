#ifndef A_GAS_CONTROL_H
#define A_GAS_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "gas_common.h"
#include "A_Gas_Config.h"
#include "A_Gas_Log.h"
#include "F_Gas_Runtime.h"
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
    F_Gas_Runtime_Context runtime_service;   // 平台初始化、时基和空闲处理功能实例。
    A_Modbus_Poll_Context sensor_poll;       // SCI1 内部传感器 Modbus 轮询实例。
    A_Can_Context external_can;              // CAN0外部通讯实例，默认使用250 kbit/s扩展帧。
    A_Modbus_Context external_modbus;        // 保留的SCI0/RS485外部Modbus从站实例。
    gas_external_comm_mode_t external_comm_mode; // 当前实际启用的外部通讯模式。
    A_Hmi_Context hmi;                       // SCI9 大彩串口屏实例。
    A_Hmi_Config_Context hmi_config;         // 密码保护的11项可见参数编辑、完整参数校验和反馈实例。
    A_Hmi_Log_Context hmi_log;               // 串口屏事件和常规日志全量流式滑动查询实例。
    A_Storage_Context storage_service;       // 软件 IIC 和 AT24C256 存储实例。
    A_Gas_Log_Context log_service;           // AT24C256常规记录和状态事件循环日志实例。
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
 * 函数名：A_GasControl_StartAuto。
 * 说明：选择合格备用瓶并启动自动供气模式。
 * 输入：context 为应用上下文输入输出指针。
 * 输出：成功进入自动模式时返回 true，否则返回 false。
 */
bool A_GasControl_StartAuto(A_Gas_Control_Context *context);

/*
 * 函数名：A_GasControl_Stop。
 * 说明：停止自动控制并关闭当前实例的全部阀门。
 * 输入：context 为应用上下文输入输出指针。
 * 输出：无。
 */
void A_GasControl_Stop(A_Gas_Control_Context *context);

/*
 * 函数名：A_GasControl_SetExternalCommMode。
 * 说明：在系统停止且十八路阀门全部关闭时切换CAN或RS485外部通讯，并把成功模式保存到EEPROM。
 * 输入：context为应用上下文；mode为目标通讯模式，0表示CAN、1表示RS485/Modbus。
 * 输出：目标接口成功启用并完成持久化时返回true；运行中、初始化失败或存储失败时返回false并恢复原模式。
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
 * 函数名：A_GasControl_SetTestValve。
 * 说明：按照串口屏开关状态打开或关闭指定气瓶测试阀，并按当前超时参数自动关闭；允许排气阀同时开启。
 * 输入：context 为应用上下文；index 为从 0 开始的气瓶索引。
 * 输出：命令满足状态和互锁条件并成功执行时返回 true，否则返回 false。
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
 * 说明：设置指定气瓶由人员确认的测试合格标志，作为初始化或低压待换进入待用的必要条件。
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
