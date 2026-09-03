/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现系统上电后的FreeRTOS气源控制程序入口。
 */

#include "hal_data.h"

#include "A_Gas_Rtos.h"

/*
 * 函数名：hal_entry。
 * 说明：作为FreeRTOS应用入口，创建气源任务并启动调度器。
 * 输入：无。
 * 输出：无；调度器正常启动后函数不返回。
 */
void hal_entry(void)
{
#if BSP_TZ_SECURE_BUILD
    R_BSP_NonSecureEnter();
#else
    static A_Gas_Rtos_Context gas_rtos_context; // 当前作用域变量，用于保存模块上下文。
    static A_Gas_Control_Context gas_control_context; // 气源业务长期实例，与RTOS任务资源分开静态分配。

    if (!A_GasRtos_Start(&gas_rtos_context, &gas_control_context))
    {
        for (;;)
        {
            __WFI();
            // 任务创建或调度器启动失败后保持所有输出的上电安全状态。
        }
    }
#endif
}

#if BSP_TZ_SECURE_BUILD

BSP_CMSE_NONSECURE_ENTRY void template_nonsecure_callable ();

/* Trustzone Secure Projects require at least one nonsecure callable function in order to build (Remove this if it is not required to build). */
BSP_CMSE_NONSECURE_ENTRY void template_nonsecure_callable ()
{

}
#endif
