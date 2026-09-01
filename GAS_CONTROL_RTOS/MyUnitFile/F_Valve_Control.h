/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明三阀控制功能层上下文和阀门操作接口。
 */

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
 * 输出：硬件全关并清除全部阀门命令时返回true，否则返回false。
 */
bool F_ValveControl_Init(H_Gas_Platform_Context *platform, Gas_System *system);

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
 * 说明：在自动模式的初始化、待测试、待用或低压待换状态设置排气阀；与供气阀互锁，但允许测试阀同时开启。
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
 * 说明：在总测试阀已开启及业务状态允许时设置分路测试阀；与供气阀互锁，但允许排气阀同时开启。
 * 输入：platform 为硬件上下文；system 为系统状态；config 为运行参数；index 为气瓶索引；on 为目标状态。
 * 输出：命令成功执行时返回 true，否则返回 false。
 */
bool F_ValveControl_SetTest(H_Gas_Platform_Context *platform,
                            Gas_System *system,
                            const Gas_Config *config,
                            uint8_t index,
                            bool on);

/*
 * 函数名：F_ValveControl_SetTotalTest。
 * 说明：设置由六路测试阀内部联动的VAL_CAL总测试阀，不提供独立人工或外部控制入口。
 * 输入：platform为硬件上下文；system为系统状态；config为运行参数；on为目标状态。
 * 输出：总测试阀命令成功执行时返回true，否则返回false。
 */
bool F_ValveControl_SetTotalTest(H_Gas_Platform_Context *platform,
                                 Gas_System *system,
                                 const Gas_Config *config,
                                 bool on);

/*
 * 函数名：F_ValveControl_TotalTestCanOpen。
 * 说明：检查共享VALP1+的1号阀组是否已经满足下一次12 V强吸合间隔。
 * 输入：platform为只读硬件上下文；now_ms为当前毫秒时间。
 * 输出：总测试阀可以安全开始强吸合时返回true，否则返回false。
 */
bool F_ValveControl_TotalTestCanOpen(const H_Gas_Platform_Context *platform,
                                     uint32_t now_ms);

/*
 * 函数名：F_ValveControl_AllOff。
 * 说明：尝试关闭全部硬件阀门，只有全部成功才清除系统中的阀门命令镜像。
 * 输入：platform 为硬件上下文；system 为必需的系统状态输出指针。
 * 输出：全部硬件阀门关闭写入成功时返回true，参数无效或任一路失败时返回false。
 */
bool F_ValveControl_AllOff(H_Gas_Platform_Context *platform, Gas_System *system);

#endif
