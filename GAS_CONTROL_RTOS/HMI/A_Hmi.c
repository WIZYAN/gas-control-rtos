/*
 * Version: v1.12
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现串口屏实时监控、按钮事件、RTC和状态刷新业务。
 */

#include "A_Hmi.h"

#include <stddef.h>
#include <string.h>

/*
 * 函数名：A_Hmi_TimeReached。
 * 说明：使用有符号毫秒差判断当前时间是否已经到达截止时间。
 * 输入：now_ms 为当前时间；deadline_ms 为截止时间。
 * 输出：已经到达或超过截止时间时返回 true，否则返回 false。
 */
static bool A_Hmi_TimeReached(uint32_t now_ms, uint32_t deadline_ms)
{
    return ((int32_t) (now_ms - deadline_ms) >= 0);
}

/*
 * 函数名：A_Hmi_FormatUnsigned。
 * 说明：把无符号整数转换成不带前导零的 ASCII 十进制文本。
 * 输入：value 为待转换数值；text 为输出缓冲区；capacity 为缓冲区容量。
 * 输出：返回写入的字符数量，容量不足时返回 0。
 */
static size_t A_Hmi_FormatUnsigned(uint32_t value, char *text, size_t capacity)
{
    char reverse[10]; // 当前作用域变量，用于保存当前处理数据数组。
    size_t count = 0U; // 当前作用域变量，用于保存数量计数。
    size_t index; // 当前作用域变量，用于保存遍历索引。

    do
    {
        reverse[count++] = (char) ('0' + (value % 10U));
        value /= 10U;
    } while ((value > 0U) && (count < sizeof(reverse)));

    if (count > capacity)
    {
        return 0U;
    }
    for (index = 0U; index < count; ++index)
    {
        text[index] = reverse[count - index - 1U];
    }
    return count;
}

/*
 * 函数名：A_Hmi_FormatPressure。
 * 说明：把有效或超量程的 MPa 压力格式化为三位小数ASCII文本，其他质量输出“--”。
 * 输入：pressure_mpa 为压力值；quality 为压力质量；text 为输出缓冲区；capacity 为缓冲区容量。
 * 输出：返回写入的字符数量。
 */
static size_t A_Hmi_FormatPressure(float pressure_mpa,
                                   gas_pressure_quality_t quality,
                                   char *text,
                                   size_t capacity)
{
    uint32_t raw; // 当前作用域变量，用于保存当前处理数据。
    uint32_t fraction; // 当前作用域变量，用于保存当前处理数据。
    size_t length; // 当前作用域变量，用于保存有效数据长度。

    if ((text == NULL) || (capacity < 2U))
    {
        return 0U;
    }
    if (((quality != GAS_PRESSURE_VALID) && (quality != GAS_PRESSURE_OUT_OF_RANGE)) ||
        !(pressure_mpa >= 0.0F) || (pressure_mpa > GAS_PRESSURE_DISPLAY_MAX_MPA))
    {
        text[0] = '-';
        text[1] = '-';
        // 无效、过期或不可表达的异常压力统一显示为“--”，避免把旧值或非法浮点数当成实时压力。
        return 2U;
    }

    raw = (uint32_t) ((pressure_mpa * 1000.0F) + 0.5F);
    // 放大 1000 倍后用整数拆分，可避免在嵌入式工程中引入体积较大的浮点 printf。
    fraction = raw % 1000U;
    length = A_Hmi_FormatUnsigned(raw / 1000U, text, capacity);
    if ((length == 0U) || ((length + 4U) > capacity))
    {
        return 0U;
    }
    text[length++] = '.';
    text[length++] = (char) ('0' + (fraction / 100U));
    text[length++] = (char) ('0' + ((fraction / 10U) % 10U));
    text[length++] = (char) ('0' + (fraction % 10U));
    return length;
}

/*
 * 函数名：A_Hmi_CopyGbkText。
 * 说明：把指定长度的固定 GBK 文本复制到大彩文本控件发送缓冲区，不追加字符串结束符。
 * 输入：source 为 GBK 源文本；source_length 为源文本字节数；text 为输出缓冲区；capacity 为缓冲区容量。
 * 输出：复制成功时返回 GBK 文本字节数，输入无效或容量不足时返回 0。
 */
static size_t A_Hmi_CopyGbkText(const char *source,
                                size_t source_length,
                                char *text,
                                size_t capacity)
{
    if ((source == NULL) || (text == NULL) || (source_length == 0U) ||
        (source_length > capacity))
    {
        return 0U;
    }

    (void) memcpy(text, source, source_length);
    // 大彩写文本帧由长度字段和帧尾确定内容边界，因此发送缓冲区不需要追加 '\0'。
    return source_length;
}

/*
 * 函数名：A_Hmi_FormatCylinderState。
 * 说明：把气瓶状态转换为串口屏控件使用的固定 GBK 中文文本。
 * 输入：state 为气瓶状态；text 为输出缓冲区；capacity 为缓冲区容量。
 * 输出：返回写入的 GBK 文本字节数，输入无效或容量不足时返回 0。
 */
static size_t A_Hmi_FormatCylinderState(gas_cylinder_state_t state,
                                        char *text,
                                        size_t capacity)
{
    const char *state_text; // 当前作用域变量，用于保存业务状态指针。

    switch (state)
    {
        case GAS_CYL_INIT:
            state_text = "\xB3\xF5\xCA\xBC\xBB\xAF"; // 初始化。
            break;

        case GAS_CYL_READY:
            state_text = "\xB4\xFD\xD3\xC3"; // 待用。
            break;

        case GAS_CYL_ACTIVE:
            state_text = "\xCA\xB9\xD3\xC3"; // 使用。
            break;

        case GAS_CYL_LOW_REPLACE:
            state_text = "\xB5\xCD\xD1\xB9\xB4\xFD\xBB\xBB"; // 低压待换。
            break;

        case GAS_CYL_LOW_WARNING:
            state_text = "\xB5\xCD\xD1\xB9\xBE\xAF\xB8\xE6"; // 低压警告。
            break;

        case GAS_CYL_DISABLED:
            state_text = "\xCD\xA3\xD3\xC3"; // 停用。
            break;

        case GAS_CYL_WAIT_TEST:
            state_text = "\xB4\xFD\xB2\xE2\xCA\xD4"; // 待测试。
            break;

        default:
            state_text = "\xCE\xB4\xD6\xAA"; // 未知，用于防止异常枚举值显示成正常状态。
            break;
    }

    return A_Hmi_CopyGbkText(state_text, strlen(state_text), text, capacity);
}

/*
 * 函数名：A_Hmi_GetHighlightFrame。
 * 说明：把气瓶业务状态映射为实时监控卡片的三帧高亮图标索引。
 * 输入：state为当前气瓶状态。
 * 输出：使用状态返回绿色帧，低压警告返回红色帧，其他状态返回普通透明帧。
 */
static uint8_t A_Hmi_GetHighlightFrame(gas_cylinder_state_t state)
{
    if (state == GAS_CYL_ACTIVE)
    {
        return A_HMI_HIGHLIGHT_FRAME_ACTIVE;
    }
    if (state == GAS_CYL_LOW_WARNING)
    {
        return A_HMI_HIGHLIGHT_FRAME_WARNING;
    }
    return A_HMI_HIGHLIGHT_FRAME_NORMAL;
}

/*
 * 函数名：A_Hmi_Init。
 * 说明：初始化大彩 DC10600PM101 串口屏应用实例。
 * 输入：context 为 HMI 应用层上下文输入输出指针。
 * 输出：初始化成功时返回 true，否则返回 false。
 */
bool A_Hmi_Init(A_Hmi_Context *context)
{
    if (context == NULL)
    {
        return false;
    }

    (void) memset(context, 0, sizeof(*context));
    context->ready = F_Hmi_Init(&context->function);
    return context->ready;
}

/*
 * 函数名：A_Hmi_Task。
 * 说明：周期解析按钮和RTC响应、更新系统时间，并每秒请求一次串口屏全局RTC。
 * 输入：context 为 HMI 应用层上下文；system 为气源系统输入输出指针；now_ms 为当前毫秒计数。
 * 输出：无；按钮事件保存在内部队列，RTC响应写入 system 的系统日期时间。
 */
void A_Hmi_Task(A_Hmi_Context *context, Gas_System *system, uint32_t now_ms)
{
    F_Hmi_Rtc_Time rtc_time; // 当前作用域变量，用于保存当前处理数据。

    if ((context == NULL) || (system == NULL) || !context->ready)
    {
        return;
    }

    F_Hmi_Task(&context->function);
    if (F_Hmi_TakeRtcTime(&context->function, &rtc_time))
    {
        system->date_time.year = rtc_time.year;
        system->date_time.month = rtc_time.month;
        system->date_time.day = rtc_time.day;
        system->date_time.week = rtc_time.week;
        system->date_time.hour = rtc_time.hour;
        system->date_time.minute = rtc_time.minute;
        system->date_time.second = rtc_time.second;
        system->date_time.source_timestamp_ms = now_ms;
        system->date_time.valid = true;
        // 后续日志只能在 valid 为 true 时使用该日期时间，避免记录未经校验的数据。
    }

    if (system->date_time.valid &&
        A_Hmi_TimeReached(now_ms,
                          system->date_time.source_timestamp_ms + A_HMI_RTC_STALE_TIME_MS))
    {
        system->date_time.valid = false;
        // 超过规定时间未收到新 RTC 响应时使时间失效，日志模块将暂停写入带时间记录。
    }

    if (A_Hmi_TimeReached(now_ms, context->next_rtc_read_ms) &&
        F_Hmi_SendReadRtc(&context->function))
    {
        context->next_rtc_read_ms = now_ms + A_HMI_RTC_READ_INTERVAL_MS;
    }
}

/*
 * 函数名：A_Hmi_TakeButtonEvent。
 * 说明：取出一条串口屏按钮或下拉菜单选择事件。
 * 输入：context 为 HMI 上下文；button_id 为控件 ID 输出指针；value 为按钮状态或菜单选中项索引输出指针。
 * 输出：存在待处理事件时返回 true，否则返回 false。
 */
bool A_Hmi_TakeButtonEvent(A_Hmi_Context *context, uint16_t *button_id, uint8_t *value)
{
    return ((context != NULL) && context->ready &&
            F_Hmi_TakeButtonEvent(&context->function, button_id, value));
}

/*
 * 函数名：A_Hmi_RequestExhaustSync。
 * 说明：请求优先把指定气瓶的MCU实际排气命令回写到串口屏1～6号排气按钮。
 * 输入：context为HMI上下文；index为从0开始的气瓶索引。
 * 输出：无；参数有效时设置待同步位，实际发送由A_Hmi_Refresh分时完成。
 */
void A_Hmi_RequestExhaustSync(A_Hmi_Context *context, uint8_t index)
{
    if ((context == NULL) || (index >= GAS_CYLINDER_COUNT))
    {
        return;
    }

    context->exhaust_refresh_pending_bits |= (uint8_t) (1U << index);
}

/*
 * 函数名：A_Hmi_RequestTestSync。
 * 说明：请求优先把指定气瓶的MCU实际测试阀命令回写到串口屏7～12号测试阀按钮。
 * 输入：context为HMI上下文；index为从0开始的气瓶索引。
 * 输出：无；参数有效时设置待同步位，实际发送由A_Hmi_Refresh分时完成。
 */
void A_Hmi_RequestTestSync(A_Hmi_Context *context, uint8_t index)
{
    if ((context == NULL) || (index >= GAS_CYLINDER_COUNT))
    {
        return;
    }

    context->test_refresh_pending_bits |= (uint8_t) (1U << index);
}

/*
 * 函数名：A_Hmi_RequestDisableSync。
 * 说明：请求优先把指定气瓶的MCU停用状态回写到串口屏13～18号停用开关。
 * 输入：context为HMI上下文；index为从0开始的气瓶索引。
 * 输出：无；参数有效时设置待同步位，实际发送由A_Hmi_Refresh分时完成。
 */
void A_Hmi_RequestDisableSync(A_Hmi_Context *context, uint8_t index)
{
    if ((context == NULL) || (index >= GAS_CYLINDER_COUNT))
    {
        return;
    }

    context->disable_refresh_pending_bits |= (uint8_t) (1U << index);
}

/*
 * 函数名：A_Hmi_RequestQualificationSync。
 * 说明：请求优先把指定气瓶的MCU测试通过标志回写到串口屏51～56号开关。
 * 输入：context为HMI上下文；index为从0开始的气瓶索引。
 * 输出：无；参数有效时设置待同步位，实际发送由A_Hmi_Refresh分时完成。
 */
void A_Hmi_RequestQualificationSync(A_Hmi_Context *context, uint8_t index)
{
    if ((context == NULL) || (index >= GAS_CYLINDER_COUNT))
    {
        return;
    }

    context->qualification_refresh_pending_bits |= (uint8_t) (1U << index);
}

/*
 * 函数名：A_Hmi_TakeTextEvent。
 * 说明：取出一条串口屏文本输入控件上传的ASCII参数文本。
 * 输入：context为HMI上下文；page_id和control_id为控件标识输出；text为输出缓存；capacity为容量。
 * 输出：存在完整事件时返回文本长度，否则返回0。
 */
size_t A_Hmi_TakeTextEvent(A_Hmi_Context *context,
                           uint16_t *page_id,
                           uint16_t *control_id,
                           char *text,
                           size_t capacity)
{
    if ((context == NULL) || !context->ready)
    {
        return 0U;
    }
    return F_Hmi_TakeTextEvent(&context->function,
                               page_id,
                               control_id,
                               text,
                               capacity);
}

/*
 * 函数名：A_Hmi_PeekTextEvent。
 * 说明：查看串口屏待处理文本输入的画面和控件ID，但不取走文本。
 * 输入：context为HMI上下文；page_id和control_id为控件标识输出指针。
 * 输出：存在待处理文本事件时返回true，否则返回false。
 */
bool A_Hmi_PeekTextEvent(const A_Hmi_Context *context,
                         uint16_t *page_id,
                         uint16_t *control_id)
{
    return ((context != NULL) && context->ready &&
            F_Hmi_PeekTextEvent(&context->function, page_id, control_id));
}

/*
 * 函数名：A_Hmi_SendText。
 * 说明：向指定串口屏画面的文本控件发送ASCII或GBK字节文本。
 * 输入：context为HMI上下文；page_id和control_id指定控件；text为文本；length为字节数。
 * 输出：成功启动异步发送时返回true，否则返回false。
 */
bool A_Hmi_SendText(A_Hmi_Context *context,
                    uint16_t page_id,
                    uint16_t control_id,
                    const char *text,
                    size_t length)
{
    return ((context != NULL) && context->ready &&
            F_Hmi_SendText(&context->function, page_id, control_id, text, length));
}

/*
 * 函数名：A_Hmi_Refresh。
 * 说明：分时刷新压力、气瓶状态、十八路双色阀位、四组六路操作按钮及卡片高亮。
 * 输入：context 为 HMI 上下文；system 为只读气源系统；now_ms 为当前毫秒计数。
 * 输出：无；每次最多启动一帧异步发送。
 */
void A_Hmi_Refresh(A_Hmi_Context *context, const Gas_System *system, uint32_t now_ms)
{
    char text[16]; // 当前作用域变量，用于保存显示文本缓冲区或长度。
    uint16_t control_id; // 当前作用域变量，用于保存串口屏控件标识。
    uint8_t index; // 当前作用域变量，用于保存遍历索引。
    uint8_t supply_bits = 0U; // 当前刷新函数使用的供气阀位图；bit0～bit5分别对应1～6号瓶，0表示关阀，1表示开阀，bit6～bit7保留为0。
    uint8_t exhaust_bits = 0U; // 当前刷新函数使用的排气阀位图；bit0～bit5分别对应1～6号瓶，0表示关阀，1表示开阀，bit6～bit7保留为0。
    uint8_t test_bits = 0U; // 当前刷新函数使用的测试阀位图；bit0～bit5分别对应1～6号瓶，0表示关阀，1表示开阀，bit6～bit7保留为0。
    uint8_t disable_bits = 0U; // 当前刷新函数使用的停用状态位图；bit0～bit5分别对应1～6号瓶，0表示启用，1表示停用，bit6～bit7保留为0。
    uint8_t qualification_bits = 0U; // 当前刷新函数使用的测试合格位图；bit0～bit5分别对应1～6号瓶，0表示未合格，1表示已合格，bit6～bit7保留为0。
    uint8_t changed_bits; // 当前刷新函数使用的差异位图；bit0～bit5分别对应1～6号瓶，0表示对应状态未变化，1表示对应状态需要回写，bit6～bit7保留为0。
    size_t length = 0U; // 当前作用域变量，用于保存有效数据长度。
    bool out_of_range; // out_of_range 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示数值在允许范围内，true表示数值超出允许范围。
    bool sent = false; // sent 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示尚未发送成功，true表示已经发送成功。

    if ((context == NULL) || (system == NULL) || !context->ready)
    {
        return;
    }

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        if (system->cylinder[index].supply_cmd)
        {
            supply_bits |= (uint8_t) (1U << index);
        }
        if (system->cylinder[index].exhaust_cmd)
        {
            exhaust_bits |= (uint8_t) (1U << index);
        }
        if (system->cylinder[index].test_cmd)
        {
            test_bits |= (uint8_t) (1U << index);
        }
        if (system->cylinder[index].state == GAS_CYL_DISABLED)
        {
            disable_bits |= (uint8_t) (1U << index);
        }
        if (system->cylinder[index].qualification_passed)
        {
            qualification_bits |= (uint8_t) (1U << index);
        }
    }
    if (!context->supply_valve_refresh_initialized)
    {
        context->supply_valve_refresh_value_bits = supply_bits;
        context->supply_valve_refresh_pending_bits = supply_bits;
        context->supply_valve_refresh_initialized = true;
    }
    else
    {
        changed_bits = (uint8_t) (context->supply_valve_refresh_value_bits ^ supply_bits);
        context->supply_valve_refresh_value_bits = supply_bits;
        context->supply_valve_refresh_pending_bits |= changed_bits;
    }
    // 进气阀变化时优先切换双色图标；首次全关无需发送，因为画面默认帧就是“关闭”。

    if (!context->exhaust_refresh_initialized)
    {
        context->exhaust_refresh_value_bits = exhaust_bits;
        context->exhaust_refresh_pending_bits = (uint8_t) ((1U << GAS_CYLINDER_COUNT) - 1U);
        context->exhaust_valve_refresh_pending_bits = exhaust_bits;
        context->exhaust_refresh_initialized = true;
    }
    else
    {
        changed_bits = (uint8_t) (context->exhaust_refresh_value_bits ^ exhaust_bits);
        context->exhaust_refresh_value_bits = exhaust_bits;
        context->exhaust_refresh_pending_bits |= changed_bits;
        context->exhaust_valve_refresh_pending_bits |= changed_bits;
    }
    // 实际排气命令变化时建立优先回写队列，按钮只负责发起定时排气请求，不直接决定阀门状态。

    if (!context->test_refresh_initialized)
    {
        context->test_refresh_value_bits = test_bits;
        context->test_refresh_pending_bits = (uint8_t) ((1U << GAS_CYLINDER_COUNT) - 1U);
        context->test_valve_refresh_pending_bits = test_bits;
        context->test_refresh_initialized = true;
    }
    else
    {
        changed_bits = (uint8_t) (context->test_refresh_value_bits ^ test_bits);
        context->test_refresh_value_bits = test_bits;
        context->test_refresh_pending_bits |= changed_bits;
        context->test_valve_refresh_pending_bits |= changed_bits;
    }
    // 测试阀实际命令变化时优先校正按钮，包含CAN控制、自动超时和安全关断等所有来源。

    if (!context->disable_refresh_initialized)
    {
        context->disable_refresh_value_bits = disable_bits;
        context->disable_refresh_pending_bits = (uint8_t) ((1U << GAS_CYLINDER_COUNT) - 1U);
        context->disable_refresh_initialized = true;
    }
    else
    {
        changed_bits = (uint8_t) (context->disable_refresh_value_bits ^ disable_bits);
        context->disable_refresh_value_bits = disable_bits;
        context->disable_refresh_pending_bits |= changed_bits;
    }
    // 停用按钮只反映气瓶是否真正处于停用状态，不直接采用CAN或串口屏的请求值。

    if (!context->qualification_refresh_initialized)
    {
        context->qualification_refresh_value_bits = qualification_bits;
        context->qualification_refresh_pending_bits = (uint8_t) ((1U << GAS_CYLINDER_COUNT) - 1U);
        context->qualification_refresh_initialized = true;
    }
    else
    {
        changed_bits = (uint8_t) (context->qualification_refresh_value_bits ^ qualification_bits);
        context->qualification_refresh_value_bits = qualification_bits;
        context->qualification_refresh_pending_bits |= changed_bits;
    }
    // MCU标志发生变化时建立优先回写队列，串口屏本地按钮状态不作为业务判断依据。

    if (!A_Hmi_TimeReached(now_ms, context->next_refresh_ms))
    {
        return;
    }

    if (context->exhaust_refresh_pending_bits != 0U)
    {
        for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
        {
            if ((context->exhaust_refresh_pending_bits & (uint8_t) (1U << index)) != 0U)
            {
                break;
            }
        }
        sent = F_Hmi_SendButtonState(&context->function,
                                     A_HMI_MONITOR_PAGE_ID,
                                     (uint16_t) (A_HMI_EXHAUST_BUTTON_BASE + index),
                                     (exhaust_bits & (uint8_t) (1U << index)) != 0U);
        if (sent)
        {
            context->exhaust_refresh_pending_bits &= (uint8_t) ~(uint8_t) (1U << index);
            context->next_refresh_ms = now_ms + A_HMI_REFRESH_GAP_MS;
        }
        // 每次只校正一个排气按钮，开阀、到时关阀或互锁拒绝后均优先恢复为MCU实际状态。
        return;
    }

    if (context->test_refresh_pending_bits != 0U)
    {
        for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
        {
            if ((context->test_refresh_pending_bits & (uint8_t) (1U << index)) != 0U)
            {
                break;
            }
        }
        sent = F_Hmi_SendButtonState(&context->function,
                                     A_HMI_MONITOR_PAGE_ID,
                                     (uint16_t) (A_HMI_TEST_BUTTON_BASE + index),
                                     (test_bits & (uint8_t) (1U << index)) != 0U);
        if (sent)
        {
            context->test_refresh_pending_bits &= (uint8_t) ~(uint8_t) (1U << index);
            context->next_refresh_ms = now_ms + A_HMI_REFRESH_GAP_MS;
        }
        // 每次只校正一个测试阀按钮，互锁拒绝或一分钟自动关闭后均恢复为MCU实际状态。
        return;
    }

    if (context->disable_refresh_pending_bits != 0U)
    {
        for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
        {
            if ((context->disable_refresh_pending_bits & (uint8_t) (1U << index)) != 0U)
            {
                break;
            }
        }
        sent = F_Hmi_SendButtonState(&context->function,
                                     A_HMI_MONITOR_PAGE_ID,
                                     (uint16_t) (A_HMI_DISABLE_BUTTON_BASE + index),
                                     (disable_bits & (uint8_t) (1U << index)) != 0U);
        if (sent)
        {
            context->disable_refresh_pending_bits &= (uint8_t) ~(uint8_t) (1U << index);
            context->next_refresh_ms = now_ms + A_HMI_REFRESH_GAP_MS;
        }
        // 停用和重新启用请求执行后优先回写，失败时也会把屏端开关校正到真实业务状态。
        return;
    }

    if (context->qualification_refresh_pending_bits != 0U)
    {
        for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
        {
            if ((context->qualification_refresh_pending_bits & (uint8_t) (1U << index)) != 0U)
            {
                break;
            }
        }
        sent = F_Hmi_SendButtonState(&context->function,
                                     A_HMI_MONITOR_PAGE_ID,
                                     (uint16_t) (A_HMI_QUALIFIED_BUTTON_BASE + index),
                                     (qualification_bits & (uint8_t) (1U << index)) != 0U);
        if (sent)
        {
            context->qualification_refresh_pending_bits &= (uint8_t) ~(uint8_t) (1U << index);
            context->next_refresh_ms = now_ms + A_HMI_REFRESH_GAP_MS;
        }
        // 每次只校正一个测试通过开关，自动清零或非法点击后优先于普通监控数据恢复画面。
        return;
    }

    if (context->supply_valve_refresh_pending_bits != 0U)
    {
        for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
        {
            if ((context->supply_valve_refresh_pending_bits & (uint8_t) (1U << index)) != 0U)
            {
                break;
            }
        }
        sent = F_Hmi_SendIconFrame(&context->function,
                                   A_HMI_MONITOR_PAGE_ID,
                                   (uint16_t) (A_HMI_SUPPLY_ICON_BASE + index),
                                   ((supply_bits & (uint8_t) (1U << index)) != 0U) ?
                                   A_HMI_VALVE_FRAME_OPEN : A_HMI_VALVE_FRAME_CLOSED);
        if (sent)
        {
            context->supply_valve_refresh_pending_bits &= (uint8_t) ~(uint8_t) (1U << index);
            context->next_refresh_ms = now_ms + A_HMI_REFRESH_GAP_MS;
        }
        // 自动开关进气阀后优先更新颜色，画面只反映MCU命令状态。
        return;
    }

    if (context->exhaust_valve_refresh_pending_bits != 0U)
    {
        for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
        {
            if ((context->exhaust_valve_refresh_pending_bits & (uint8_t) (1U << index)) != 0U)
            {
                break;
            }
        }
        sent = F_Hmi_SendIconFrame(&context->function,
                                   A_HMI_MONITOR_PAGE_ID,
                                   (uint16_t) (A_HMI_EXHAUST_ICON_BASE + index),
                                   ((exhaust_bits & (uint8_t) (1U << index)) != 0U) ?
                                   A_HMI_VALVE_FRAME_OPEN : A_HMI_VALVE_FRAME_CLOSED);
        if (sent)
        {
            context->exhaust_valve_refresh_pending_bits &= (uint8_t) ~(uint8_t) (1U << index);
            context->next_refresh_ms = now_ms + A_HMI_REFRESH_GAP_MS;
        }
        // 排气阀开启、到时关闭或安全关断后优先更新对应双色图标。
        return;
    }

    if (context->test_valve_refresh_pending_bits != 0U)
    {
        for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
        {
            if ((context->test_valve_refresh_pending_bits & (uint8_t) (1U << index)) != 0U)
            {
                break;
            }
        }
        sent = F_Hmi_SendIconFrame(&context->function,
                                   A_HMI_MONITOR_PAGE_ID,
                                   (uint16_t) (A_HMI_TEST_ICON_BASE + index),
                                   ((test_bits & (uint8_t) (1U << index)) != 0U) ?
                                   A_HMI_VALVE_FRAME_OPEN : A_HMI_VALVE_FRAME_CLOSED);
        if (sent)
        {
            context->test_valve_refresh_pending_bits &= (uint8_t) ~(uint8_t) (1U << index);
            context->next_refresh_ms = now_ms + A_HMI_REFRESH_GAP_MS;
        }
        // 测试阀开启、超时关闭或安全关断后优先更新对应双色图标。
        return;
    }

    // 68个刷新槽按顺序轮转，每个任务周期最多发送一项，防止刷新流量堵塞按钮和RTC通信。
    if (context->refresh_slot < 12U)
    {
        index = (uint8_t) (context->refresh_slot / 2U);
        if ((context->refresh_slot & 1U) == 0U)
        {
            context->pressure_refresh_mpa[index] = system->cylinder[index].pressure_mpa;
            context->pressure_refresh_quality[index] = system->cylinder[index].pressure_quality;
            out_of_range = (context->pressure_refresh_quality[index] == GAS_PRESSURE_OUT_OF_RANGE);
            control_id = (uint16_t) ((out_of_range ? A_HMI_PRESSURE_TEXT_BASE :
                                                   A_HMI_PRESSURE_OVERRANGE_TEXT_BASE) + index);
            text[0] = ' ';
            length = 1U;
            // 先清空当前不使用的颜色层，下一槽再写入目标层，避免青色与红色数字重叠混色。
        }
        else
        {
            out_of_range = (context->pressure_refresh_quality[index] == GAS_PRESSURE_OUT_OF_RANGE);
            control_id = (uint16_t) ((out_of_range ? A_HMI_PRESSURE_OVERRANGE_TEXT_BASE :
                                                   A_HMI_PRESSURE_TEXT_BASE) + index);
            length = A_Hmi_FormatPressure(context->pressure_refresh_mpa[index],
                                          context->pressure_refresh_quality[index],
                                          text,
                                          sizeof(text));
        }
    }
    else if (context->refresh_slot < 18U)
    {
        index = (uint8_t) (context->refresh_slot - 12U);
        control_id = (uint16_t) (A_HMI_STATE_TEXT_BASE + index);
        length = A_Hmi_FormatCylinderState(system->cylinder[index].state, text, sizeof(text));
    }
    else if (context->refresh_slot < 24U)
    {
        index = (uint8_t) (context->refresh_slot - 18U);
        sent = F_Hmi_SendIconFrame(&context->function,
                                   A_HMI_MONITOR_PAGE_ID,
                                   (uint16_t) (A_HMI_SUPPLY_ICON_BASE + index),
                                   system->cylinder[index].supply_cmd ?
                                   A_HMI_VALVE_FRAME_OPEN : A_HMI_VALVE_FRAME_CLOSED);
    }
    else if (context->refresh_slot < 30U)
    {
        index = (uint8_t) (context->refresh_slot - 24U);
        sent = F_Hmi_SendIconFrame(&context->function,
                                   A_HMI_MONITOR_PAGE_ID,
                                   (uint16_t) (A_HMI_EXHAUST_ICON_BASE + index),
                                   system->cylinder[index].exhaust_cmd ?
                                   A_HMI_VALVE_FRAME_OPEN : A_HMI_VALVE_FRAME_CLOSED);
    }
    else if (context->refresh_slot < 36U)
    {
        index = (uint8_t) (context->refresh_slot - 30U);
        sent = F_Hmi_SendIconFrame(&context->function,
                                   A_HMI_MONITOR_PAGE_ID,
                                   (uint16_t) (A_HMI_TEST_ICON_BASE + index),
                                   system->cylinder[index].test_cmd ?
                                   A_HMI_VALVE_FRAME_OPEN : A_HMI_VALVE_FRAME_CLOSED);
    }
    else if (context->refresh_slot == 36U)
    {
        context->pressure_refresh_mpa[GAS_TOTAL_PRESSURE_SENSOR_INDEX] =
                system->total_pressure.pressure_mpa;
        context->pressure_refresh_quality[GAS_TOTAL_PRESSURE_SENSOR_INDEX] =
                system->total_pressure.pressure_quality;
        out_of_range = (context->pressure_refresh_quality[GAS_TOTAL_PRESSURE_SENSOR_INDEX] ==
                        GAS_PRESSURE_OUT_OF_RANGE);
        control_id = out_of_range ? A_HMI_TOTAL_PRESSURE_TEXT :
                                    A_HMI_TOTAL_PRESSURE_OVERRANGE_TEXT; // 当前作用域变量，用于保存当前处理数据。
        text[0] = ' ';
        length = 1U;
        // 总管压力同样先清空非目标颜色层，保证画面中任意时刻只有一种颜色承担有效数值。
    }
    else if (context->refresh_slot == 37U)
    {
        out_of_range = (context->pressure_refresh_quality[GAS_TOTAL_PRESSURE_SENSOR_INDEX] ==
                        GAS_PRESSURE_OUT_OF_RANGE);
        control_id = out_of_range ? A_HMI_TOTAL_PRESSURE_OVERRANGE_TEXT :
                                    A_HMI_TOTAL_PRESSURE_TEXT; // 当前作用域变量，用于保存当前处理数据。
        length = A_Hmi_FormatPressure(context->pressure_refresh_mpa[GAS_TOTAL_PRESSURE_SENSOR_INDEX],
                                      context->pressure_refresh_quality[GAS_TOTAL_PRESSURE_SENSOR_INDEX],
                                      text,
                                      sizeof(text));
    }
    else if (context->refresh_slot < 44U)
    {
        index = (uint8_t) (context->refresh_slot - 38U);
        control_id = (uint16_t) (A_HMI_HIGHLIGHT_ICON_BASE + index);
        sent = F_Hmi_SendIconFrame(&context->function,
                                   A_HMI_MONITOR_PAGE_ID,
                                   control_id,
                                   A_Hmi_GetHighlightFrame(system->cylinder[index].state));
        // 高亮层只在使用和低压警告状态显示，其余五种状态统一切回透明普通帧。
    }
    else if (context->refresh_slot < 50U)
    {
        index = (uint8_t) (context->refresh_slot - 44U);
        sent = F_Hmi_SendButtonState(&context->function,
                                     A_HMI_MONITOR_PAGE_ID,
                                     (uint16_t) (A_HMI_EXHAUST_BUTTON_BASE + index),
                                     system->cylinder[index].exhaust_cmd);
        // 周期重申实际排气状态，使串口屏重启、切页或偶发丢帧后恢复“人工排气/排气中”。
    }
    else if (context->refresh_slot < 56U)
    {
        index = (uint8_t) (context->refresh_slot - 50U);
        sent = F_Hmi_SendButtonState(&context->function,
                                     A_HMI_MONITOR_PAGE_ID,
                                     (uint16_t) (A_HMI_TEST_BUTTON_BASE + index),
                                     system->cylinder[index].test_cmd);
        // 周期重申测试阀实际命令，确保自动超时、屏幕重启或偶发丢帧后按钮状态一致。
    }
    else if (context->refresh_slot < 62U)
    {
        index = (uint8_t) (context->refresh_slot - 56U);
        sent = F_Hmi_SendButtonState(&context->function,
                                     A_HMI_MONITOR_PAGE_ID,
                                     (uint16_t) (A_HMI_DISABLE_BUTTON_BASE + index),
                                     system->cylinder[index].state == GAS_CYL_DISABLED);
        // 周期重申实际停用状态，重新启用进入初始化后会自动把开关恢复为正常使用。
    }
    else if (context->refresh_slot < 68U)
    {
        index = (uint8_t) (context->refresh_slot - 62U);
        sent = F_Hmi_SendButtonState(&context->function,
                                     A_HMI_MONITOR_PAGE_ID,
                                     (uint16_t) (A_HMI_QUALIFIED_BUTTON_BASE + index),
                                     system->cylinder[index].qualification_passed);
        // 周期回写用于串口屏重启、页面切换或偶发丢帧后的最终一致性校正。
    }

    if (!sent && (length > 0U))
    {
        sent = F_Hmi_SendText(&context->function,
                              A_HMI_MONITOR_PAGE_ID,
                              control_id,
                              text,
                              length);
    }

    if (sent)
    {
        context->refresh_slot = (uint8_t) ((context->refresh_slot + 1U) % A_HMI_REFRESH_SLOT_COUNT);
        context->next_refresh_ms = now_ms + A_HMI_REFRESH_GAP_MS;
        // 只有底层成功接收发送请求后才推进槽位，串口忙时保留当前项等待下次重试。
    }
}
