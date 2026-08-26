#ifndef F_GAS_RUNTIME_H
#define F_GAS_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "H_Gas_Platform.h"

// 运行环境功能层上下文，封装硬件上下文并向应用层提供初始化、时基和空闲处理接口。
typedef struct
{
    H_Gas_Platform_Context platform; // 当前气源控制实例专用的硬件逻辑层上下文。
    bool ready;                    // 硬件初始化和安全使能是否全部就绪。
} F_Gas_Runtime_Context;

/*
 * 函数名：F_GasRuntime_Init。
 * 说明：初始化运行环境功能实例及其拥有的硬件逻辑层上下文。
 * 输入：service 为运行环境功能实例的输入输出指针。
 * 输出：硬件初始化完成且允许进入自动控制时返回 true，否则返回 false。
 */
bool F_GasRuntime_Init(F_Gas_Runtime_Context *service);

/*
 * 函数名：F_GasRuntime_Millis。
 * 说明：读取并更新当前运行环境实例的单调毫秒时基。
 * 输入：service 为运行环境功能实例的输入输出指针。
 * 输出：返回当前实例启动后的毫秒计数；参数无效时返回 0。
 */
uint32_t F_GasRuntime_Millis(F_Gas_Runtime_Context *service);

/*
 * 函数名：F_GasRuntime_Idle。
 * 说明：执行当前运行环境实例的空闲处理。
 * 输入：service 为运行环境功能实例指针。
 * 输出：无。
 */
void F_GasRuntime_Idle(F_Gas_Runtime_Context *service);

#endif
