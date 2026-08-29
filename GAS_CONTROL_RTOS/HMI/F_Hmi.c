#include "F_Hmi.h"

#include <string.h>

/*
 * 函数名：F_Hmi_FrameComplete。
 * 说明：检查当前接收缓存是否以大彩固定帧尾 FF FC FF FF 结束。
 * 输入：context 为只读 HMI 功能层上下文指针。
 * 输出：已经收到完整帧尾时返回 true，否则返回 false。
 */
static bool F_Hmi_FrameComplete(const F_Hmi_Context *context)
{
    uint16_t length;

    if ((context == NULL) || (context->rx_length < 5U))
    {
        return false;
    }
    length = context->rx_length;
    return ((context->rx_frame[length - 4U] == 0xFFU) &&
            (context->rx_frame[length - 3U] == 0xFCU) &&
            (context->rx_frame[length - 2U] == 0xFFU) &&
            (context->rx_frame[length - 1U] == 0xFFU));
}

/*
 * 函数名：F_Hmi_BcdToBinary。
 * 说明：把一个压缩 BCD 字节转换为二进制数值，并检查十进制数位和允许范围。
 * 输入：bcd 为压缩 BCD；minimum 为最小允许值；maximum 为最大允许值；value 为转换结果输出指针。
 * 输出：BCD 数位和数值范围均合法时返回 true，否则返回 false。
 */
static bool F_Hmi_BcdToBinary(uint8_t bcd,
                              uint8_t minimum,
                              uint8_t maximum,
                              uint8_t *value)
{
    uint8_t high;
    uint8_t low;
    uint8_t binary;

    if (value == NULL)
    {
        return false;
    }

    high = (uint8_t) (bcd >> 4U);
    low = (uint8_t) (bcd & 0x0FU);
    if ((high > 9U) || (low > 9U))
    {
        return false;
    }

    binary = (uint8_t) ((high * 10U) + low);
    if ((binary < minimum) || (binary > maximum))
    {
        return false;
    }
    *value = binary;
    return true;
}

/*
 * 函数名：F_Hmi_DaysInMonth。
 * 说明：计算 2000～2099 年指定月份的实际天数，并处理闰年二月。
 * 输入：year 为完整年份；month 为月份。
 * 输出：返回该月天数；月份无效时返回 0。
 */
static uint8_t F_Hmi_DaysInMonth(uint16_t year, uint8_t month)
{
    const uint8_t days[12] = {31U, 28U, 31U, 30U, 31U, 30U,
                              31U, 31U, 30U, 31U, 30U, 31U};
    uint8_t result;

    if ((month == 0U) || (month > 12U))
    {
        return 0U;
    }

    result = days[month - 1U];
    if ((month == 2U) && ((year % 4U) == 0U))
    {
        result = 29U;
    }
    return result;
}

/*
 * 函数名：F_Hmi_ParseRtcFrame。
 * 说明：解析 EE F7 年月周日时分秒 RTC 响应，把 BCD 字段转换成二进制并执行日期合法性校验。
 * 输入：context 为包含完整接收帧的 HMI 功能层上下文输入输出指针。
 * 输出：无；帧合法且 RTC 事件槽空闲时锁存解析结果。
 */
static void F_Hmi_ParseRtcFrame(F_Hmi_Context *context)
{
    F_Hmi_Rtc_Time rtc_time;
    uint8_t year;

    if ((context == NULL) || context->rtc_time_pending ||
        (context->rx_length != 13U) ||
        (context->rx_frame[0] != 0xEEU) ||
        (context->rx_frame[1] != 0xF7U))
    {
        return;
    }

    if (!F_Hmi_BcdToBinary(context->rx_frame[2], 0U, 99U, &year) ||
        !F_Hmi_BcdToBinary(context->rx_frame[3], 1U, 12U, &rtc_time.month) ||
        !F_Hmi_BcdToBinary(context->rx_frame[4], 0U, 6U, &rtc_time.week) ||
        !F_Hmi_BcdToBinary(context->rx_frame[5], 1U, 31U, &rtc_time.day) ||
        !F_Hmi_BcdToBinary(context->rx_frame[6], 0U, 23U, &rtc_time.hour) ||
        !F_Hmi_BcdToBinary(context->rx_frame[7], 0U, 59U, &rtc_time.minute) ||
        !F_Hmi_BcdToBinary(context->rx_frame[8], 0U, 59U, &rtc_time.second))
    {
        return;
    }

    rtc_time.year = (uint16_t) (2000U + year);
    if (rtc_time.day > F_Hmi_DaysInMonth(rtc_time.year, rtc_time.month))
    {
        // BCD 范围合法并不代表日期合法，还需按月份和闰年检查实际天数。
        return;
    }

    context->rtc_time = rtc_time;
    context->rtc_time_pending = true;
}

/*
 * 函数名：F_Hmi_ParseFrame。
 * 说明：区分EE B1 11按钮和文本输入、EE B1 14下拉菜单选择上传帧，或解析EE F7 RTC响应帧并锁存对应事件。
 * 输入：context 为 HMI 功能层上下文输入输出指针。
 * 输出：无；格式合法且对应事件槽空闲时更新按钮或 RTC 事件字段。
 */
static void F_Hmi_ParseFrame(F_Hmi_Context *context)
{
    uint16_t text_end;
    uint16_t text_length;
    uint8_t text_queue_tail;

    if ((context->rx_length >= 14U) &&
        (context->rx_frame[0] == 0xEEU) &&
        (context->rx_frame[1] == 0xB1U) &&
        (context->rx_frame[2] == 0x11U))
    {
        if ((context->rx_frame[7] == 0x10U) && !context->button_pending)
        {
            context->button_id = (uint16_t) (((uint16_t) context->rx_frame[5] << 8U) |
                                             context->rx_frame[6]);
            context->button_value = context->rx_frame[context->rx_length - 5U];
            context->button_pending = true;
            // 0x10是按钮控件类型；单事件槽在应用层取走前不覆盖尚未处理的操作。
        }
        else if ((context->rx_frame[7] == 0x11U) &&
                 (context->text_queue_count < F_HMI_TEXT_EVENT_QUEUE_SIZE))
        {
            text_end = (uint16_t) (context->rx_length - 5U);
            text_length = (uint16_t) (text_end - 8U);
            if ((context->rx_frame[text_end] == 0U) &&
                (text_length <= F_HMI_TEXT_MAX_SIZE))
            {
                text_queue_tail = (uint8_t) ((context->text_queue_head +
                                              context->text_queue_count) %
                                             F_HMI_TEXT_EVENT_QUEUE_SIZE);
                context->text_queue[text_queue_tail].page_id =
                    (uint16_t) (((uint16_t) context->rx_frame[3] << 8U) |
                                context->rx_frame[4]);
                context->text_queue[text_queue_tail].control_id =
                    (uint16_t) (((uint16_t) context->rx_frame[5] << 8U) |
                                context->rx_frame[6]);
                (void) memcpy(context->text_queue[text_queue_tail].value,
                              &context->rx_frame[8], text_length);
                context->text_queue[text_queue_tail].value[text_length] = '\0';
                context->text_queue[text_queue_tail].length = (uint8_t) text_length;
                context->text_queue_count++;
                // 0x11是文本控件类型；只接受限定长度且以NUL结束的ASCII参数文本，并按顺序入队。
            }
        }
    }
    else if ((context->rx_length == 14U) &&
             (context->rx_frame[0] == 0xEEU) &&
             (context->rx_frame[1] == 0xB1U) &&
             (context->rx_frame[2] == 0x14U) &&
             (context->rx_frame[7] == 0x1AU) &&
             !context->button_pending)
    {
        context->button_id = (uint16_t) (((uint16_t) context->rx_frame[5] << 8U) |
                                         context->rx_frame[6]);
        context->button_value = context->rx_frame[8];
        context->button_pending = true;
        // 0x1A是下拉菜单控件类型；frame[8]为从0开始的选中项索引，frame[9]为按下或弹起状态。
        // 按下和弹起都会上传且携带相同索引，重复锁存对单项选择幂等，因此不再区分状态值。
    }
    else
    {
        F_Hmi_ParseRtcFrame(context);
    }
}

/*
 * 函数名：F_Hmi_Init。
 * 说明：初始化大彩协议解析功能和 SCI9 硬件层。
 * 输入：context 为 HMI 功能层上下文输入输出指针。
 * 输出：初始化成功时返回 true，否则返回 false。
 */
bool F_Hmi_Init(F_Hmi_Context *context)
{
    if (context == NULL)
    {
        return false;
    }

    (void) memset(context, 0, sizeof(*context));
    return H_Hmi_Init(&context->hardware);
}

/*
 * 函数名：F_Hmi_Task。
 * 说明：从 SCI9 环形缓冲区取字节并解析大彩按钮和下拉菜单控件上传帧。
 * 输入：context 为 HMI 功能层上下文输入输出指针。
 * 输出：无；解析成功后在上下文中锁存一条按钮或下拉菜单选择事件。
 */
void F_Hmi_Task(F_Hmi_Context *context)
{
    uint8_t value;

    if (context == NULL)
    {
        return;
    }

    while (H_Hmi_ReadByte(&context->hardware, &value))
    {
        if (!context->receiving)
        {
            if (value == 0xEEU)
            {
                context->receiving = true;
                context->rx_length = 1U;
                context->rx_frame[0] = value;
                // 仅以协议帧头 0xEE 开始收集，线路上的其他孤立字节直接丢弃。
            }
            continue;
        }

        if (context->rx_length >= F_HMI_FRAME_MAX_SIZE)
        {
            context->receiving = false;
            context->rx_length = 0U;
            // 缓冲区溢出说明帧已失步，立即复位解析器并等待下一个帧头重新同步。
            continue;
        }

        context->rx_frame[context->rx_length++] = value;
        if (F_Hmi_FrameComplete(context))
        {
            F_Hmi_ParseFrame(context);
            context->receiving = false;
            context->rx_length = 0U;
            // 连续结束符确认完整帧后再解析，避免半帧数据触发按钮或更新时间。
        }
    }
}

/*
 * 函数名：F_Hmi_TakeButtonEvent。
 * 说明：取出一条已经解析完成的按钮或下拉菜单选择事件。
 * 输入：context 为功能层上下文；button_id 为控件 ID 输出指针；value 为按钮状态或菜单选中项索引输出指针。
 * 输出：存在待处理事件时返回 true，否则返回 false。
 */
bool F_Hmi_TakeButtonEvent(F_Hmi_Context *context, uint16_t *button_id, uint8_t *value)
{
    if ((context == NULL) || (button_id == NULL) || (value == NULL) || !context->button_pending)
    {
        return false;
    }

    *button_id = context->button_id;
    *value = context->button_value;
    context->button_pending = false;
    return true;
}

/*
 * 函数名：F_Hmi_TakeTextEvent。
 * 说明：取出一条由大彩文本输入控件通过B1 11上传的ASCII文本事件。
 * 输入：context为功能层上下文；page_id和control_id为控件标识输出指针；text为输出缓存；capacity为容量。
 * 输出：存在事件且输出缓存足够时返回文本长度；没有事件或参数无效时返回0。
 */
size_t F_Hmi_TakeTextEvent(F_Hmi_Context *context,
                           uint16_t *page_id,
                           uint16_t *control_id,
                           char *text,
                           size_t capacity)
{
    const F_Hmi_Text_Event *event;
    size_t length;

    if ((context == NULL) || (page_id == NULL) || (control_id == NULL) ||
        (text == NULL) || (context->text_queue_count == 0U))
    {
        return 0U;
    }

    event = &context->text_queue[context->text_queue_head];
    length = event->length;
    if (capacity <= length)
    {
        return 0U;
    }

    *page_id = event->page_id;
    *control_id = event->control_id;
    (void) memcpy(text, event->value, length + 1U);
    context->text_queue_head = (uint8_t) ((context->text_queue_head + 1U) %
                                          F_HMI_TEXT_EVENT_QUEUE_SIZE);
    context->text_queue_count--;
    return length;
}

/*
 * 函数名：F_Hmi_PeekTextEvent。
 * 说明：查看文本输入FIFO队首事件的画面和控件ID，但不清除事件。
 * 输入：context为功能层上下文；page_id和control_id为控件标识输出指针。
 * 输出：存在待处理文本事件时返回true，否则返回false。
 */
bool F_Hmi_PeekTextEvent(const F_Hmi_Context *context,
                         uint16_t *page_id,
                         uint16_t *control_id)
{
    if ((context == NULL) || (page_id == NULL) || (control_id == NULL) ||
        (context->text_queue_count == 0U))
    {
        return false;
    }

    *page_id = context->text_queue[context->text_queue_head].page_id;
    *control_id = context->text_queue[context->text_queue_head].control_id;
    return true;
}

/*
 * 函数名：F_Hmi_SendReadRtc。
 * 说明：按大彩协议发送读取串口屏全局 RTC 的 0x82 指令；RTC 控件 ID 不包含在此全局命令中。
 * 输入：context 为 HMI 功能层上下文输入输出指针。
 * 输出：成功启动异步发送时返回 true，否则返回 false。
 */
bool F_Hmi_SendReadRtc(F_Hmi_Context *context)
{
    const uint8_t command[6] = {0xEEU, 0x82U, 0xFFU, 0xFCU, 0xFFU, 0xFFU};

    if ((context == NULL) || H_Hmi_IsTxBusy(&context->hardware))
    {
        return false;
    }

    (void) memcpy(context->tx_frame, command, sizeof(command));
    return H_Hmi_Write(&context->hardware, context->tx_frame, sizeof(command));
}

/*
 * 函数名：F_Hmi_TakeRtcTime。
 * 说明：取出一条已经完成 BCD 和日期合法性校验的 0xF7 RTC 响应。
 * 输入：context 为 HMI 功能层上下文；rtc_time 为 RTC 时间输出指针。
 * 输出：存在待处理的有效 RTC 时间时返回 true，否则返回 false。
 */
bool F_Hmi_TakeRtcTime(F_Hmi_Context *context, F_Hmi_Rtc_Time *rtc_time)
{
    if ((context == NULL) || (rtc_time == NULL) || !context->rtc_time_pending)
    {
        return false;
    }

    *rtc_time = context->rtc_time;
    context->rtc_time_pending = false;
    return true;
}

/*
 * 函数名：F_Hmi_SendText。
 * 说明：按大彩 B1 10 指令格式更新指定画面和控件的 ASCII 文本。
 * 输入：context 为功能层上下文；page_id 为画面 ID；control_id 为控件 ID；text 为文本；length 为文本长度。
 * 输出：成功启动异步发送时返回 true，否则返回 false。
 */
bool F_Hmi_SendText(F_Hmi_Context *context,
                    uint16_t page_id,
                    uint16_t control_id,
                    const char *text,
                    size_t length)
{
    size_t frame_length = length + 11U;

    if ((context == NULL) || (text == NULL) || (length == 0U) ||
        (frame_length > F_HMI_TX_MAX_SIZE) || H_Hmi_IsTxBusy(&context->hardware))
    {
        return false;
    }

    context->tx_frame[0] = 0xEEU;
    context->tx_frame[1] = 0xB1U;
    context->tx_frame[2] = 0x10U;
    context->tx_frame[3] = (uint8_t) (page_id >> 8U);
    context->tx_frame[4] = (uint8_t) page_id;
    context->tx_frame[5] = (uint8_t) (control_id >> 8U);
    context->tx_frame[6] = (uint8_t) control_id;
    (void) memcpy(&context->tx_frame[7], text, length);
    context->tx_frame[7U + length] = 0xFFU;
    context->tx_frame[8U + length] = 0xFCU;
    context->tx_frame[9U + length] = 0xFFU;
    context->tx_frame[10U + length] = 0xFFU;
    // 文本后追加大彩协议固定结束序列 FF FC FF FF，frame_length 已包含这 4 个字节。
    return H_Hmi_Write(&context->hardware, context->tx_frame, frame_length);
}

/*
 * 函数名：F_Hmi_SendButtonState。
 * 说明：按大彩B1 10指令强制设置指定按钮控件的弹起或按下状态。
 * 输入：context为功能层上下文；page_id和control_id指定按钮；pressed为false弹起、true按下。
 * 输出：成功启动异步发送时返回true，串口忙或参数无效时返回false。
 */
bool F_Hmi_SendButtonState(F_Hmi_Context *context,
                           uint16_t page_id,
                           uint16_t control_id,
                           bool pressed)
{
    const size_t frame_length = 12U;

    if ((context == NULL) || H_Hmi_IsTxBusy(&context->hardware))
    {
        return false;
    }

    context->tx_frame[0] = 0xEEU;
    context->tx_frame[1] = 0xB1U;
    context->tx_frame[2] = 0x10U;
    context->tx_frame[3] = (uint8_t) (page_id >> 8U);
    context->tx_frame[4] = (uint8_t) page_id;
    context->tx_frame[5] = (uint8_t) (control_id >> 8U);
    context->tx_frame[6] = (uint8_t) control_id;
    context->tx_frame[7] = pressed ? 1U : 0U;
    context->tx_frame[8] = 0xFFU;
    context->tx_frame[9] = 0xFCU;
    context->tx_frame[10] = 0xFFU;
    context->tx_frame[11] = 0xFFU;
    // 按钮和文本均使用B1 10，按钮数据固定为一字节0或1。
    return H_Hmi_Write(&context->hardware, context->tx_frame, frame_length);
}

/*
 * 函数名：F_Hmi_SendIconFrame。
 * 说明：按大彩B1 23指令设置指定图标控件当前显示帧，用于状态图层切换。
 * 输入：context为功能层上下文；page_id为画面ID；control_id为图标控件ID；frame_id为从0开始的帧索引。
 * 输出：成功启动异步发送时返回true，串口忙或参数无效时返回false。
 */
bool F_Hmi_SendIconFrame(F_Hmi_Context *context,
                         uint16_t page_id,
                         uint16_t control_id,
                         uint8_t frame_id)
{
    const size_t frame_length = 12U;

    if ((context == NULL) || H_Hmi_IsTxBusy(&context->hardware))
    {
        return false;
    }

    context->tx_frame[0] = 0xEEU;
    context->tx_frame[1] = 0xB1U;
    context->tx_frame[2] = 0x23U;
    context->tx_frame[3] = (uint8_t) (page_id >> 8U);
    context->tx_frame[4] = (uint8_t) page_id;
    context->tx_frame[5] = (uint8_t) (control_id >> 8U);
    context->tx_frame[6] = (uint8_t) control_id;
    context->tx_frame[7] = frame_id;
    context->tx_frame[8] = 0xFFU;
    context->tx_frame[9] = 0xFCU;
    context->tx_frame[10] = 0xFFU;
    context->tx_frame[11] = 0xFFU;
    // 指定帧指令只携带一个8位帧索引，高亮资源固定使用0、1、2三帧。
    return H_Hmi_Write(&context->hardware, context->tx_frame, frame_length);
}

/*
 * 函数名：F_Hmi_SendRecordClear。
 * 说明：按大彩B1 53指令清空指定数据记录控件中的现有行。
 * 输入：context为功能层上下文；page_id为画面ID；control_id为数据记录控件ID。
 * 输出：成功启动异步发送时返回true，串口忙或参数无效时返回false。
 */
bool F_Hmi_SendRecordClear(F_Hmi_Context *context,
                           uint16_t page_id,
                           uint16_t control_id)
{
    const size_t frame_length = 11U;

    if ((context == NULL) || H_Hmi_IsTxBusy(&context->hardware))
    {
        return false;
    }

    context->tx_frame[0] = 0xEEU;
    context->tx_frame[1] = 0xB1U;
    context->tx_frame[2] = 0x53U;
    context->tx_frame[3] = (uint8_t) (page_id >> 8U);
    context->tx_frame[4] = (uint8_t) page_id;
    context->tx_frame[5] = (uint8_t) (control_id >> 8U);
    context->tx_frame[6] = (uint8_t) control_id;
    context->tx_frame[7] = 0xFFU;
    context->tx_frame[8] = 0xFCU;
    context->tx_frame[9] = 0xFFU;
    context->tx_frame[10] = 0xFFU;
    return H_Hmi_Write(&context->hardware, context->tx_frame, frame_length);
}

/*
 * 函数名：F_Hmi_SendRecordAdd。
 * 说明：按大彩B1 52指令向指定通用表格添加一行以分号分隔的GBK文本。
 * 输入：context为功能层上下文；page_id和control_id指定表格；record为行文本；length为字节数。
 * 输出：成功启动异步发送时返回true，串口忙、参数无效或帧过长时返回false。
 */
bool F_Hmi_SendRecordAdd(F_Hmi_Context *context,
                         uint16_t page_id,
                         uint16_t control_id,
                         const char *record,
                         size_t length)
{
    size_t frame_length = length + 11U;

    if ((context == NULL) || (record == NULL) || (length == 0U) ||
        (frame_length > F_HMI_TX_MAX_SIZE) || H_Hmi_IsTxBusy(&context->hardware))
    {
        return false;
    }

    context->tx_frame[0] = 0xEEU;
    context->tx_frame[1] = 0xB1U;
    context->tx_frame[2] = 0x52U;
    context->tx_frame[3] = (uint8_t) (page_id >> 8U);
    context->tx_frame[4] = (uint8_t) page_id;
    context->tx_frame[5] = (uint8_t) (control_id >> 8U);
    context->tx_frame[6] = (uint8_t) control_id;
    (void) memcpy(&context->tx_frame[7], record, length);
    context->tx_frame[7U + length] = 0xFFU;
    context->tx_frame[8U + length] = 0xFCU;
    context->tx_frame[9U + length] = 0xFFU;
    context->tx_frame[10U + length] = 0xFFU;
    // 单行字段之间保留英文分号，具体列数由VisualTFT数据记录控件属性决定。
    return H_Hmi_Write(&context->hardware, context->tx_frame, frame_length);
}
