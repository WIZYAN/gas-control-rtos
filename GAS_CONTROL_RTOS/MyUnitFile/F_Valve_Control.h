#ifndef F_VALVE_CONTROL_H
#define F_VALVE_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "gas_common.h"
#include "H_Gas_Platform.h"

/*
 * 函数名：F_ValveControl_Init。
 * 说明：以全部阀门关闭的安全状态初始化阀门功能。
 * 输入：platform 为硬件上下文；system 为系统状态输入输出指针。
 * 输出：无；清除全部阀门命令状态。
 */
void F_ValveControl_Init(H_Gas_Platform_Context *platform, Gas_System *system);

/*
 * 函数名：F_ValveControl_Task。
 * 说明：周期推进 12 V 吸合计时，并在本次开阀指定时间到达后切换到约 5 V 保持状态。
 * 输入：platform 为硬件上下文；system 为系统状态；now_ms 为当前毫秒计数。
 * 输出：无；硬件切换失败时通过 system 设置平台异常报警。
 */
void F_ValveControl_Task(H_Gas_Platform_Context *platform,
                       Gas_System *system,
                       uint32_t now_ms);

/*
 * 函数名：F_ValveControl_SetSupply。
 * 说明：在气瓶状态及互锁约束下设置指定供气阀。
 * 输入：platform 为硬件上下文；system 为系统状态；config 为运行参数；index 为气瓶索引；on 为目标状态。
 * 输出：命令成功执行时返回 true，否则返回 false。
 */
bool F_ValveControl_SetSupply(H_Gas_Platform_Context *platform,
                              Gas_System *system,
                              const Gas_Config *config,
                              uint8_t index,
                              bool on);

/*
 * 函数名：F_ValveControl_SetExhaust。
 * 说明：在自动模式的初始化、待用或低压待换状态设置排气阀；与供气阀互锁，但允许测试阀同时开启。
 * 输入：platform 为硬件上下文；system 为系统状态；config 为运行参数；index 为气瓶索引；on 为目标状态。
 * 输出：命令成功执行时返回 true，否则返回 false。
 */
bool F_ValveControl_SetExhaust(H_Gas_Platform_Context *platform,
                               Gas_System *system,
                               const Gas_Config *config,
                               uint8_t index,
                               bool on);

/*
 * 函数名：F_ValveControl_SetTest。
 * 说明：在自动模式的初始化、待用或低压待换状态设置测试阀；与供气阀互锁，但允许排气阀同时开启。
 * 输入：platform 为硬件上下文；system 为系统状态；config 为运行参数；index 为气瓶索引；on 为目标状态。
 * 输出：命令成功执行时返回 true，否则返回 false。
 */
bool F_ValveControl_SetTest(H_Gas_Platform_Context *platform,
                            Gas_System *system,
                            const Gas_Config *config,
                            uint8_t index,
                            bool on);

/*
 * 函数名：F_ValveControl_AllOff。
 * 说明：关闭全部硬件阀门并清除系统中的阀门命令镜像。
 * 输入：platform 为硬件上下文；system 为可选的系统状态输出指针。
 * 输出：无。
 */
void F_ValveControl_AllOff(H_Gas_Platform_Context *platform, Gas_System *system);

#endif
