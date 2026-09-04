/*
 * Version: v1.14
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现FreeRTOS气源控制任务的静态创建和调度入口。
 */

#include "A_Gas_Rtos.h"

#include <stddef.h>
#include <string.h>

/*
 * 函数名：F_GasRtos_ControlTask。
 * 说明：初始化完整气源业务实例，并以固定5ms周期执行原有综合任务。
 * 输入：argument指向A_Gas_Rtos_Context实例。
 * 输出：无；任务启动后持续运行且不返回。
 */
static void F_GasRtos_ControlTask(void *argument)
{
    A_Gas_Rtos_Context *context = (A_Gas_Rtos_Context *) argument; // 当前作用域变量，用于保存模块上下文指针。
    TickType_t last_wake_tick; // 当前作用域变量，用于保存当前处理数据。

    if ((context == NULL) || (context->gas_control == NULL))
    {
        //删除当前FreeRTOS任务
        vTaskDelete(NULL);
        return;
    }

    //初始化六瓶状态机、三阀控制、内部轮询、默认CAN外部通讯、串口屏和EEPROM存储服务
    A_GasControl_Init(context->gas_control);
    last_wake_tick = xTaskGetTickCount(); // 当前作用域变量，用于保存当前处理数据。
    for (;;)
    {
        //周期执行压力轮询、七状态判断、三阀计时、自动切瓶、日志、串口屏和当前外部通讯
        A_GasControl_Task(context->gas_control);
        //阻塞到下一个固定任务周期；把毫秒转换为FreeRTOS节拍
        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(A_GAS_RTOS_CONTROL_PERIOD_MS));
        // 使用绝对周期延时减少任务执行时间波动导致的长期漂移。
    }
}

/*
 * 函数名：A_GasRtos_Start。
 * 说明：使用调用者提供的上下文静态创建气源控制任务，然后启动FreeRTOS调度器。
 * 输入：context为FreeRTOS任务资源；gas_control为长期有效的气源业务实例。
 * 输出：任务创建失败或调度器异常返回时返回false；正常运行时函数不返回。
 */
bool A_GasRtos_Start(A_Gas_Rtos_Context *context,
                     A_Gas_Control_Context *gas_control)
{
    if ((context == NULL) || (gas_control == NULL))
    {
        return false;
    }

    //初始化或清空内存数据
    (void) memset(context, 0, sizeof(*context));
    context->gas_control = gas_control;
    //静态创建FreeRTOS任务
    context->control_task_handle = xTaskCreateStatic(F_GasRtos_ControlTask,
                                                     "GasControl",
                                                     A_GAS_RTOS_CONTROL_STACK_WORDS,
                                                     context,
                                                     A_GAS_RTOS_CONTROL_PRIORITY,
                                                     context->control_task_stack,
                                                     &context->control_task_buffer);
    if (context->control_task_handle == NULL)
    {
        return false;
    }

    //启动FreeRTOS调度器
    vTaskStartScheduler();
    return false;
}

/*
 * 函数名：vApplicationGetIdleTaskMemory。
 * 说明：向FreeRTOS提供空闲任务必需的静态TCB和栈内存。
 * 输入：ppxIdleTaskTCBBuffer、ppxIdleTaskStackBuffer和ulIdleTaskStackSize为FreeRTOS输出指针。
 * 输出：无；通过输入指针返回静态内存和栈深度。
 */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    static StaticTask_t idle_task_buffer; // 当前作用域变量，用于保存数据缓冲区。
    static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE]; // 当前作用域变量，用于保存当前处理数据数组。

    *ppxIdleTaskTCBBuffer = &idle_task_buffer;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/*
 * 函数名：vApplicationGetTimerTaskMemory。
 * 说明：向FreeRTOS提供软件定时器服务任务的静态TCB和栈内存。
 * 输入：ppxTimerTaskTCBBuffer、ppxTimerTaskStackBuffer和ulTimerTaskStackSize为FreeRTOS输出指针。
 * 输出：无；通过输入指针返回静态内存和栈深度。
 */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    static StaticTask_t timer_task_buffer; // 当前作用域变量，用于保存数据缓冲区。
    static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH]; // 当前作用域变量，用于保存当前处理数据数组。

    *ppxTimerTaskTCBBuffer = &timer_task_buffer;
    *ppxTimerTaskStackBuffer = timer_task_stack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

/*
 * 函数名：vApplicationIdleHook。
 * 说明：实现FreeRTOS空闲任务钩子；系统没有就绪任务时让CPU等待中断，以降低空闲功耗。
 * 输入：无。
 * 输出：无。
 */
void vApplicationIdleHook(void)
{
    __WFI();
    // 空闲任务不执行气源业务，只等待系统节拍或外设中断唤醒。
}
