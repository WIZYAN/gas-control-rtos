/*
 * Version: v1.12
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现气源运行时平台初始化和毫秒时基功能封装。
 */

#include "F_Gas_Runtime.h"

#include <stddef.h>

/*
 * 函数名：F_GasRuntime_Init。
 * 说明：初始化运行环境功能实例及其拥有的硬件逻辑层上下文。
 * 输入：service 为运行环境功能实例的输入输出指针。
 * 输出：硬件初始化完成且允许进入自动控制时返回 true，否则返回 false。
 */
bool F_GasRuntime_Init(F_Gas_Runtime_Context *service)
{
    if (service == NULL)
    {
        return false;
    }

    service->ready = H_GasPlatform_Init(&service->platform);
    return service->ready;
}

/*
 * 函数名：F_GasRuntime_Millis。
 * 说明：通过硬件逻辑层读取并更新当前运行环境实例的单调毫秒时基。
 * 输入：service 为运行环境功能实例的输入输出指针。
 * 输出：返回当前实例启动后的毫秒计数；参数无效时返回 0。
 */
uint32_t F_GasRuntime_Millis(F_Gas_Runtime_Context *service)
{
    if (service == NULL)
    {
        return 0U;
    }
    return H_GasPlatform_Millis(&service->platform);
}

/*
 * 函数名：F_GasRuntime_Idle。
 * 说明：把应用主循环的空闲处理请求转交给当前实例的硬件逻辑层。
 * 输入：service 为运行环境功能实例指针。
 * 输出：无。
 */
void F_GasRuntime_Idle(F_Gas_Runtime_Context *service)
{
    if (service != NULL)
    {
        H_GasPlatform_Idle(&service->platform);
    }
}
