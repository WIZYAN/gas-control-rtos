/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 声明FreeRTOS控制任务上下文和静态启动接口。
 */

#ifndef A_GAS_RTOS_H
#define A_GAS_RTOS_H

#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#include "A_Gas_Control.h"

#define A_GAS_RTOS_CONTROL_STACK_WORDS   (1024U) // 气源控制任务栈深度，单位为StackType_t字。
#define A_GAS_RTOS_CONTROL_PRIORITY      (3U)    // 气源控制任务优先级，高于后续通讯和存储任务。
#define A_GAS_RTOS_CONTROL_PERIOD_MS     (5U)    // 气源综合任务的固定调度周期，单位ms。

// FreeRTOS气源应用上下文，聚合业务实例、静态任务控制块和栈空间。
typedef struct
{
    A_Gas_Control_Context gas_control; // 气源控制系统完整业务实例。
    StaticTask_t control_task_buffer; // 气源控制任务的FreeRTOS静态TCB内存。
    StackType_t control_task_stack[A_GAS_RTOS_CONTROL_STACK_WORDS]; // 气源控制任务静态栈。
    TaskHandle_t control_task_handle; // 气源控制任务句柄，创建失败时为NULL。
    bool scheduler_started; // 是否已经进入FreeRTOS调度器；使用范围：当前声明作用域内使用；取值范围：false/true，false表示调度器尚未启动，true表示调度器已经启动。
} A_Gas_Rtos_Context;

/*
 * 函数名：A_GasRtos_Start。
 * 说明：使用调用者提供的上下文静态创建气源控制任务，然后启动FreeRTOS调度器。
 * 输入：context为FreeRTOS气源应用上下文输入输出指针。
 * 输出：任务创建失败或调度器异常返回时返回false；正常运行时函数不返回。
 */
bool A_GasRtos_Start(A_Gas_Rtos_Context *context);

/*
 * 函数名：vApplicationGetIdleTaskMemory。
 * 说明：向FreeRTOS返回空闲任务使用的静态TCB、栈地址和栈深度。
 * 输入：三个参数均为FreeRTOS提供的输出指针。
 * 输出：无；通过输入指针返回静态任务内存。
 */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize);

/*
 * 函数名：vApplicationGetTimerTaskMemory。
 * 说明：向FreeRTOS返回软件定时器服务任务使用的静态TCB、栈地址和栈深度。
 * 输入：三个参数均为FreeRTOS提供的输出指针。
 * 输出：无；通过输入指针返回静态任务内存。
 */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize);

/*
 * 函数名：vApplicationIdleHook。
 * 说明：实现FreeRTOS空闲任务钩子，使CPU在无业务任务时等待中断。
 * 输入：无。
 * 输出：无。
 */
void vApplicationIdleHook(void);

#endif
