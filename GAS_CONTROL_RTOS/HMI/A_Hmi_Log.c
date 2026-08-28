#include "A_Hmi_Log.h"

#include <string.h>

/*
 * 函数名：A_HmiLog_AppendBytes。
 * 说明：向数据行缓冲区追加指定字节，并统一执行容量检查。
 * 输入：row和length为目标缓冲及当前长度；capacity为容量；data和data_length为待追加内容。
 * 输出：追加成功时返回true，参数无效或容量不足时返回false。
 */
static bool A_HmiLog_AppendBytes(char *row,
                                 size_t *length,
                                 size_t capacity,
                                 const char *data,
                                 size_t data_length)
{
    if ((row == NULL) || (length == NULL) || (data == NULL) ||
        (*length > capacity) || (data_length > (capacity - *length)))
    {
        return false;
    }

    (void) memcpy(&row[*length], data, data_length);
    *length += data_length;
    return true;
}

/*
 * 函数名：A_HmiLog_AppendChar。
 * 说明：向数据行缓冲区追加一个ASCII字符。
 * 输入：row、length和capacity描述目标缓冲；value为待追加字符。
 * 输出：追加成功时返回true，容量不足时返回false。
 */
static bool A_HmiLog_AppendChar(char *row,
                                size_t *length,
                                size_t capacity,
                                char value)
{
    return A_HmiLog_AppendBytes(row, length, capacity, &value, 1U);
}

/*
 * 函数名：A_HmiLog_AppendUnsigned。
 * 说明：把无符号整数按指定最少位数转换为十进制ASCII并追加到数据行。
 * 输入：row、length和capacity描述目标缓冲；value为数值；minimum_digits为最少位数。
 * 输出：追加成功时返回true，位数或容量不满足时返回false。
 */
static bool A_HmiLog_AppendUnsigned(char *row,
                                    size_t *length,
                                    size_t capacity,
                                    uint32_t value,
                                    uint8_t minimum_digits)
{
    char reverse[10];
    char digits[10];
    size_t count = 0U;
    size_t output_count;
    size_t index;

    do
    {
        reverse[count++] = (char) ('0' + (value % 10U));
        value /= 10U;
    } while ((value > 0U) && (count < sizeof(reverse)));

    output_count = (count < minimum_digits) ? minimum_digits : count;
    if (output_count > sizeof(digits))
    {
        return false;
    }
    for (index = 0U; index < (output_count - count); ++index)
    {
        digits[index] = '0';
    }
    for (index = 0U; index < count; ++index)
    {
        digits[output_count - count + index] = reverse[count - index - 1U];
    }
    return A_HmiLog_AppendBytes(row, length, capacity, digits, output_count);
}

/*
 * 函数名：A_HmiLog_ReadU16。
 * 说明：按日志规定的高字节在前格式读取一个16位无符号整数。
 * 输入：data为包含两个字节的只读地址。
 * 输出：返回解码后的16位数值。
 */
static uint16_t A_HmiLog_ReadU16(const uint8_t *data)
{
    return (uint16_t) (((uint16_t) data[0] << 8U) | data[1]);
}

/*
 * 函数名：A_HmiLog_DaysInMonth。
 * 说明：计算2000～2099年指定月份的实际天数，并处理闰年二月。
 * 输入：year为完整年份；month为月份。
 * 输出：返回该月天数；月份无效时返回0。
 */
static uint8_t A_HmiLog_DaysInMonth(uint16_t year, uint8_t month)
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
 * 函数名：A_HmiLog_DateTimeValid。
 * 说明：校验日志查询日期时间的年月日和时分秒范围。
 * 输入：value为待校验的日期时间。
 * 输出：六个字段组成合法时间时返回true，否则返回false。
 */
static bool A_HmiLog_DateTimeValid(const A_Hmi_Log_Date_Time *value)
{
    uint8_t maximum_day;

    if ((value == NULL) || (value->year < 2000U) || (value->year > 2099U) ||
        (value->hour > 23U) || (value->minute > 59U) || (value->second > 59U))
    {
        return false;
    }
    maximum_day = A_HmiLog_DaysInMonth(value->year, value->month);
    return ((maximum_day > 0U) && (value->day > 0U) && (value->day <= maximum_day));
}

/*
 * 函数名：A_HmiLog_DateTimeKey。
 * 说明：把日期时间组合成YYYYMMDDHHMMSS数值，用于包含边界的时间比较。
 * 输入：value为已校验的日期时间。
 * 输出：返回64位十进制时间键。
 */
static uint64_t A_HmiLog_DateTimeKey(const A_Hmi_Log_Date_Time *value)
{
    return ((uint64_t) value->year * 10000000000ULL) +
           ((uint64_t) value->month * 100000000ULL) +
           ((uint64_t) value->day * 1000000ULL) +
           ((uint64_t) value->hour * 10000ULL) +
           ((uint64_t) value->minute * 100ULL) + value->second;
}

/*
 * 函数名：A_HmiLog_RecordDateTimeKey。
 * 说明：从32字节日志公共时间区提取YYYYMMDDHHMMSS比较键。
 * 输入：record为已通过日志格式和CRC校验的记录。
 * 输出：返回记录时间键。
 */
static uint64_t A_HmiLog_RecordDateTimeKey(const uint8_t *record)
{
    A_Hmi_Log_Date_Time value;

    value.year = (uint16_t) (2000U + record[6]);
    value.month = record[7];
    value.day = record[8];
    value.hour = record[9];
    value.minute = record[10];
    value.second = record[11];
    return A_HmiLog_DateTimeKey(&value);
}

/*
 * 函数名：A_HmiLog_FilterValid。
 * 说明：校验条件字段范围，并在启用时间筛选时检查开始不晚于结束。
 * 输入：filter为待校验的查询条件。
 * 输出：条件可安全用于扫描时返回true，否则返回false。
 */
static bool A_HmiLog_FilterValid(const A_Hmi_Log_Filter *filter)
{
    if ((filter == NULL) || (filter->cylinder_number > GAS_CYLINDER_COUNT) ||
        (filter->target_state > (uint8_t) GAS_CYL_WAIT_TEST) ||
        !A_HmiLog_DateTimeValid(&filter->start) ||
        !A_HmiLog_DateTimeValid(&filter->end))
    {
        return false;
    }
    return (!filter->time_enabled ||
            (A_HmiLog_DateTimeKey(&filter->start) <= A_HmiLog_DateTimeKey(&filter->end)));
}

/*
 * 函数名：A_HmiLog_RecordMatchesFilter。
 * 说明：判断已校验日志是否同时满足类型、时间、气瓶和进入状态条件。
 * 输入：context为当前查询快照；record为32字节日志。
 * 输出：记录应加入RAM分页索引时返回true，否则返回false。
 */
static bool A_HmiLog_RecordMatchesFilter(const A_Hmi_Log_Context *context,
                                         const uint8_t *record)
{
    uint64_t record_key;
    uint8_t expected_type;

    if ((context == NULL) || (record == NULL))
    {
        return false;
    }
    expected_type = (context->query_type == A_HMI_LOG_QUERY_REGULAR) ?
                    (uint8_t) A_GAS_LOG_TYPE_REGULAR : (uint8_t) A_GAS_LOG_TYPE_EVENT;
    if (record[0] != expected_type)
    {
        return false;
    }
    if (context->active_filter.time_enabled)
    {
        record_key = A_HmiLog_RecordDateTimeKey(record);
        if ((record_key < A_HmiLog_DateTimeKey(&context->active_filter.start)) ||
            (record_key > A_HmiLog_DateTimeKey(&context->active_filter.end)))
        {
            return false;
        }
    }
    if (context->query_type == A_HMI_LOG_QUERY_REGULAR)
    {
        return true;
    }
    if ((context->active_filter.cylinder_number != 0U) &&
        (record[12] != context->active_filter.cylinder_number))
    {
        return false;
    }
    return ((context->active_filter.target_state == 0U) ||
            (record[14] == context->active_filter.target_state));
}

/*
 * 函数名：A_HmiLog_AppendPressure。
 * 说明：把乘以1000的日志压力编码转换为三位小数MPa文本，无效质量显示为“--”。
 * 输入：row、length和capacity描述目标缓冲；raw为压力原始值；valid为质量有效标志。
 * 输出：追加成功时返回true，容量不足时返回false。
 */
static bool A_HmiLog_AppendPressure(char *row,
                                    size_t *length,
                                    size_t capacity,
                                    uint16_t raw,
                                    bool valid)
{
    if (!valid)
    {
        return A_HmiLog_AppendBytes(row, length, capacity, "--", 2U);
    }

    return (A_HmiLog_AppendUnsigned(row, length, capacity, raw / 1000U, 1U) &&
            A_HmiLog_AppendChar(row, length, capacity, '.') &&
            A_HmiLog_AppendUnsigned(row, length, capacity, raw % 1000U, 3U));
}

/*
 * 函数名：A_HmiLog_AppendBriefPressure。
 * 说明：把乘以1000的日志压力四舍五入为两位小数MPa文本，使常规日志内容适合单行显示。
 * 输入：row、length和capacity描述目标缓冲；raw为压力原始值；valid为质量有效标志。
 * 输出：追加成功时返回true，容量不足时返回false。
 */
static bool A_HmiLog_AppendBriefPressure(char *row,
                                         size_t *length,
                                         size_t capacity,
                                         uint16_t raw,
                                         bool valid)
{
    uint16_t rounded;

    if (!valid)
    {
        return A_HmiLog_AppendBytes(row, length, capacity, "--", 2U);
    }

    rounded = (uint16_t) ((raw + 5U) / 10U);
    return (A_HmiLog_AppendUnsigned(row, length, capacity, rounded / 100U, 1U) &&
            A_HmiLog_AppendChar(row, length, capacity, '.') &&
            A_HmiLog_AppendUnsigned(row, length, capacity, rounded % 100U, 2U));
}

/*
 * 函数名：A_HmiLog_AppendDateTime。
 * 说明：把日志时间转换为日期时间文本，事件保留秒，半小时常规记录只显示到分钟。
 * 输入：row、length和capacity描述目标缓冲；record为32字节日志；include_seconds指定是否显示秒。
 * 输出：追加成功时返回true，容量不足时返回false。
 */
static bool A_HmiLog_AppendDateTime(char *row,
                                    size_t *length,
                                    size_t capacity,
                                    const uint8_t *record,
                                    bool include_seconds)
{
    if (!A_HmiLog_AppendUnsigned(row, length, capacity, 2000U + record[6], 4U) ||
        !A_HmiLog_AppendChar(row, length, capacity, '-') ||
        !A_HmiLog_AppendUnsigned(row, length, capacity, record[7], 2U) ||
        !A_HmiLog_AppendChar(row, length, capacity, '-') ||
        !A_HmiLog_AppendUnsigned(row, length, capacity, record[8], 2U) ||
        !A_HmiLog_AppendChar(row, length, capacity, ' ') ||
        !A_HmiLog_AppendUnsigned(row, length, capacity, record[9], 2U) ||
        !A_HmiLog_AppendChar(row, length, capacity, ':') ||
        !A_HmiLog_AppendUnsigned(row, length, capacity, record[10], 2U))
    {
        return false;
    }

    return (!include_seconds ||
            (A_HmiLog_AppendChar(row, length, capacity, ':') &&
             A_HmiLog_AppendUnsigned(row, length, capacity, record[11], 2U)));
}

/*
 * 函数名：A_HmiLog_AppendState。
 * 说明：把日志中的气瓶状态编码转换为固定GBK中文状态名称。
 * 输入：row、length和capacity描述目标缓冲；state为1～7状态编码。
 * 输出：状态文字追加成功时返回true，否则返回false。
 */
static bool A_HmiLog_AppendState(char *row,
                                 size_t *length,
                                 size_t capacity,
                                 uint8_t state)
{
    const char *text;
    size_t text_length;

    switch ((gas_cylinder_state_t) state)
    {
        case GAS_CYL_INIT:
            text = "\xB3\xF5\xCA\xBC\xBB\xAF"; // 初始化。
            text_length = 6U;
            break;

        case GAS_CYL_READY:
            text = "\xB4\xFD\xD3\xC3"; // 待用。
            text_length = 4U;
            break;

        case GAS_CYL_ACTIVE:
            text = "\xCA\xB9\xD3\xC3"; // 使用。
            text_length = 4U;
            break;

        case GAS_CYL_LOW_REPLACE:
            text = "\xB5\xCD\xD1\xB9\xB4\xFD\xBB\xBB"; // 低压待换。
            text_length = 8U;
            break;

        case GAS_CYL_LOW_WARNING:
            text = "\xB5\xCD\xD1\xB9\xBE\xAF\xB8\xE6"; // 低压警告。
            text_length = 8U;
            break;

        case GAS_CYL_DISABLED:
            text = "\xCD\xA3\xD3\xC3"; // 停用。
            text_length = 4U;
            break;

        case GAS_CYL_WAIT_TEST:
            text = "\xB4\xFD\xB2\xE2\xCA\xD4"; // 待测试。
            text_length = 6U;
            break;

        default:
            text = "\xCE\xB4\xD6\xAA"; // 未知。
            text_length = 4U;
            break;
    }
    return A_HmiLog_AppendBytes(row, length, capacity, text, text_length);
}

/*
 * 函数名：A_HmiLog_GetPageId。
 * 说明：根据查询类型选择事件或常规日志画面ID。
 * 输入：query_type为当前日志查询类型。
 * 输出：返回目标画面ID。
 */
static uint16_t A_HmiLog_GetPageId(A_Hmi_Log_Query_Type query_type)
{
    return (query_type == A_HMI_LOG_QUERY_REGULAR) ?
           A_HMI_REGULAR_LOG_PAGE_ID : A_HMI_EVENT_LOG_PAGE_ID;
}

/*
 * 函数名：A_HmiLog_GetRecordControlId。
 * 说明：根据查询类型选择事件或常规数据记录控件ID。
 * 输入：query_type为当前日志查询类型。
 * 输出：返回目标数据记录控件ID。
 */
static uint16_t A_HmiLog_GetRecordControlId(A_Hmi_Log_Query_Type query_type)
{
    return (query_type == A_HMI_LOG_QUERY_REGULAR) ?
           A_HMI_REGULAR_LOG_CONTENT_CONTROL_ID : A_HMI_EVENT_LOG_RECORD_CONTROL_ID;
}

/*
 * 函数名：A_HmiLog_GetStatusControlId。
 * 说明：根据查询类型选择事件或常规查询状态文本控件ID。
 * 输入：query_type为当前日志查询类型。
 * 输出：返回目标状态文本控件ID。
 */
static uint16_t A_HmiLog_GetStatusControlId(A_Hmi_Log_Query_Type query_type)
{
    return (query_type == A_HMI_LOG_QUERY_REGULAR) ?
           A_HMI_REGULAR_LOG_STATUS_CONTROL_ID : A_HMI_EVENT_LOG_STATUS_CONTROL_ID;
}

/*
 * 函数名：A_HmiLog_GetPageInfoControlId。
 * 说明：根据查询类型选择事件或常规日志页码文本控件ID。
 * 输入：query_type为当前日志查询类型。
 * 输出：返回目标页码文本控件ID。
 */
static uint16_t A_HmiLog_GetPageInfoControlId(A_Hmi_Log_Query_Type query_type)
{
    return (query_type == A_HMI_LOG_QUERY_REGULAR) ?
           A_HMI_REGULAR_LOG_PAGE_INFO_CONTROL_ID : A_HMI_EVENT_LOG_PAGE_INFO_CONTROL_ID;
}

/*
 * 函数名：A_HmiLog_GetPageSize。
 * 说明：根据查询类型取得每页能够显示的逻辑日志数量。
 * 输入：query_type为当前日志查询类型。
 * 输出：常规日志返回10，事件日志返回15。
 */
static uint16_t A_HmiLog_GetPageSize(A_Hmi_Log_Query_Type query_type)
{
    return (query_type == A_HMI_LOG_QUERY_REGULAR) ?
           A_HMI_REGULAR_LOG_VISIBLE_COUNT : A_HMI_EVENT_LOG_VISIBLE_COUNT;
}

/*
 * 函数名：A_HmiLog_GetPageCount。
 * 说明：根据已经建立的单类型索引数量计算当前已知的总页数。
 * 输入：context为只读日志查询上下文。
 * 输出：没有匹配日志时返回0，否则返回向上取整后的页数。
 */
static uint16_t A_HmiLog_GetPageCount(const A_Hmi_Log_Context *context)
{
    uint16_t page_size;

    if ((context == NULL) || (context->matched_count == 0U))
    {
        return 0U;
    }
    page_size = A_HmiLog_GetPageSize(context->query_type);
    return (uint16_t) ((context->matched_count + page_size - 1U) / page_size);
}

/*
 * 函数名：A_HmiLog_GetRecordType。
 * 说明：把串口屏查询类型转换为EEPROM日志记录类型编码。
 * 输入：query_type为当前日志查询类型。
 * 输出：返回常规或事件日志类型编码。
 */
static uint8_t A_HmiLog_GetRecordType(A_Hmi_Log_Query_Type query_type)
{
    return (query_type == A_HMI_LOG_QUERY_REGULAR) ?
           (uint8_t) A_GAS_LOG_TYPE_REGULAR : (uint8_t) A_GAS_LOG_TYPE_EVENT;
}

/*
 * 函数名：A_HmiLog_SetDefaultFilter。
 * 说明：建立“全部时间、全部气瓶、全部进入状态”的初始查询条件。
 * 输入：filter为查询条件输出指针。
 * 输出：无；时间输入框边界设为20000101 000000至20991231 235959。
 */
static void A_HmiLog_SetDefaultFilter(A_Hmi_Log_Filter *filter)
{
    if (filter == NULL)
    {
        return;
    }
    (void) memset(filter, 0, sizeof(*filter));
    filter->start.year = 2000U;
    filter->start.month = 1U;
    filter->start.day = 1U;
    filter->end.year = 2099U;
    filter->end.month = 12U;
    filter->end.day = 31U;
    filter->end.hour = 23U;
    filter->end.minute = 59U;
    filter->end.second = 59U;
    filter->time_enabled = false;
}

/*
 * 函数名：A_HmiLog_ParseDigits。
 * 说明：把固定位数的ASCII数字转换为无符号整数。
 * 输入：text为文本；length为必须完全消费的字节数；value为输出指针。
 * 输出：所有字节均为0～9时返回true，否则返回false。
 */
static bool A_HmiLog_ParseDigits(const char *text, size_t length, uint32_t *value)
{
    size_t index;
    uint32_t result = 0U;

    if ((text == NULL) || (value == NULL) || (length == 0U))
    {
        return false;
    }
    for (index = 0U; index < length; ++index)
    {
        if ((text[index] < '0') || (text[index] > '9'))
        {
            return false;
        }
        result = (result * 10U) + (uint32_t) (text[index] - '0');
    }
    *value = result;
    return true;
}

/*
 * 函数名：A_HmiLog_FormatFilterField。
 * 说明：把查询页的日期、时间、气瓶、状态或提示格式化为ASCII/GBK文本。
 * 输入：context为日志上下文；slot为0～7刷新槽；text和capacity为输出缓冲。
 * 输出：返回有效文本字节数，参数或容量异常时返回0。
 */
static size_t A_HmiLog_FormatFilterField(const A_Hmi_Log_Context *context,
                                         uint8_t slot,
                                         char *text,
                                         size_t capacity)
{
    const char all_text[] = "\xC8\xAB\xB2\xBF"; // 全部。
    const char number_text[] = "\xBA\xC5"; // 号。
    const char ready_text[] = "\xCC\xF5\xBC\xFE\xD2\xD1\xBE\xCD\xD0\xF7"; // 条件已就绪。
    const char input_error_text[] = "\xC8\xD5\xC6\xDA\xBB\xF2\xCA\xB1\xBC\xE4\xB8\xF1\xCA\xBD\xB4\xED\xCE\xF3"; // 日期或时间格式错误。
    const char reset_text[] = "\xD2\xD1\xBB\xD6\xB8\xB4\xC8\xAB\xB2\xBF\xBC\xC7\xC2\xBC"; // 已恢复全部记录。
    const A_Hmi_Log_Date_Time *date_time;
    size_t length = 0U;

    if ((context == NULL) || (text == NULL))
    {
        return 0U;
    }
    if (slot <= 3U)
    {
        date_time = (slot < 2U) ? &context->edit_filter.start : &context->edit_filter.end;
        if ((slot == 0U) || (slot == 2U))
        {
            return (A_HmiLog_AppendUnsigned(text, &length, capacity, date_time->year, 4U) &&
                    A_HmiLog_AppendUnsigned(text, &length, capacity, date_time->month, 2U) &&
                    A_HmiLog_AppendUnsigned(text, &length, capacity, date_time->day, 2U)) ? length : 0U;
        }
        return (A_HmiLog_AppendUnsigned(text, &length, capacity, date_time->hour, 2U) &&
                A_HmiLog_AppendUnsigned(text, &length, capacity, date_time->minute, 2U) &&
                A_HmiLog_AppendUnsigned(text, &length, capacity, date_time->second, 2U)) ? length : 0U;
    }
    if (slot == 5U)
    {
        if (context->edit_filter.cylinder_number == 0U)
        {
            return A_HmiLog_AppendBytes(text, &length, capacity,
                                       all_text, sizeof(all_text) - 1U) ? length : 0U;
        }
        return (A_HmiLog_AppendUnsigned(text, &length, capacity,
                                       context->edit_filter.cylinder_number, 1U) &&
                A_HmiLog_AppendBytes(text, &length, capacity,
                                    number_text, sizeof(number_text) - 1U)) ? length : 0U;
    }
    if (slot == 6U)
    {
        if (context->edit_filter.target_state == 0U)
        {
            return A_HmiLog_AppendBytes(text, &length, capacity,
                                       all_text, sizeof(all_text) - 1U) ? length : 0U;
        }
        return A_HmiLog_AppendState(text, &length, capacity,
                                   context->edit_filter.target_state) ? length : 0U;
    }
    if (slot == 7U)
    {
        const char *status_text = ready_text;
        size_t status_length = sizeof(ready_text) - 1U;

        if (context->filter_status == A_HMI_LOG_FILTER_STATUS_INPUT_ERROR)
        {
            status_text = input_error_text;
            status_length = sizeof(input_error_text) - 1U;
        }
        else if (context->filter_status == A_HMI_LOG_FILTER_STATUS_RESET)
        {
            status_text = reset_text;
            status_length = sizeof(reset_text) - 1U;
        }
        return A_HmiLog_AppendBytes(text, &length, capacity,
                                   status_text, status_length) ? length : 0U;
    }
    return 0U;
}

/*
 * 函数名：A_HmiLog_FilterRefreshTask。
 * 说明：每次最多回写一个Screen6动态控件，避免长帧阻塞SCI9。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：成功启动一帧发送时返回true，否则返回false。
 */
static bool A_HmiLog_FilterRefreshTask(A_Hmi_Log_Context *context)
{
    const uint16_t control_id[8] = {
        A_HMI_LOG_FILTER_START_DATE_ID, A_HMI_LOG_FILTER_START_TIME_ID,
        A_HMI_LOG_FILTER_END_DATE_ID, A_HMI_LOG_FILTER_END_TIME_ID,
        A_HMI_LOG_FILTER_ALL_TIME_BUTTON_ID, A_HMI_LOG_FILTER_CYLINDER_TEXT_ID,
        A_HMI_LOG_FILTER_STATE_TEXT_ID, A_HMI_LOG_FILTER_STATUS_TEXT_ID
    };
    char text[A_HMI_LOG_STATUS_MAX_SIZE];
    size_t length;
    uint8_t slot;
    bool sent;

    if ((context == NULL) || (context->filter_refresh_mask == 0U))
    {
        return false;
    }
    for (slot = 0U; slot < 8U; ++slot)
    {
        if ((context->filter_refresh_mask & (uint16_t) (1U << slot)) == 0U)
        {
            continue;
        }
        if (slot == 4U)
        {
            sent = F_Hmi_SendButtonState(&context->hmi->function,
                                         A_HMI_LOG_FILTER_PAGE_ID,
                                         control_id[slot],
                                         !context->edit_filter.time_enabled);
        }
        else
        {
            length = A_HmiLog_FormatFilterField(context, slot, text, sizeof(text));
            sent = ((length > 0U) &&
                    F_Hmi_SendText(&context->hmi->function,
                                   A_HMI_LOG_FILTER_PAGE_ID,
                                   control_id[slot], text, length));
        }
        if (sent)
        {
            context->filter_refresh_mask &= (uint16_t) ~(uint16_t) (1U << slot);
        }
        return sent;
    }
    return false;
}

/*
 * 函数名：A_HmiLog_FormatRegularPressureRow。
 * 说明：把常规记录格式化为“时间；压力内容”两列表格行。
 * 输入：record为已校验的常规日志；row为输出缓冲；capacity为容量。
 * 输出：返回生成的有效字节数，格式或容量异常时返回0。
 */
static size_t A_HmiLog_FormatRegularPressureRow(const uint8_t *record,
                                                char *row,
                                                size_t capacity)
{
    const char pressure_text[4] = {'\xD1', '\xB9', '\xC1', '\xA6'}; // 压力。
    const char total_pressure_text[4] = {'\xD7', '\xDC', '\xD1', '\xB9'}; // 总压。
    uint8_t index;
    uint8_t quality_mask = record[29];
    size_t length = 0U;

    if (!A_HmiLog_AppendUnsigned(row, &length, capacity, record[7], 2U) ||
        !A_HmiLog_AppendChar(row, &length, capacity, '-') ||
        !A_HmiLog_AppendUnsigned(row, &length, capacity, record[8], 2U) ||
        !A_HmiLog_AppendChar(row, &length, capacity, ' ') ||
        !A_HmiLog_AppendUnsigned(row, &length, capacity, record[9], 2U) ||
        !A_HmiLog_AppendChar(row, &length, capacity, ':') ||
        !A_HmiLog_AppendUnsigned(row, &length, capacity, record[10], 2U) ||
        !A_HmiLog_AppendChar(row, &length, capacity, ';') ||
        !A_HmiLog_AppendBytes(row, &length, capacity, pressure_text, sizeof(pressure_text)) ||
        !A_HmiLog_AppendChar(row, &length, capacity, ':'))
    {
        return 0U;
    }

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        if (!A_HmiLog_AppendUnsigned(row, &length, capacity, (uint32_t) index + 1U, 1U) ||
            !A_HmiLog_AppendChar(row, &length, capacity, '#') ||
            !A_HmiLog_AppendBriefPressure(row,
                                          &length,
                                          capacity,
                                          A_HmiLog_ReadU16(&record[12U + ((uint16_t) index * 2U)]),
                                          (quality_mask & (uint8_t) (1U << index)) != 0U) ||
            !A_HmiLog_AppendChar(row, &length, capacity, ' '))
        {
            return 0U;
        }
    }

    if (!A_HmiLog_AppendBytes(row, &length, capacity, total_pressure_text,
                              sizeof(total_pressure_text)) ||
        !A_HmiLog_AppendChar(row, &length, capacity, ':') ||
        !A_HmiLog_AppendBriefPressure(row,
                                      &length,
                                      capacity,
                                      A_HmiLog_ReadU16(&record[24]),
                                      (quality_mask & 0x40U) != 0U) ||
        !A_HmiLog_AppendBytes(row, &length, capacity, " MPa;", 5U))
    {
        return 0U;
    }
    return length;
}

/*
 * 函数名：A_HmiLog_FormatRegularStateRow。
 * 说明：把常规记录格式化为“空时间；状态内容”两列表格行。
 * 输入：record为已校验的常规日志；row为输出缓冲；capacity为容量。
 * 输出：返回生成的有效字节数，格式或容量异常时返回0。
 */
static size_t A_HmiLog_FormatRegularStateRow(const uint8_t *record,
                                             char *row,
                                             size_t capacity)
{
    const char state_text[4] = {'\xD7', '\xB4', '\xCC', '\xAC'}; // 状态。
    uint32_t packed_states;
    uint8_t index;
    size_t length = 0U;

    if (!A_HmiLog_AppendChar(row, &length, capacity, ';') ||
        !A_HmiLog_AppendBytes(row, &length, capacity, state_text, sizeof(state_text)) ||
        !A_HmiLog_AppendChar(row, &length, capacity, ':'))
    {
        return 0U;
    }

    packed_states = ((uint32_t) record[26] << 16U) |
                    ((uint32_t) record[27] << 8U) |
                    record[28];
    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        uint8_t state = (uint8_t) ((packed_states >> (index * 3U)) & 0x07UL);

        if (!A_HmiLog_AppendUnsigned(row, &length, capacity, (uint32_t) index + 1U, 1U) ||
            !A_HmiLog_AppendChar(row, &length, capacity, '#') ||
            !A_HmiLog_AppendState(row, &length, capacity, state) ||
            ((index < (GAS_CYLINDER_COUNT - 1U)) &&
             !A_HmiLog_AppendChar(row, &length, capacity, ' ')))
        {
            return 0U;
        }
    }
    return A_HmiLog_AppendChar(row, &length, capacity, ';') ? length : 0U;
}

/*
 * 函数名：A_HmiLog_FormatEventRow。
 * 说明：把事件记录格式化为“时间、气瓶、状态变化、压力”四列表格行。
 * 输入：record为已校验的事件日志；row为输出缓冲；capacity为容量。
 * 输出：返回生成的有效字节数，格式或容量异常时返回0。
 */
static size_t A_HmiLog_FormatEventRow(const uint8_t *record,
                                      char *row,
                                      size_t capacity)
{
    const char number_text[2] = {'\xBA', '\xC5'}; // 号。
    size_t length = 0U;

    if (!A_HmiLog_AppendDateTime(row, &length, capacity, record, true) ||
        !A_HmiLog_AppendChar(row, &length, capacity, ';') ||
        !A_HmiLog_AppendUnsigned(row, &length, capacity, record[12], 1U) ||
        !A_HmiLog_AppendBytes(row, &length, capacity, number_text, sizeof(number_text)) ||
        !A_HmiLog_AppendChar(row, &length, capacity, ';') ||
        !A_HmiLog_AppendState(row, &length, capacity, record[13]) ||
        !A_HmiLog_AppendBytes(row, &length, capacity, "->", 2U) ||
        !A_HmiLog_AppendState(row, &length, capacity, record[14]) ||
        !A_HmiLog_AppendChar(row, &length, capacity, ';') ||
        !A_HmiLog_AppendPressure(row,
                                 &length,
                                 capacity,
                                 A_HmiLog_ReadU16(&record[16]),
                                 record[18] == (uint8_t) GAS_PRESSURE_VALID) ||
        !A_HmiLog_AppendBytes(row, &length, capacity, " MPa;", 5U))
    {
        return 0U;
    }
    return length;
}

/*
 * 函数名：A_HmiLog_StartRequest。
 * 说明：保存日志流水号快照，清空旧索引并启动指定类型日志的第一页优先分页查询。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：无；查询状态切换到清表步骤。
 */
static void A_HmiLog_StartRequest(A_Hmi_Log_Context *context)
{
    context->request_pending = false;
    context->query_type = context->pending_type;
    context->row_ready = false;
    context->current_record_ready = false;
    context->row_length = 0U;
    context->sent_count = 0U;
    context->regular_line_phase = 0U;
    context->matched_count = 0U;
    context->scanned_count = 0U;
    context->read_failed = !A_GasLog_IsReady(context->log);
    context->filter_error = !A_HmiLog_FilterValid(&context->active_filter);
    context->snapshot_changed = false;
    context->progress_pending = false;
    context->index_complete = false;
    context->cache_valid = !context->read_failed && !context->filter_error;
    context->page_rendered = false;
    context->table_is_clear = false;
    context->page_request_pending = true;
    context->current_page = 0U;
    context->requested_page = 0U;
    context->page_start_index = 0U;
    context->page_end_index = 0U;
    context->page_cursor = 0U;
    context->total_count = context->filter_error ? 0U : A_GasLog_GetCount(context->log);
    context->scan_logical_index = context->total_count;
    context->snapshot_next_sequence = context->log->next_sequence;
    context->state = A_HMI_LOG_QUERY_CLEAR;
    // 第1页索引满足后立即显示，同时保留后台扫描进度以继续建立完整分页索引。
}

/*
 * 函数名：A_HmiLog_FailAndClear。
 * 说明：标记本次查询失败并回到清表步骤，防止屏幕保留不完整的部分结果。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：无。
 */
static void A_HmiLog_FailAndClear(A_Hmi_Log_Context *context)
{
    context->read_failed = true;
    context->cache_valid = false;
    context->index_complete = true;
    context->row_ready = false;
    context->current_record_ready = false;
    context->regular_line_phase = 0U;
    context->progress_pending = false;
    context->page_request_pending = false;
    context->table_is_clear = false;
    context->state = A_HMI_LOG_QUERY_CLEAR;
}

/*
 * 函数名：A_HmiLog_ScanOneRecord。
 * 说明：从快照最新端向旧端读取一条日志，把匹配记录的逻辑位置加入RAM分页索引。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：本条读取及校验成功时返回true，否则返回false。
 */
static bool A_HmiLog_ScanOneRecord(A_Hmi_Log_Context *context)
{
    uint16_t logical_index;

    if (context->scan_logical_index == 0U)
    {
        return false;
    }
    logical_index = (uint16_t) (context->scan_logical_index - 1U);
    if (!A_GasLog_ReadRecord(context->log, logical_index, context->current_record))
    {
        return false;
    }
    context->scan_logical_index = logical_index;
    context->scanned_count++;
    if (A_HmiLog_RecordMatchesFilter(context, context->current_record))
    {
        if (context->matched_count >= A_HMI_LOG_INDEX_CAPACITY)
        {
            return false;
        }
        context->log_index[context->matched_count] = logical_index;
        context->matched_count++;
        // 索引按扫描顺序从最新到最旧保存，翻页时无需再次遍历前面的原始日志。
    }
    if (((context->scanned_count % A_HMI_LOG_PROGRESS_INTERVAL) == 0U) &&
        (context->scan_logical_index > 0U))
    {
        context->progress_pending = true;
    }
    return true;
}

/*
 * 函数名：A_HmiLog_FormatCurrentRow。
 * 说明：格式化当前事件行，或按压力行、状态行的顺序格式化常规记录。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：生成有效行时返回true，格式或容量异常时返回false。
 */
static bool A_HmiLog_FormatCurrentRow(A_Hmi_Log_Context *context)
{
    if (context->query_type == A_HMI_LOG_QUERY_EVENT)
    {
        context->row_length = A_HmiLog_FormatEventRow(context->current_record,
                                                       context->row,
                                                       sizeof(context->row));
    }
    else if (context->regular_line_phase == 0U)
    {
        context->row_length = A_HmiLog_FormatRegularPressureRow(context->current_record,
                                                                 context->row,
                                                                 sizeof(context->row));
        // 表格改为新记录追加到底部，因此先发带时间的压力行，再发空时间的状态行。
    }
    else
    {
        context->row_length = A_HmiLog_FormatRegularStateRow(context->current_record,
                                                              context->row,
                                                              sizeof(context->row));
    }
    context->row_ready = context->row_length > 0U;
    return context->row_ready;
}

/*
 * 函数名：A_HmiLog_IsRequestedPageAvailable。
 * 说明：判断当前RAM索引是否已经足够显示请求页；最后一页允许在索引完成后不足整页。
 * 输入：context为只读日志查询上下文。
 * 输出：请求页现在能够完整显示时返回true，否则返回false。
 */
static bool A_HmiLog_IsRequestedPageAvailable(const A_Hmi_Log_Context *context)
{
    uint32_t required_count;
    uint16_t page_count;
    uint16_t page_size;

    if ((context == NULL) || (context->matched_count == 0U))
    {
        return false;
    }
    page_size = A_HmiLog_GetPageSize(context->query_type);
    required_count = ((uint32_t) context->requested_page + 1UL) * page_size;
    if (!context->index_complete)
    {
        return ((uint32_t) context->matched_count >= required_count);
    }
    page_count = A_HmiLog_GetPageCount(context);
    return (context->requested_page < page_count);
}

/*
 * 函数名：A_HmiLog_StartPage。
 * 说明：根据已建立的RAM索引准备请求页的起止位置，并进入清表或读记录步骤。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：无；页面游标和状态在context中更新。
 */
static void A_HmiLog_StartPage(A_Hmi_Log_Context *context)
{
    uint32_t end_index;
    uint16_t page_size = A_HmiLog_GetPageSize(context->query_type);

    context->current_page = context->requested_page;
    context->page_start_index = (uint16_t) ((uint32_t) context->current_page * page_size);
    end_index = (uint32_t) context->page_start_index + page_size;
    context->page_end_index = (end_index > context->matched_count) ?
                              context->matched_count : (uint16_t) end_index;
    context->page_cursor = context->page_start_index;
    context->sent_count = 0U;
    context->regular_line_phase = 0U;
    context->current_record_ready = false;
    context->row_ready = false;
    context->row_length = 0U;
    context->page_request_pending = false;
    context->state = context->table_is_clear ?
                     A_HMI_LOG_QUERY_PAGE_READ : A_HMI_LOG_QUERY_PAGE_CLEAR;
}

/*
 * 函数名：A_HmiLog_ProcessPageRequest。
 * 说明：在索引数量允许时启动请求页；索引未完成时继续后台扫描，越界页在完成后钳位到最后一页。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：无；查询状态切换到页面读取、索引扫描或页码提示。
 */
static void A_HmiLog_ProcessPageRequest(A_Hmi_Log_Context *context)
{
    uint16_t page_count;

    if (!context->page_request_pending)
    {
        return;
    }
    if (!context->index_complete && !A_HmiLog_IsRequestedPageAvailable(context))
    {
        context->state = A_HMI_LOG_QUERY_SCAN;
        return;
    }

    page_count = A_HmiLog_GetPageCount(context);
    if (page_count == 0U)
    {
        context->page_request_pending = false;
        context->current_page = 0U;
        context->state = A_HMI_LOG_QUERY_PAGE_INFO;
        return;
    }
    if (context->requested_page >= page_count)
    {
        context->requested_page = (uint16_t) (page_count - 1U);
    }
    if (context->page_rendered && (context->requested_page == context->current_page))
    {
        context->page_request_pending = false;
        context->state = A_HMI_LOG_QUERY_PAGE_INFO;
        return;
    }
    A_HmiLog_StartPage(context);
}

/*
 * 函数名：A_HmiLog_FinishIndex。
 * 说明：完成当前类型索引统计，并显示等待中的目标页或更新现有页面的准确页数。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：无；索引标记和后续分页状态在context中更新。
 */
static void A_HmiLog_FinishIndex(A_Hmi_Log_Context *context)
{
    context->index_complete = true;
    context->progress_pending = false;
    if (!context->page_rendered && !context->page_request_pending)
    {
        context->requested_page = 0U;
        context->page_request_pending = true;
    }
    if (context->page_request_pending)
    {
        A_HmiLog_ProcessPageRequest(context);
    }
    else
    {
        context->state = A_HMI_LOG_QUERY_PAGE_INFO;
    }
}

/*
 * 函数名：A_HmiLog_SendProgress。
 * 说明：向当前日志画面发送“扫描 已处理/快照总数”进度文本。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：成功启动发送时返回true，否则返回false并在后续周期重试。
 */
static bool A_HmiLog_SendProgress(A_Hmi_Log_Context *context)
{
    const char scan_text[] = "\xC9\xA8\xC3\xE8\x20"; // 扫描。
    char status[A_HMI_LOG_STATUS_MAX_SIZE];
    size_t length = 0U;

    if (!A_HmiLog_AppendBytes(status, &length, sizeof(status),
                              scan_text, sizeof(scan_text) - 1U) ||
        !A_HmiLog_AppendUnsigned(status, &length, sizeof(status),
                                context->scanned_count, 1U) ||
        !A_HmiLog_AppendChar(status, &length, sizeof(status), '/') ||
        !A_HmiLog_AppendUnsigned(status, &length, sizeof(status),
                                context->total_count, 1U))
    {
        return false;
    }

    return F_Hmi_SendText(&context->hmi->function,
                          A_HmiLog_GetPageId(context->query_type),
                          A_HmiLog_GetStatusControlId(context->query_type),
                          status,
                          length);
}

/*
 * 函数名：A_HmiLog_SendStatus。
 * 说明：向当前日志画面发送完成条数、快照变化、RTC无效或读取错误提示。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：成功启动发送时返回true，否则返回false并在后续周期重试。
 */
static bool A_HmiLog_SendStatus(A_Hmi_Log_Context *context)
{
    const char event_text[] = "\xCA\xC2\xBC\xFE\x20"; // 事件。
    const char regular_text[] = "\xB3\xA3\xB9\xE6\x20"; // 常规。
    const char count_text[] = "\xCC\xF5"; // 条。
    const char changed_text[] = "\xC8\xD5\xD6\xBE\xD2\xD1\xB8\xFC\xD0\xC2"; // 日志已更新。
    const char error_text[] = "\xB6\xC1\xC8\xA1\xB4\xED\xCE\xF3"; // 读取错误。
    const char rtc_text[] = "\xCA\xB1\xBC\xE4\xCE\xDE\xD0\xA7"; // 时间无效。
    const char range_text[] = "\xCA\xB1\xBC\xE4\xB7\xB6\xCE\xA7\xB4\xED\xCE\xF3"; // 时间范围错误。
    char status[A_HMI_LOG_STATUS_MAX_SIZE];
    const char *fixed_text = NULL;
    size_t fixed_length = 0U;
    size_t length = 0U;

    if (context->filter_error)
    {
        fixed_text = range_text;
        fixed_length = sizeof(range_text) - 1U;
    }
    else if (context->read_failed)
    {
        fixed_text = error_text;
        fixed_length = sizeof(error_text) - 1U;
    }
    else if (context->snapshot_changed)
    {
        fixed_text = changed_text;
        fixed_length = sizeof(changed_text) - 1U;
    }
    else if ((context->system == NULL) || !context->system->date_time.valid)
    {
        fixed_text = rtc_text;
        fixed_length = sizeof(rtc_text) - 1U;
    }

    if (fixed_text != NULL)
    {
        if (!A_HmiLog_AppendBytes(status, &length, sizeof(status), fixed_text, fixed_length))
        {
            return false;
        }
    }
    else
    {
        const char *type_text = (context->query_type == A_HMI_LOG_QUERY_REGULAR) ?
                                regular_text : event_text;
        size_t type_length = (context->query_type == A_HMI_LOG_QUERY_REGULAR) ?
                             (sizeof(regular_text) - 1U) : (sizeof(event_text) - 1U);

        if (!A_HmiLog_AppendBytes(status, &length, sizeof(status), type_text, type_length) ||
            !A_HmiLog_AppendUnsigned(status, &length, sizeof(status), context->matched_count, 1U) ||
            !A_HmiLog_AppendBytes(status, &length, sizeof(status),
                                  count_text, sizeof(count_text) - 1U))
        {
            return false;
        }
    }

    return F_Hmi_SendText(&context->hmi->function,
                          A_HmiLog_GetPageId(context->query_type),
                          A_HmiLog_GetStatusControlId(context->query_type),
                          status,
                          length);
}

/*
 * 函数名：A_HmiLog_SendPageInfo。
 * 说明：向当前日志画面发送页码、总页数、已匹配条数或后台统计状态。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：成功启动发送时返回true，否则返回false并在后续周期重试。
 */
static bool A_HmiLog_SendPageInfo(A_Hmi_Log_Context *context)
{
    const char first_text[] = "\xB5\xDA"; // 第。
    const char page_text[] = "\xD2\xB3"; // 页。
    const char total_text[] = "\xB9\xB2"; // 共。
    const char count_text[] = "\xCC\xF5"; // 条。
    const char counting_text[] = "\xCD\xB3\xBC\xC6\xD6\xD0"; // 统计中。
    char status[A_HMI_LOG_STATUS_MAX_SIZE];
    uint16_t page_count = A_HmiLog_GetPageCount(context);
    uint16_t display_page = (context->matched_count == 0U) ?
                            0U : (uint16_t) (context->current_page + 1U);
    size_t length = 0U;

    if (!A_HmiLog_AppendBytes(status, &length, sizeof(status),
                              first_text, sizeof(first_text) - 1U) ||
        !A_HmiLog_AppendUnsigned(status, &length, sizeof(status), display_page, 1U))
    {
        return false;
    }
    if (context->index_complete)
    {
        if (!A_HmiLog_AppendChar(status, &length, sizeof(status), '/') ||
            !A_HmiLog_AppendUnsigned(status, &length, sizeof(status), page_count, 1U))
        {
            return false;
        }
    }
    if (!A_HmiLog_AppendBytes(status, &length, sizeof(status),
                              page_text, sizeof(page_text) - 1U) ||
        !A_HmiLog_AppendChar(status, &length, sizeof(status), ' '))
    {
        return false;
    }
    if (!context->index_complete)
    {
        if (!A_HmiLog_AppendBytes(status, &length, sizeof(status),
                                  counting_text, sizeof(counting_text) - 1U))
        {
            return false;
        }
    }
    else if (!A_HmiLog_AppendBytes(status, &length, sizeof(status),
                                   total_text, sizeof(total_text) - 1U) ||
             !A_HmiLog_AppendUnsigned(status, &length, sizeof(status),
                                     context->matched_count, 1U) ||
             !A_HmiLog_AppendBytes(status, &length, sizeof(status),
                                  count_text, sizeof(count_text) - 1U))
    {
        return false;
    }

    return F_Hmi_SendText(&context->hmi->function,
                          A_HmiLog_GetPageId(context->query_type),
                          A_HmiLog_GetPageInfoControlId(context->query_type),
                          status,
                          length);
}

/*
 * 函数名：A_HmiLog_Init。
 * 说明：初始化串口屏分页日志实例，并关联HMI、EEPROM日志和气源系统状态。
 * 输入：context为日志查询上下文；hmi为HMI应用实例；log为EEPROM日志实例；system为气源系统只读实例。
 * 输出：参数有效时返回true，否则返回false。
 */
bool A_HmiLog_Init(A_Hmi_Log_Context *context,
                   A_Hmi_Context *hmi,
                   A_Gas_Log_Context *log,
                   const Gas_System *system)
{
    if ((context == NULL) || (hmi == NULL) || (log == NULL) || (system == NULL))
    {
        return false;
    }

    (void) memset(context, 0, sizeof(*context));
    context->hmi = hmi;
    context->log = log;
    context->system = system;
    context->query_type = A_HMI_LOG_QUERY_EVENT;
    context->pending_type = A_HMI_LOG_QUERY_EVENT;
    A_HmiLog_SetDefaultFilter(&context->edit_filter);
    context->active_filter = context->edit_filter;
    context->filter_status = A_HMI_LOG_FILTER_STATUS_READY;
    context->ready = true;
    return true;
}

/*
 * 函数名：A_HmiLog_HandleFilterButton。
 * 说明：处理日志类型页签、全部时间、气瓶、进入状态、查询和重置按钮。
 * 输入：context为日志查询上下文；button_id和value为按钮控件ID和上传值。
 * 输出：按钮属于日志条件模块时返回true，否则返回false。
 */
bool A_HmiLog_HandleFilterButton(A_Hmi_Log_Context *context,
                                 uint16_t button_id,
                                 uint8_t value)
{
    if ((context == NULL) || !context->ready)
    {
        return false;
    }
    if ((button_id == A_HMI_LOG_FILTER_EVENT_BUTTON_ID) ||
        (button_id == A_HMI_REGULAR_TO_EVENT_BUTTON_ID))
    {
        if (value != 0U)
        {
            (void) A_HmiLog_Request(context, A_HMI_LOG_QUERY_EVENT);
        }
        return true;
    }
    if ((button_id == A_HMI_LOG_FILTER_REGULAR_BUTTON_ID) ||
        (button_id == A_HMI_EVENT_TO_REGULAR_BUTTON_ID))
    {
        if (value != 0U)
        {
            (void) A_HmiLog_Request(context, A_HMI_LOG_QUERY_REGULAR);
        }
        return true;
    }
    if (button_id == A_HMI_LOG_FILTER_ALL_TIME_BUTTON_ID)
    {
        context->edit_filter.time_enabled = (value == 0U);
        context->filter_status = A_HMI_LOG_FILTER_STATUS_READY;
        context->filter_refresh_mask |= (uint16_t) ((1U << 4U) | (1U << 7U));
        return true;
    }
    if (button_id == A_HMI_LOG_FILTER_CYLINDER_BUTTON_ID)
    {
        if (value != 0U)
        {
            context->edit_filter.cylinder_number =
                (uint8_t) ((context->edit_filter.cylinder_number + 1U) %
                           (GAS_CYLINDER_COUNT + 1U));
            context->filter_status = A_HMI_LOG_FILTER_STATUS_READY;
            context->filter_refresh_mask |= (uint16_t) ((1U << 5U) | (1U << 7U));
        }
        return true;
    }
    if (button_id == A_HMI_LOG_FILTER_STATE_BUTTON_ID)
    {
        if (value != 0U)
        {
            context->edit_filter.target_state =
                (uint8_t) ((context->edit_filter.target_state + 1U) %
                           ((uint8_t) GAS_CYL_WAIT_TEST + 1U));
            context->filter_status = A_HMI_LOG_FILTER_STATUS_READY;
            context->filter_refresh_mask |= (uint16_t) ((1U << 6U) | (1U << 7U));
        }
        return true;
    }
    if (button_id == A_HMI_LOG_FILTER_RESET_BUTTON_ID)
    {
        if (value != 0U)
        {
            A_HmiLog_SetDefaultFilter(&context->edit_filter);
            context->filter_status = A_HMI_LOG_FILTER_STATUS_RESET;
            context->filter_refresh_mask |= A_HMI_LOG_FILTER_REFRESH_ALL;
        }
        return true;
    }
    return false;
}

/*
 * 函数名：A_HmiLog_InputTask。
 * 说明：取出Screen6的YYYYMMDD或HHMMSS文本，校验并更新查询条件。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：无；错误文本会被恢复为上一个合法值。
 */
void A_HmiLog_InputTask(A_Hmi_Log_Context *context)
{
    A_Hmi_Log_Filter candidate;
    A_Hmi_Log_Date_Time *date_time;
    char text[F_HMI_TEXT_MAX_SIZE + 1U];
    uint16_t page_id;
    uint16_t control_id;
    uint8_t slot;
    uint32_t value;
    size_t length;
    bool valid = false;

    if ((context == NULL) || !context->ready || (context->hmi == NULL) ||
        !A_Hmi_PeekTextEvent(context->hmi, &page_id, &control_id) ||
        (page_id != A_HMI_LOG_FILTER_PAGE_ID) ||
        (control_id < A_HMI_LOG_FILTER_START_DATE_ID) ||
        (control_id > A_HMI_LOG_FILTER_END_TIME_ID))
    {
        return;
    }
    length = A_Hmi_TakeTextEvent(context->hmi, &page_id, &control_id,
                                 text, sizeof(text));
    if (length == 0U)
    {
        return;
    }

    candidate = context->edit_filter;
    slot = (uint8_t) (control_id - A_HMI_LOG_FILTER_START_DATE_ID);
    date_time = (slot < 2U) ? &candidate.start : &candidate.end;
    if (((slot == 0U) || (slot == 2U)) && (length == 8U) &&
        A_HmiLog_ParseDigits(text, length, &value))
    {
        date_time->year = (uint16_t) (value / 10000U);
        date_time->month = (uint8_t) ((value / 100U) % 100U);
        date_time->day = (uint8_t) (value % 100U);
        valid = A_HmiLog_DateTimeValid(date_time);
    }
    else if (((slot == 1U) || (slot == 3U)) && (length == 6U) &&
             A_HmiLog_ParseDigits(text, length, &value))
    {
        date_time->hour = (uint8_t) (value / 10000U);
        date_time->minute = (uint8_t) ((value / 100U) % 100U);
        date_time->second = (uint8_t) (value % 100U);
        valid = A_HmiLog_DateTimeValid(date_time);
    }

    if (valid)
    {
        context->edit_filter = candidate;
        context->filter_status = A_HMI_LOG_FILTER_STATUS_READY;
        context->filter_refresh_mask |= (uint16_t) (1U << 7U);
    }
    else
    {
        context->filter_status = A_HMI_LOG_FILTER_STATUS_INPUT_ERROR;
        context->filter_refresh_mask |= (uint16_t) ((1U << slot) | (1U << 7U));
        // 仅回写本输入框的旧值，其他已编辑条件保持不变。
    }
}

/*
 * 函数名：A_HmiLog_Request。
 * 说明：请求刷新指定类型日志，实际索引建立、第一页显示和后台统计由任务分步完成。
 * 输入：context为日志查询上下文；query_type为事件日志或常规日志类型。
 * 输出：模块已初始化且查询类型有效时返回true，否则返回false。
 */
bool A_HmiLog_Request(A_Hmi_Log_Context *context,
                      A_Hmi_Log_Query_Type query_type)
{
    if ((context == NULL) || !context->ready ||
        !A_GasLog_IsReady(context->log) ||
        ((query_type != A_HMI_LOG_QUERY_EVENT) &&
         (query_type != A_HMI_LOG_QUERY_REGULAR)))
    {
        return false;
    }
    context->active_filter = context->edit_filter;
    context->filter_status = A_HMI_LOG_FILTER_STATUS_READY;
    context->pending_type = query_type;
    context->request_pending = true;
    return true;
}

/*
 * 函数名：A_HmiLog_RequestPage。
 * 说明：请求显示指定日志类型的最新页、上一页或下一页；没有对应索引时自动从第1页开始刷新。
 * 输入：context为日志查询上下文；query_type为事件或常规日志；command为翻页命令。
 * 输出：请求参数有效并已被接收时返回true，否则返回false。
 */
bool A_HmiLog_RequestPage(A_Hmi_Log_Context *context,
                          A_Hmi_Log_Query_Type query_type,
                          A_Hmi_Log_Page_Command command)
{
    uint16_t base_page;

    if ((context == NULL) || !context->ready ||
        !A_GasLog_IsReady(context->log) ||
        ((query_type != A_HMI_LOG_QUERY_EVENT) &&
         (query_type != A_HMI_LOG_QUERY_REGULAR)) ||
        ((command != A_HMI_LOG_PAGE_LATEST) &&
         (command != A_HMI_LOG_PAGE_PREVIOUS) &&
         (command != A_HMI_LOG_PAGE_NEXT)))
    {
        return false;
    }

    if (!context->cache_valid || (context->query_type != query_type))
    {
        return A_HmiLog_Request(context, query_type);
    }
    if (context->snapshot_changed ||
        (context->log->next_sequence != context->snapshot_next_sequence))
    {
        context->snapshot_changed = true;
        context->page_request_pending = false;
        if (context->state == A_HMI_LOG_QUERY_IDLE)
        {
            context->state = A_HMI_LOG_QUERY_STATUS;
        }
        // 已失效的逻辑索引不能继续翻页，保留当前页并提示人员使用刷新按钮。
        return true;
    }

    base_page = context->page_request_pending ?
                context->requested_page : context->current_page;
    if (command == A_HMI_LOG_PAGE_LATEST)
    {
        context->requested_page = 0U;
    }
    else if (command == A_HMI_LOG_PAGE_PREVIOUS)
    {
        context->requested_page = (base_page > 0U) ?
                                  (uint16_t) (base_page - 1U) : 0U;
    }
    else
    {
        context->requested_page = (base_page < UINT16_MAX) ?
                                  (uint16_t) (base_page + 1U) : base_page;
    }
    context->page_request_pending = true;
    return true;
}

/*
 * 函数名：A_HmiLog_Task。
 * 说明：非阻塞执行索引建立、当前页读取、记录格式转换和串口发送，每次调用最多读一条或发一行。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：无；查询进度保存在context中。
 */
void A_HmiLog_Task(A_Hmi_Log_Context *context)
{
    uint16_t page_id;
    uint16_t record_control_id;

    if ((context == NULL) || !context->ready || !context->hmi->ready)
    {
        return;
    }
    if (context->request_pending)
    {
        A_HmiLog_StartRequest(context);
    }
    if ((context->state == A_HMI_LOG_QUERY_IDLE) &&
        !context->page_request_pending && (context->filter_refresh_mask != 0U))
    {
        (void) A_HmiLog_FilterRefreshTask(context);
        return;
    }
    if ((context->state == A_HMI_LOG_QUERY_IDLE) && context->cache_valid &&
        !context->snapshot_changed &&
        (context->log->next_sequence != context->snapshot_next_sequence))
    {
        context->snapshot_changed = true;
        context->page_request_pending = false;
        context->state = A_HMI_LOG_QUERY_STATUS;
        // 空闲查看期间产生新日志时也主动提示，但不改变人员正在阅读的当前页。
    }
    if ((context->state == A_HMI_LOG_QUERY_IDLE) && context->page_request_pending)
    {
        A_HmiLog_ProcessPageRequest(context);
    }
    if (context->state == A_HMI_LOG_QUERY_IDLE)
    {
        return;
    }
    if ((context->state != A_HMI_LOG_QUERY_CLEAR) && !context->snapshot_changed &&
        context->cache_valid &&
        (context->log->next_sequence != context->snapshot_next_sequence))
    {
        context->snapshot_changed = true;
        context->row_ready = false;
        context->current_record_ready = false;
        context->progress_pending = false;
        context->page_request_pending = false;
        context->state = A_HMI_LOG_QUERY_STATUS;
        // 索引或页面发送期间产生新日志时停止使用旧索引，保留已显示页并提示人员刷新。
    }

    page_id = A_HmiLog_GetPageId(context->query_type);
    record_control_id = A_HmiLog_GetRecordControlId(context->query_type);
    switch (context->state)
    {
        case A_HMI_LOG_QUERY_CLEAR:
            if (F_Hmi_SendRecordClear(&context->hmi->function, page_id, record_control_id))
            {
                context->table_is_clear = true;
                if (context->read_failed || context->filter_error)
                {
                    context->page_request_pending = false;
                    context->state = A_HMI_LOG_QUERY_PAGE_INFO;
                }
                else if (context->total_count == 0U)
                {
                    context->index_complete = true;
                    context->page_request_pending = false;
                    context->state = A_HMI_LOG_QUERY_PAGE_INFO;
                }
                else
                {
                    context->progress_pending = true;
                    context->state = A_HMI_LOG_QUERY_SCAN;
                }
            }
            break;

        case A_HMI_LOG_QUERY_SCAN:
            if (context->progress_pending)
            {
                if (A_HmiLog_SendProgress(context))
                {
                    context->progress_pending = false;
                }
                break;
            }
            if (context->scan_logical_index == 0U)
            {
                A_HmiLog_FinishIndex(context);
            }
            else if (!A_HmiLog_ScanOneRecord(context))
            {
                A_HmiLog_FailAndClear(context);
            }
            else if (context->page_request_pending &&
                     A_HmiLog_IsRequestedPageAvailable(context))
            {
                A_HmiLog_StartPage(context);
            }
            else if (context->scan_logical_index == 0U)
            {
                A_HmiLog_FinishIndex(context);
            }
            break;

        case A_HMI_LOG_QUERY_PAGE_CLEAR:
            if (F_Hmi_SendRecordClear(&context->hmi->function, page_id, record_control_id))
            {
                context->table_is_clear = true;
                context->state = A_HMI_LOG_QUERY_PAGE_READ;
            }
            break;

        case A_HMI_LOG_QUERY_PAGE_READ:
            if (context->page_cursor >= context->page_end_index)
            {
                context->page_rendered = context->sent_count > 0U;
                context->table_is_clear = !context->page_rendered;
                context->state = A_HMI_LOG_QUERY_PAGE_INFO;
                break;
            }
            if ((context->page_cursor >= context->matched_count) ||
                !A_GasLog_ReadRecord(context->log,
                                    context->log_index[context->page_cursor],
                                    context->current_record) ||
                (context->current_record[0] != A_HmiLog_GetRecordType(context->query_type)))
            {
                A_HmiLog_FailAndClear(context);
                break;
            }
            context->current_record_ready = true;
            context->regular_line_phase = 0U;
            context->state = A_HMI_LOG_QUERY_PAGE_ROWS;
            break;

        case A_HMI_LOG_QUERY_PAGE_ROWS:
            if (!context->current_record_ready)
            {
                A_HmiLog_FailAndClear(context);
                break;
            }
            if (!context->row_ready && !A_HmiLog_FormatCurrentRow(context))
            {
                A_HmiLog_FailAndClear(context);
                break;
            }
            if (F_Hmi_SendRecordAdd(&context->hmi->function,
                                    page_id,
                                    record_control_id,
                                    context->row,
                                    context->row_length))
            {
                context->row_ready = false;
                if ((context->query_type == A_HMI_LOG_QUERY_REGULAR) &&
                    (context->regular_line_phase == 0U))
                {
                    context->regular_line_phase++;
                }
                else
                {
                    context->current_record_ready = false;
                    context->regular_line_phase = 0U;
                    context->sent_count++;
                    context->page_cursor++;
                    context->state = A_HMI_LOG_QUERY_PAGE_READ;
                }
            }
            break;

        case A_HMI_LOG_QUERY_PAGE_INFO:
            if (A_HmiLog_SendPageInfo(context))
            {
                if (context->read_failed || context->filter_error || context->snapshot_changed)
                {
                    context->state = A_HMI_LOG_QUERY_STATUS;
                }
                else if (context->page_request_pending)
                {
                    A_HmiLog_ProcessPageRequest(context);
                }
                else if (!context->index_complete)
                {
                    context->state = A_HMI_LOG_QUERY_SCAN;
                }
                else
                {
                    context->state = A_HMI_LOG_QUERY_STATUS;
                }
            }
            break;

        case A_HMI_LOG_QUERY_STATUS:
            if (A_HmiLog_SendStatus(context))
            {
                context->state = A_HMI_LOG_QUERY_IDLE;
            }
            break;

        default:
            context->state = A_HMI_LOG_QUERY_IDLE;
            break;
    }
}

/*
 * 函数名：A_HmiLog_IsBusy。
 * 说明：查询日志刷新是否仍在进行，用于暂缓普通监控画面刷新以避免争用SCI9。
 * 输入：context为只读日志查询上下文。
 * 输出：存在待处理请求或查询尚未结束时返回true，否则返回false。
 */
bool A_HmiLog_IsBusy(const A_Hmi_Log_Context *context)
{
    return ((context != NULL) && context->ready &&
            (context->request_pending || context->page_request_pending ||
             (context->filter_refresh_mask != 0U) ||
             (context->state != A_HMI_LOG_QUERY_IDLE)));
}

/*
 * 函数名：A_HmiLog_InvalidateCache。
 * 说明：取消当前日志扫描或翻页并清除RAM索引，供EEPROM日志开始物理清除时调用。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：无；筛选条件保留，旧日志索引和待处理读取请求全部失效。
 */
void A_HmiLog_InvalidateCache(A_Hmi_Log_Context *context)
{
    if (context == NULL)
    {
        return;
    }

    context->state = A_HMI_LOG_QUERY_IDLE;
    context->request_pending = false;
    context->page_request_pending = false;
    context->cache_valid = false;
    context->index_complete = false;
    context->snapshot_changed = false;
    context->progress_pending = false;
    context->row_ready = false;
    context->current_record_ready = false;
    context->matched_count = 0U;
    context->scanned_count = 0U;
    context->total_count = 0U;
    context->current_page = 0U;
    context->requested_page = 0U;
    context->page_start_index = 0U;
    context->page_end_index = 0U;
    context->page_cursor = 0U;
    context->sent_count = 0U;
    // 不修改人员已经输入的时间、气瓶和状态筛选条件，下一次查询从空缓存重新建立索引。
}
