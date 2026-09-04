/*
 * Version: v1.14
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现压力串口、十九路阀门和毫秒时基的平台硬件操作。
 */

#include "H_Gas_Platform.h"

#include <string.h>

#include "hal_data.h"

// 本文件是 RA4M1 硬件逻辑层，只负责 GPIO、阀门吸合保持驱动、SCI1、RS485 方向和单调时基。
// 供气阀映射为 VAL1/4/7/10/13/16，排气阀映射为 VAL2/5/8/11/14/17，测试阀映射为 VAL3/6/9/12/15/18。

/*
 * 函数名：H_GasPlatform_ValveLevel。
 * 说明：按照阀门有效电平配置，把逻辑开关状态转换为板级 GPIO 电平。
 * 输入：on 为阀门目标逻辑状态，true 表示开阀，false 表示关阀。
 * 输出：返回对应的 BSP GPIO 高电平或低电平枚举值。
 */
static bsp_io_level_t H_GasPlatform_ValveLevel(bool on)
{
    bool high = (on == (GAS_BOARD_VALVE_ACTIVE_LEVEL != 0U)); // 阀门GPIO输出电平标志；使用范围：当前逻辑状态转电平函数内；取值范围：false/true，false表示输出低电平，true表示输出高电平。
    return high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW;
}

/*
 * 函数名：H_GasPlatform_ConfigureValvePins。
 * 说明：把全部阀门控制和强吸合控制引脚强制配置为 GPIO 输出，并在配置瞬间使用安全关闭电平。
 * 输入：无。
 * 输出：全部引脚配置成功时返回 true；任一引脚配置失败时返回 false。
 */
static bool H_GasPlatform_ConfigureValvePins(void)
{
    const bsp_io_port_pin_t all_valve_pins[] =
    {
        VAL1, VAL2, VAL3, VAL_P1, VAL4, VAL5, VAL6, VAL_P2,
        VAL7, VAL8, VAL9, VAL_P3, VAL10, VAL11, VAL12, VAL_P4,
        VAL13, VAL14, VAL15, VAL_P5, VAL16, VAL17, VAL18, VAL_P6, VAL_CAL
    };
    const uint32_t safe_off_cfg = (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                                  ((GAS_BOARD_VALVE_ACTIVE_LEVEL != 0U) ?
                                   (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW :
                                   (uint32_t) IOPORT_CFG_PORT_OUTPUT_HIGH);
    size_t i; // 当前作用域变量，用于保存当前处理数据。
    bool success = true; // success 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示操作失败，true表示操作成功。

    for (i = 0U; i < (sizeof(all_valve_pins) / sizeof(all_valve_pins[0])); ++i)
    {
        //配置GPIO引脚模式
        if (R_IOPORT_PinCfg(&g_ioport_ctrl, all_valve_pins[i], safe_off_cfg) != FSP_SUCCESS)
        {
            success = false;
        }
    }
    // 即使 FSP 中误把某一路配置成外设模式，硬件层初始化也会恢复为安全的 GPIO 输出模式。

    return success;
}

/*
 * 函数名：H_GasPlatform_SupplyValvePin。
 * 说明：把从 0 开始的气瓶索引转换为对应的供气阀 GPIO 引脚。
 * 输入：index 为气瓶索引；pin 为 GPIO 引脚输出指针。
 * 输出：索引和输出指针有效时返回 true 并写入 pin，否则返回 false。
 */
static bool H_GasPlatform_SupplyValvePin(uint8_t index, bsp_io_port_pin_t *pin)
{
    if (pin == NULL)
    {
        return false;
    }

    switch (index)
    {
        case 0U: *pin = VAL1;  break;
        case 1U: *pin = VAL4;  break;
        case 2U: *pin = VAL7;  break;
        case 3U: *pin = VAL10; break;
        case 4U: *pin = VAL13; break;
        case 5U: *pin = VAL16; break;
        default: return false;
    }
    return true;
}

/*
 * 函数名：H_GasPlatform_ExhaustValvePin。
 * 说明：把从 0 开始的气瓶索引转换为对应的排气阀 GPIO 引脚。
 * 输入：index 为气瓶索引；pin 为 GPIO 引脚输出指针。
 * 输出：索引和输出指针有效时返回 true 并写入 pin，否则返回 false。
 */
static bool H_GasPlatform_ExhaustValvePin(uint8_t index, bsp_io_port_pin_t *pin)
{
    if (pin == NULL)
    {
        return false;
    }

    switch (index)
    {
        case 0U: *pin = VAL2;  break;
        case 1U: *pin = VAL5;  break;
        case 2U: *pin = VAL8;  break;
        case 3U: *pin = VAL11; break;
        case 4U: *pin = VAL14; break;
        case 5U: *pin = VAL17; break;
        default: return false;
    }
    return true;
}

/*
 * 函数名：H_GasPlatform_TestValvePin。
 * 说明：把从 0 开始的气瓶索引转换为对应的测试阀 GPIO 引脚。
 * 输入：index 为气瓶索引；pin 为 GPIO 引脚输出指针。
 * 输出：索引和输出指针有效时返回 true 并写入 pin，否则返回 false。
 */
static bool H_GasPlatform_TestValvePin(uint8_t index, bsp_io_port_pin_t *pin)
{
    if (pin == NULL)
    {
        return false;
    }

    switch (index)
    {
        case 0U: *pin = VAL3;  break;
        case 1U: *pin = VAL6;  break;
        case 2U: *pin = VAL9;  break;
        case 3U: *pin = VAL12; break;
        case 4U: *pin = VAL15; break;
        case 5U: *pin = VAL18; break;
        default: return false;
    }
    return true;
}

/*
 * 函数名：H_GasPlatform_BoostValvePin。
 * 说明：把从 0 开始的气瓶索引转换为对应的 VAL_P1～VAL_P6 12 V 吸合控制引脚。
 * 输入：index 为气瓶索引；pin 为 GPIO 引脚输出指针。
 * 输出：索引和输出指针有效时返回 true 并写入 pin，否则返回 false。
 */
static bool H_GasPlatform_BoostValvePin(uint8_t index, bsp_io_port_pin_t *pin)
{
    if (pin == NULL)
    {
        return false;
    }

    switch (index)
    {
        case 0U: *pin = VAL_P1; break;
        case 1U: *pin = VAL_P2; break;
        case 2U: *pin = VAL_P3; break;
        case 3U: *pin = VAL_P4; break;
        case 4U: *pin = VAL_P5; break;
        case 5U: *pin = VAL_P6; break;
        default: return false;
    }
    return true;
}

/*
 * 函数名：H_GasPlatform_TimeReached。
 * 说明：使用有符号毫秒差判断当前时间是否到达截止时间，并兼容计数器回绕。
 * 输入：now_ms 为当前毫秒计数；deadline_ms 为截止毫秒计数。
 * 输出：已经到达或超过截止时间时返回 true，否则返回 false。
 */
static bool H_GasPlatform_TimeReached(uint32_t now_ms, uint32_t deadline_ms)
{
    return ((int32_t) (now_ms - deadline_ms) >= 0);
}

/*
 * 函数名：H_GasPlatform_SetValveBoost。
 * 说明：设置指定气瓶组的 VAL_Px，使能时选择 12 V 吸合电源，关闭时由 5.5VP 支路保持。
 * 输入：context 为硬件上下文；index 为气瓶索引；on 为 12 V 吸合控制状态。
 * 输出：GPIO 写入成功时返回 true，否则返回 false。
 */
static bool H_GasPlatform_SetValveBoost(H_Gas_Platform_Context *context, uint8_t index, bool on)
{
    bsp_io_port_pin_t pin; // 当前作用域变量，用于保存GPIO引脚。

    //把从 0 开始的气瓶索引转换为对应的 VAL_P1～VAL_P6 12 V 吸合控制引脚
    if ((context == NULL) || !H_GasPlatform_BoostValvePin(index, &pin))
    {
        return false;
    }

    //写入GPIO引脚电平；按照阀门有效电平配置
    if (R_IOPORT_PinWrite(&g_ioport_ctrl, pin, H_GasPlatform_ValveLevel(on)) != FSP_SUCCESS)
    {
        context->valve_io_error = true;
        return false;
    }

    context->boost_state[index] = on;
    if (!on)
    {
        context->boost_deadline_ms[index] = 0U;
    }
    return true;
}

/*
 * 函数名：H_GasPlatform_WriteValveOutput。
 * 说明：控制单路阀门负端，并在首次开阀时按运行参数启动对应组的 12 V 吸合阶段。
 * 输入：context 为硬件上下文；index 为气瓶索引；pin 为 VALx 引脚；on 为目标状态；pull_in_time_ms 为吸合时间；state 为输出状态镜像指针。
 * 输出：单路阀门和必要的 VAL_Px 操作均成功时返回 true，否则返回 false。
 */
static bool H_GasPlatform_WriteValveOutput(H_Gas_Platform_Context *context,
                             uint8_t index,
                             bsp_io_port_pin_t pin,
                             bool on,
                             uint32_t pull_in_time_ms,
                             bool *state)
{
    fsp_err_t result; // 当前作用域变量，用于保存操作结果。

    if ((context == NULL) || (state == NULL) || (index >= GAS_CYLINDER_COUNT) ||
        (on && (pull_in_time_ms == 0U)))
    {
        return false;
    }

    if (on)
    {
        if (*state)
        {
            return true; // 重复开阀命令不重新触发 12 V 吸合脉冲。
        }
        if (context->boost_interval_active[index])
        {
            //使用有符号毫秒差判断当前时间是否到达截止时间
            if (!H_GasPlatform_TimeReached(context->millis, context->boost_available_ms[index]))
            {
                return false;
            }
            context->boost_interval_active[index] = false;
        }
        //设置指定气瓶组的 VAL_Px
        if (!H_GasPlatform_SetValveBoost(context, index, true))
        {
            return false;
        }
        context->boost_interval_active[index] = true;
        context->boost_available_ms[index] = context->millis + GAS_VALVE_BOOST_MIN_INTERVAL_MS;
        // 同组第二只阀门开启时会再次提升共享 VAL_Px，因此限制脉冲间隔，避免快速操作反复冲击已开启线圈。

        result = R_IOPORT_PinWrite(&g_ioport_ctrl, pin, H_GasPlatform_ValveLevel(true));
        if (result != FSP_SUCCESS)
        {
            context->valve_io_error = true;
            //设置指定气瓶组的 VAL_Px
            (void) H_GasPlatform_SetValveBoost(context, index, false);
            return false;
        }

        *state = true;
        context->boost_deadline_ms[index] = context->millis + pull_in_time_ms;
        return true;
    }

    //写入GPIO引脚电平；按照阀门有效电平配置
    result = R_IOPORT_PinWrite(&g_ioport_ctrl, pin, H_GasPlatform_ValveLevel(false));
    if (result != FSP_SUCCESS)
    {
        context->valve_io_error = true;
        return false;
    }

    *state = false;
    if (!context->supply_state[index] && !context->exhaust_state[index] &&
        !context->test_state[index] && ((index != 0U) || !context->total_test_state))
    {
        return H_GasPlatform_SetValveBoost(context, index, false); // 组内没有线圈工作时关闭残留的 12 V 吸合控制。
    }
    return true;
}

/*
 * 函数名：H_GasPlatform_SensorDirectionReceive。
 * 说明：把压力传感器 RS485 收发器切换到接收状态，PRE_EN 低电平同时关闭 DE 并使能低有效 /RE。
 * 输入：无。
 * 输出：PRE_EN 成功写入接收电平时返回 true，否则返回 false。
 */
static bool H_GasPlatform_SensorDirectionReceive(void)
{
    bsp_io_level_t receive_level = (GAS_SENSOR_RS485_RX_LEVEL != 0U) ?
                                   BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW;

    //写入GPIO引脚电平
    return (R_IOPORT_PinWrite(&g_ioport_ctrl, PRE_EN, receive_level) == FSP_SUCCESS);
}

/*
 * 函数名：H_GasPlatform_SensorDirectionTransmit。
 * 说明：把压力传感器 RS485 收发器切换到发送状态，PRE_EN 高电平同时关闭低有效 /RE 并使能 DE。
 * 输入：无。
 * 输出：PRE_EN 成功写入发送电平时返回 true，否则返回 false。
 */
static bool H_GasPlatform_SensorDirectionTransmit(void)
{
    bsp_io_level_t transmit_level = (GAS_SENSOR_RS485_TX_LEVEL != 0U) ?
                                    BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW;

    //写入GPIO引脚电平
    return (R_IOPORT_PinWrite(&g_ioport_ctrl, PRE_EN, transmit_level) == FSP_SUCCESS);
}

/*
 * 函数名：H_GasPlatform_Init。
 * 说明：初始化指定硬件上下文、阀门安全状态、RS485 方向、SCI1 9600 波特率和 DWT 单调时基。
 * 输入：context 为硬件逻辑层上下文输入输出指针。
 * 输出：通信、时基、方向控制和阀门总使能全部就绪时返回 true，否则返回 false。
 */
bool H_GasPlatform_Init(H_Gas_Platform_Context *context)
{
    baud_setting_t baud_setting; // 当前作用域变量，用于保存当前处理数据。
    bool timebase_ready; // timebase_ready 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。
    bool direction_ready; // direction_ready 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。
    bool valve_pins_ready; // valve_pins_ready 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。
    bool valves_off; // 上电全关结果标志；使用范围：当前初始化函数；false表示至少一路GPIO关闭写入失败，true表示全部成功。
    bool uart_ready = false; // uart_ready 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示未就绪，true表示已就绪。

    if (context == NULL)
    {
        return false;
    }

    //初始化或清空内存数据
    (void) memset(context, 0, sizeof(*context));
    //把全部阀门控制和强吸合控制引脚强制配置为 GPIO 输出
    valve_pins_ready = H_GasPlatform_ConfigureValvePins();
    //把板上全部已知阀门GPIO写为关闭电平
    valves_off = H_GasPlatform_AllValvesOff(context);
    //把压力传感器 RS485 收发器切换到接收状态
    direction_ready = H_GasPlatform_SensorDirectionReceive();

    //计算SCI串口波特率参数；初始化SCI串口
    if ((R_SCI_UART_BaudCalculate(GAS_SENSOR_UART_BAUDRATE,
                                  false,
                                  GAS_SENSOR_UART_MAX_ERROR_X1000,
                                  &baud_setting) == FSP_SUCCESS) &&
        (R_SCI_UART_Open(&rs485_sensor_ctrl, &rs485_sensor_cfg) == FSP_SUCCESS))
    {
        //更新SCI串口波特率；注册SCI串口回调
        if ((R_SCI_UART_BaudSet(&rs485_sensor_ctrl, &baud_setting) == FSP_SUCCESS) &&
            (R_SCI_UART_CallbackSet(&rs485_sensor_ctrl,
                                    rs485_sensor_callback,
                                    context,
                                    NULL) == FSP_SUCCESS))
        {
            context->sensor_uart_open = true;
            uart_ready = true;
        }
        else
        {
            //关闭SCI串口
            (void) R_SCI_UART_Close(&rs485_sensor_ctrl);
        }
    }
    // PRE_RES 是 120 Ω 匹配电阻使能，不参与收发方向切换；板级启动层将其固定为高电平。
    // 运行时显式设置 9600，并把实例上下文传给 FSP 回调，避免回调依赖模块全局变量。

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    context->last_cycle_count = DWT->CYCCNT;
    timebase_ready = ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) &&
                     (SystemCoreClock >= 1000U);
    // DWT 时基由任务读取时累计，不占用公共 SysTick 中断，也不需要全局回调对象。

    return (uart_ready && timebase_ready && direction_ready && valve_pins_ready && valves_off &&
            (GAS_BOARD_VALVE_OUTPUTS_ENABLED == 1U));
}

/*
 * 函数名：H_GasPlatform_Millis。
 * 说明：根据 DWT 周期计数增量更新并读取指定实例的单调毫秒计数。
 * 输入：context 为硬件逻辑层上下文输入输出指针。
 * 输出：返回该实例启动后的 32 位毫秒计数；参数无效或系统时钟异常时返回 0。
 */
uint32_t H_GasPlatform_Millis(H_Gas_Platform_Context *context)
{
    uint32_t current_cycles; // 当前作用域变量，用于保存当前处理数据。
    uint32_t elapsed_cycles; // 当前作用域变量，用于保存当前处理数据。
    uint32_t cycles_per_ms; // 当前作用域变量，用于保存毫秒时间值。
    uint64_t accumulated_cycles; // 当前作用域变量，用于保存当前处理数据。

    if ((context == NULL) || (SystemCoreClock < 1000U))
    {
        return 0U;
    }

    current_cycles = DWT->CYCCNT;
    elapsed_cycles = current_cycles - context->last_cycle_count;
    context->last_cycle_count = current_cycles;
    cycles_per_ms = SystemCoreClock / 1000U;
    accumulated_cycles = (uint64_t) context->cycle_remainder + elapsed_cycles;
    context->millis += (uint32_t) (accumulated_cycles / cycles_per_ms);
    context->cycle_remainder = (uint32_t) (accumulated_cycles % cycles_per_ms);
    // 使用无符号差值兼容 DWT 计数器回绕，并保留不足 1 ms 的周期余数。

    return context->millis;
}

/*
 * 函数名：H_GasPlatform_SensorTxStart。
 * 说明：切换 RS485 到发送方向，并通过 SCI1 异步发送一帧传感器请求。
 * 输入：context 为硬件上下文；data 为只读发送缓冲区；length 为发送字节数。
 * 输出：成功启动异步发送时返回 true；参数、方向或串口操作失败时返回 false。
 */
bool H_GasPlatform_SensorTxStart(H_Gas_Platform_Context *context, const uint8_t *data, size_t length)
{
    fsp_err_t err; // 当前作用域变量，用于保存当前处理数据。

    if ((context == NULL) || !context->sensor_uart_open || (data == NULL) ||
        (length == 0U) || (length > UINT32_MAX))
    {
        return false;
    }

    context->sensor_tx_done = false;
    //把压力传感器 RS485 收发器切换到发送状态
    if (!H_GasPlatform_SensorDirectionTransmit())
    {
        context->sensor_uart_error = true;
        return false;
    }

    //启动SCI异步发送
    err = R_SCI_UART_Write(&rs485_sensor_ctrl, data, (uint32_t) length);
    if (err != FSP_SUCCESS)
    {
        context->sensor_uart_error = true;
        //把压力传感器 RS485 收发器切换到接收状态
        (void) H_GasPlatform_SensorDirectionReceive();
        return false;
    }
    return true;
}

/*
 * 函数名：H_GasPlatform_SensorRxStart。
 * 说明：通过 SCI1 启动指定长度的异步传感器响应接收。
 * 输入：context 为硬件上下文；data 为接收数据输出缓冲区；length 为期望字节数。
 * 输出：成功启动异步接收时返回 true；参数或串口操作失败时返回 false。
 */
bool H_GasPlatform_SensorRxStart(H_Gas_Platform_Context *context, uint8_t *data, size_t length)
{
    fsp_err_t err; // SCI1定长接收启动结果。

    if ((context == NULL) || !context->sensor_uart_open || (data == NULL) ||
        (length == 0U) || (length > UINT32_MAX))
    {
        return false;
    }

    context->sensor_rx_done = false;
    context->sensor_uart_error = false;
    //启动SCI异步接收
    err = R_SCI_UART_Read(&rs485_sensor_ctrl, data, (uint32_t) length);
    if (err != FSP_SUCCESS)
    {
        context->sensor_uart_error = true;
        return false;
    }
    return true;
}

/*
 * 函数名：H_GasPlatform_SensorAbort。
 * 说明：中止指定实例的 SCI1 收发事务、清除完成标志，并恢复 RS485 接收方向。
 * 输入：context 为硬件逻辑层上下文输入输出指针。
 * 输出：中止和方向恢复均成功时返回 true；任一步失败时返回 false 并保持错误标志。
 */
bool H_GasPlatform_SensorAbort(H_Gas_Platform_Context *context)
{
    bool abort_ok; // SCI1收发中止结果；false表示串口未打开或FSP中止失败。
    bool direction_ok; // RS485恢复接收方向结果；false表示PRE_EN写入失败。

    if (context == NULL)
    {
        return false;
    }

    context->sensor_tx_done = false;
    context->sensor_rx_done = false;
    context->sensor_uart_error = false;
    //中止SCI串口传输
    abort_ok = context->sensor_uart_open &&
               (R_SCI_UART_Abort(&rs485_sensor_ctrl, UART_DIR_RX_TX) == FSP_SUCCESS);
    //把压力传感器 RS485 收发器切换到接收状态
    direction_ok = H_GasPlatform_SensorDirectionReceive();
    if (!abort_ok || !direction_ok)
    {
        context->sensor_uart_error = true;
        return false;
    }
    return true;
}

/*
 * 函数名：H_GasPlatform_SensorTxDone。
 * 说明：查询指定实例最近一次 SCI1 异步发送是否已成功完成。
 * 输入：context 为只读硬件逻辑层上下文指针。
 * 输出：发送完成且未发生串口错误时返回 true，否则返回 false。
 */
bool H_GasPlatform_SensorTxDone(const H_Gas_Platform_Context *context)
{
    return ((context != NULL) && context->sensor_tx_done && !context->sensor_uart_error);
}

/*
 * 函数名：H_GasPlatform_SensorRxDone。
 * 说明：查询指定实例最近一次 SCI1 定长异步接收是否已成功完成。
 * 输入：context 为只读硬件逻辑层上下文指针。
 * 输出：接收完成且未发生串口错误时返回 true，否则返回 false。
 */
bool H_GasPlatform_SensorRxDone(const H_Gas_Platform_Context *context)
{
    return ((context != NULL) && context->sensor_rx_done && !context->sensor_uart_error);
}

/*
 * 函数名：H_GasPlatform_SensorHasError。
 * 说明：查询指定实例当前传感器串口事务的错误标志。
 * 输入：context 为只读硬件逻辑层上下文指针。
 * 输出：错误标志已置位或参数无效时返回 true，否则返回 false。
 */
bool H_GasPlatform_SensorHasError(const H_Gas_Platform_Context *context)
{
    return ((context == NULL) || context->sensor_uart_error);
}

/*
 * 函数名：H_GasPlatform_WriteSupplyValve。
 * 说明：把指定气瓶的供气阀命令写入对应 GPIO，并执行板级输出总使能限制。
 * 输入：context 为硬件上下文；cylinder_index 为气瓶索引；on 为目标开关状态；pull_in_time_ms 为 12 V 吸合时间。
 * 输出：GPIO 命令成功执行时返回 true；参数、使能或硬件写入失败时返回 false。
 */
bool H_GasPlatform_WriteSupplyValve(H_Gas_Platform_Context *context,
                                    uint8_t cylinder_index,
                                    bool on,
                                    uint32_t pull_in_time_ms)
{
    bsp_io_port_pin_t pin; // 当前作用域变量，用于保存GPIO引脚。

    //把从 0 开始的气瓶索引转换为对应的供气阀 GPIO 引脚
    if ((context == NULL) || !H_GasPlatform_SupplyValvePin(cylinder_index, &pin))
    {
        return false;
    }

#if (GAS_BOARD_VALVE_OUTPUTS_ENABLED == 0U)
    if (on)
    {
        return false;
    }
    // 板级输出未使能时只接受关阀命令。
#endif

    //控制单路阀门负端
    return H_GasPlatform_WriteValveOutput(context,
                            cylinder_index,
                            pin,
                            on,
                            pull_in_time_ms,
                            &context->supply_state[cylinder_index]);
}

/*
 * 函数名：H_GasPlatform_WriteExhaustValve。
 * 说明：把指定气瓶的排气阀命令写入对应 GPIO，并执行板级输出总使能限制。
 * 输入：context 为硬件上下文；cylinder_index 为气瓶索引；on 为目标开关状态；pull_in_time_ms 为 12 V 吸合时间。
 * 输出：GPIO 命令成功执行时返回 true；参数、使能或硬件写入失败时返回 false。
 */
bool H_GasPlatform_WriteExhaustValve(H_Gas_Platform_Context *context,
                                     uint8_t cylinder_index,
                                     bool on,
                                     uint32_t pull_in_time_ms)
{
    bsp_io_port_pin_t pin; // 当前作用域变量，用于保存GPIO引脚。

    //把从 0 开始的气瓶索引转换为对应的排气阀 GPIO 引脚
    if ((context == NULL) || !H_GasPlatform_ExhaustValvePin(cylinder_index, &pin))
    {
        return false;
    }

#if (GAS_BOARD_VALVE_OUTPUTS_ENABLED == 0U)
    if (on)
    {
        return false;
    }
    // 板级输出未使能时只接受关阀命令。
#endif

    //控制单路阀门负端
    return H_GasPlatform_WriteValveOutput(context,
                            cylinder_index,
                            pin,
                            on,
                            pull_in_time_ms,
                            &context->exhaust_state[cylinder_index]);
}

/*
 * 函数名：H_GasPlatform_WriteTestValve。
 * 说明：把指定气瓶的测试阀命令写入对应 VAL3/6/9/12/15/18 GPIO，并执行板级输出总使能限制。
 * 输入：context 为硬件上下文；cylinder_index 为气瓶索引；on 为目标开关状态；pull_in_time_ms 为 12 V 吸合时间。
 * 输出：GPIO 命令成功执行时返回 true；参数、使能或硬件写入失败时返回 false。
 */
bool H_GasPlatform_WriteTestValve(H_Gas_Platform_Context *context,
                                  uint8_t cylinder_index,
                                  bool on,
                                  uint32_t pull_in_time_ms)
{
    bsp_io_port_pin_t pin; // 当前作用域变量，用于保存GPIO引脚。

    //把从 0 开始的气瓶索引转换为对应的测试阀 GPIO 引脚
    if ((context == NULL) || !H_GasPlatform_TestValvePin(cylinder_index, &pin))
    {
        return false;
    }

#if (GAS_BOARD_VALVE_OUTPUTS_ENABLED == 0U)
    if (on)
    {
        return false;
    }
    // 板级输出未使能时只接受关阀命令。
#endif

    //控制单路阀门负端
    return H_GasPlatform_WriteValveOutput(context,
                                           cylinder_index,
                                           pin,
                                           on,
                                           pull_in_time_ms,
                                           &context->test_state[cylinder_index]);
}

/*
 * 函数名：H_GasPlatform_WriteTotalTestValve。
 * 说明：控制VAL_CAL总测试阀；线圈负端由VAL_CAL驱动，正端复用1号阀组VALP1+。
 * 输入：context为硬件上下文；on为目标状态；pull_in_time_ms为12 V强吸合时间。
 * 输出：GPIO命令和1号阀组吸合控制成功执行时返回true，否则返回false。
 */
bool H_GasPlatform_WriteTotalTestValve(H_Gas_Platform_Context *context,
                                       bool on,
                                       uint32_t pull_in_time_ms)
{
    if (context == NULL)
    {
        return false;
    }

#if (GAS_BOARD_VALVE_OUTPUTS_ENABLED == 0U)
    if (on)
    {
        return false;
    }
    // 板级输出未使能时只接受关阀命令。
#endif

    //控制单路阀门负端
    return H_GasPlatform_WriteValveOutput(context,
                                           0U,
                                           VAL_CAL,
                                           on,
                                           pull_in_time_ms,
                                           &context->total_test_state);
}

/*
 * 函数名：H_GasPlatform_ValveTask。
 * 说明：到达吸合截止时间后关闭对应 VAL_Px，并维护同组下一次强吸合脉冲的最早允许时间。
 * 输入：context 为硬件上下文；now_ms 为当前毫秒计数。
 * 输出：所有到期 VAL_Px 均成功关闭时返回 true，否则返回 false 并保留待重试状态。
 */
bool H_GasPlatform_ValveTask(H_Gas_Platform_Context *context, uint32_t now_ms)
{
    uint8_t index; // 当前作用域变量，用于保存遍历索引。
    bool success = true; // success 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示操作失败，true表示操作成功。

    if (context == NULL)
    {
        return false;
    }

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        //使用有符号毫秒差判断当前时间是否到达截止时间
        if (context->boost_interval_active[index] &&
            H_GasPlatform_TimeReached(now_ms, context->boost_available_ms[index]))
        {
            context->boost_interval_active[index] = false;
        }

        //使用有符号毫秒差判断当前时间是否到达截止时间；设置指定气瓶组的 VAL_Px
        if (context->boost_state[index] &&
            H_GasPlatform_TimeReached(now_ms, context->boost_deadline_ms[index]) &&
            !H_GasPlatform_SetValveBoost(context, index, false))
        {
            success = false; // 写入失败时保留 boost_state，下一周期继续尝试关闭 12 V。
        }
    }
    return success;
}

/*
 * 函数名：H_GasPlatform_ValveHasIoError。
 * 说明：查询自上次成功全关后是否发生过真实阀门GPIO写入失败。
 * 输入：context 为只读硬件逻辑层上下文指针。
 * 输出：错误标志已置位或参数无效时返回true，否则返回false。
 */
bool H_GasPlatform_ValveHasIoError(const H_Gas_Platform_Context *context)
{
    return ((context == NULL) || context->valve_io_error);
}

/*
 * 函数名：H_GasPlatform_AllValvesOff。
 * 说明：把板上全部已知阀门GPIO写为关闭电平，仅在全部写入成功后清除阀门状态和IO错误锁存。
 * 输入：context 为硬件逻辑层上下文输入输出指针。
 * 输出：所有阀门GPIO均写入成功时返回true，参数无效或任一写入失败时返回false并保留软件镜像。
 */
bool H_GasPlatform_AllValvesOff(H_Gas_Platform_Context *context)
{
    const bsp_io_port_pin_t all_valve_pins[] =
    {
        VAL1, VAL2, VAL3, VAL_P1, VAL4, VAL5, VAL6, VAL_P2,
        VAL7, VAL8, VAL9, VAL_P3, VAL10, VAL11, VAL12, VAL_P4,
        VAL13, VAL14, VAL15, VAL_P5, VAL16, VAL17, VAL18, VAL_P6, VAL_CAL
    };
    size_t i; // 当前作用域变量，用于保存当前处理数据。
    bool success = true; // 全关结果标志；使用范围：当前函数；false表示至少一路GPIO写入失败，true表示全部成功。

    if (context == NULL)
    {
        return false;
    }

    for (i = 0U; i < (sizeof(all_valve_pins) / sizeof(all_valve_pins[0])); ++i)
    {
        //写入GPIO引脚电平；按照阀门有效电平配置
        if (R_IOPORT_PinWrite(&g_ioport_ctrl,
                              all_valve_pins[i],
                              H_GasPlatform_ValveLevel(false)) != FSP_SUCCESS)
        {
            success = false;
            context->valve_io_error = true;
        }
    }
    // 即使某一路失败也继续尝试其余阀门，最大化紧急关断覆盖范围。

    if (success)
    {
        //初始化或清空内存数据
        (void) memset(context->supply_state, 0, sizeof(context->supply_state));
        //初始化或清空内存数据
        (void) memset(context->exhaust_state, 0, sizeof(context->exhaust_state));
        //初始化或清空内存数据
        (void) memset(context->test_state, 0, sizeof(context->test_state));
        context->total_test_state = false;
        //初始化或清空内存数据
        (void) memset(context->boost_state, 0, sizeof(context->boost_state));
        //初始化或清空内存数据
        (void) memset(context->boost_deadline_ms, 0, sizeof(context->boost_deadline_ms));
        context->valve_io_error = false;
        // 运行中全关不清除强吸合最短间隔，防止停止后立即重启绕过线圈保护。
    }
    return success;
}

/*
 * 函数名：rs485_sensor_callback。
 * 说明：处理 SCI1 的发送完成、接收完成和错误事件，并在发送结束后恢复接收方向。
 * 输入：p_args 为 FSP 串口事件参数，其中 p_context 指向当前硬件实例上下文。
 * 输出：无；通过回调上下文更新收发完成标志和串口错误标志。
 */
void rs485_sensor_callback(uart_callback_args_t *p_args)
{
    H_Gas_Platform_Context *context; // 当前作用域变量，用于保存模块上下文指针。

    if ((p_args == NULL) || (p_args->p_context == NULL))
    {
        return;
    }
    context = (H_Gas_Platform_Context *) p_args->p_context;

    if (p_args->event == UART_EVENT_TX_COMPLETE)
    {
        context->sensor_tx_done = true;
        //把压力传感器 RS485 收发器切换到接收状态
        if (!H_GasPlatform_SensorDirectionReceive())
        {
            context->sensor_uart_error = true;
        }
    }
    else if (p_args->event == UART_EVENT_RX_COMPLETE)
    {
        context->sensor_rx_done = true;
    }
    else if ((p_args->event & (UART_EVENT_ERR_PARITY |
                               UART_EVENT_ERR_FRAMING |
                               UART_EVENT_ERR_OVERFLOW |
                               UART_EVENT_BREAK_DETECT)) != 0U)
    {
        context->sensor_uart_error = true;
        //把压力传感器 RS485 收发器切换到接收状态
        (void) H_GasPlatform_SensorDirectionReceive();
    }
    else
    {
        __NOP();
        // 未启动定长接收时产生的单字符事件不参与本主站事务。
    }
}
