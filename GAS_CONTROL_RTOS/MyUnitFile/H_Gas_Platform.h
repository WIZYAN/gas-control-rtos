/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明气源平台硬件上下文、阀门映射和底层操作接口。
 */

#ifndef H_GAS_PLATFORM_H
#define H_GAS_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gas_common.h"

// 硬件逻辑层上下文，由上层实例持有并通过指针传入所有硬件接口，模块内部不保存全局状态。
typedef struct
{
    volatile bool sensor_tx_done;                  // 传感器发送完成标志；使用范围：SCI1中断与传感器任务之间；取值范围：false/true，false表示异步发送尚未完成，true表示最近一次异步发送已完成。
    volatile bool sensor_rx_done;                  // 传感器接收完成标志；使用范围：SCI1中断与传感器任务之间；取值范围：false/true，false表示定长接收尚未完成，true表示最近一次定长接收已完成。
    volatile bool sensor_uart_error;               // 传感器串口错误标志；使用范围：SCI1中断与传感器任务之间；取值范围：false/true，false表示事务正常，true表示最近一次事务发生错误。
    bool sensor_uart_open; // 压力传感器 SCI1 实例是否已经成功打开；使用范围：当前声明作用域内使用；取值范围：false/true，false表示关闭，true表示开启。
    uint32_t millis;                               // 由 DWT 周期计数换算得到的累计毫秒计数。
    uint32_t last_cycle_count;                     // 上次换算时间时读取的 DWT 周期计数。
    uint32_t cycle_remainder;                      // 尚不足 1 ms 的剩余 CPU 周期数。
    bool supply_state[GAS_CYLINDER_COUNT]; // 六路供气阀最近一次成功写入的逻辑状态；使用范围：当前声明作用域内使用；取值范围：false/true，false表示关闭或未置位，true表示开启或已置位。
    bool exhaust_state[GAS_CYLINDER_COUNT]; // 六路排气阀最近一次成功写入的逻辑状态；使用范围：当前声明作用域内使用；取值范围：false/true，false表示关闭或未置位，true表示开启或已置位。
    bool test_state[GAS_CYLINDER_COUNT]; // 六路测试阀最近一次成功写入的逻辑状态；使用范围：当前声明作用域内使用；取值范围：false/true，false表示关闭或未置位，true表示开启或已置位。
    bool total_test_state; // VAL_CAL总测试阀最近一次成功写入的逻辑状态；使用范围：当前声明作用域内使用；取值范围：false/true，false表示关闭或未置位，true表示开启或已置位。
    bool boost_state[GAS_CYLINDER_COUNT]; // 六组 VAL_Px 当前是否处于 12 V 吸合阶段；使用范围：当前声明作用域内使用；取值范围：false/true，false表示关闭或未置位，true表示开启或已置位。
    uint32_t boost_deadline_ms[GAS_CYLINDER_COUNT]; // 六组 12 V 吸合阶段的结束时间。
    bool boost_interval_active[GAS_CYLINDER_COUNT]; // 六组强吸合最短间隔计时是否正在生效；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未激活，true表示已激活。
    uint32_t boost_available_ms[GAS_CYLINDER_COUNT]; // 六组允许再次启动 12 V 强吸合的最早时间。
} H_Gas_Platform_Context;

/*
 * 函数名：H_GasPlatform_Init。
 * 说明：初始化指定 RA4M1 硬件逻辑层实例，并把全部阀门引脚恢复为安全关闭的 GPIO 输出模式。
 * 输入：context 为硬件逻辑层上下文输入输出指针。
 * 输出：通信、时基、方向、阀门引脚和阀门使能全部就绪时返回 true，否则返回 false。
 */
bool H_GasPlatform_Init(H_Gas_Platform_Context *context);

/*
 * 函数名：H_GasPlatform_Millis。
 * 说明：读取并更新指定硬件实例的单调毫秒计数。
 * 输入：context 为硬件逻辑层上下文输入输出指针。
 * 输出：返回当前毫秒计数；参数无效时返回 0。
 */
uint32_t H_GasPlatform_Millis(H_Gas_Platform_Context *context);

/*
 * 函数名：H_GasPlatform_Idle。
 * 说明：执行指定硬件实例的空闲处理。
 * 输入：context 为硬件逻辑层上下文指针。
 * 输出：无。
 */
void H_GasPlatform_Idle(H_Gas_Platform_Context *context);

/*
 * 函数名：H_GasPlatform_SensorTxStart。
 * 说明：启动压力传感器 RS485 异步发送。
 * 输入：context 为硬件上下文；data 为发送缓冲区；length 为发送字节数。
 * 输出：成功启动时返回 true，否则返回 false。
 */
bool H_GasPlatform_SensorTxStart(H_Gas_Platform_Context *context, const uint8_t *data, size_t length);

/*
 * 函数名：H_GasPlatform_SensorRxStart。
 * 说明：启动压力传感器 RS485 定长异步接收。
 * 输入：context 为硬件上下文；data 为接收缓冲区；length 为期望字节数。
 * 输出：成功启动时返回 true，否则返回 false。
 */
bool H_GasPlatform_SensorRxStart(H_Gas_Platform_Context *context, uint8_t *data, size_t length);

/*
 * 函数名：H_GasPlatform_SensorAbort。
 * 说明：中止当前传感器收发事务并恢复 RS485 接收方向。
 * 输入：context 为硬件逻辑层上下文输入输出指针。
 * 输出：无。
 */
void H_GasPlatform_SensorAbort(H_Gas_Platform_Context *context);

/*
 * 函数名：H_GasPlatform_SensorTxDone。
 * 说明：查询当前实例的传感器发送完成状态。
 * 输入：context 为只读硬件逻辑层上下文指针。
 * 输出：发送完成且无错误时返回 true，否则返回 false。
 */
bool H_GasPlatform_SensorTxDone(const H_Gas_Platform_Context *context);

/*
 * 函数名：H_GasPlatform_SensorRxDone。
 * 说明：查询当前实例的传感器接收完成状态。
 * 输入：context 为只读硬件逻辑层上下文指针。
 * 输出：接收完成且无错误时返回 true，否则返回 false。
 */
bool H_GasPlatform_SensorRxDone(const H_Gas_Platform_Context *context);

/*
 * 函数名：H_GasPlatform_WriteSupplyValve。
 * 说明：设置指定气瓶的板级供气阀输出。
 * 输入：context 为硬件上下文；cylinder_index 为气瓶索引；on 为目标状态；pull_in_time_ms 为 12 V 吸合时间。
 * 输出：命令成功写入时返回 true，否则返回 false。
 */
bool H_GasPlatform_WriteSupplyValve(H_Gas_Platform_Context *context,
                                    uint8_t cylinder_index,
                                    bool on,
                                    uint32_t pull_in_time_ms);

/*
 * 函数名：H_GasPlatform_WriteExhaustValve。
 * 说明：设置指定气瓶的板级排气阀输出。
 * 输入：context 为硬件上下文；cylinder_index 为气瓶索引；on 为目标状态；pull_in_time_ms 为 12 V 吸合时间。
 * 输出：命令成功写入时返回 true，否则返回 false。
 */
bool H_GasPlatform_WriteExhaustValve(H_Gas_Platform_Context *context,
                                     uint8_t cylinder_index,
                                     bool on,
                                     uint32_t pull_in_time_ms);

/*
 * 函数名：H_GasPlatform_WriteTestValve。
 * 说明：设置指定气瓶的板级测试阀输出。
 * 输入：context 为硬件上下文；cylinder_index 为气瓶索引；on 为目标状态；pull_in_time_ms 为 12 V 吸合时间。
 * 输出：命令成功写入时返回 true，否则返回 false。
 */
bool H_GasPlatform_WriteTestValve(H_Gas_Platform_Context *context,
                                  uint8_t cylinder_index,
                                  bool on,
                                  uint32_t pull_in_time_ms);

/*
 * 函数名：H_GasPlatform_WriteTotalTestValve。
 * 说明：控制VAL_CAL总测试阀负端，并复用VALP1+执行12 V强吸合和约5 V保持。
 * 输入：context为硬件上下文；on为目标状态；pull_in_time_ms为12 V强吸合时间。
 * 输出：命令成功写入时返回true，否则返回false。
 */
bool H_GasPlatform_WriteTotalTestValve(H_Gas_Platform_Context *context,
                                       bool on,
                                       uint32_t pull_in_time_ms);

/*
 * 函数名：H_GasPlatform_ValveTask。
 * 说明：到达本次开阀指定的吸合截止时间后关闭对应 VAL_Px，使已开启线圈转入约 5 V 保持状态。
 * 输入：context 为硬件上下文；now_ms 为当前毫秒计数。
 * 输出：所有到期 VAL_Px 均成功关闭时返回 true，否则返回 false 并保留待重试状态。
 */
bool H_GasPlatform_ValveTask(H_Gas_Platform_Context *context, uint32_t now_ms);

/*
 * 函数名：H_GasPlatform_AllValvesOff。
 * 说明：尝试关闭板上全部已知阀门，仅在所有GPIO均写入成功后清零输出状态。
 * 输入：context 为硬件逻辑层上下文输入输出指针。
 * 输出：全部阀门GPIO均成功写入关闭电平时返回true，参数无效或任一写入失败时返回false。
 */
bool H_GasPlatform_AllValvesOff(H_Gas_Platform_Context *context);

#endif
