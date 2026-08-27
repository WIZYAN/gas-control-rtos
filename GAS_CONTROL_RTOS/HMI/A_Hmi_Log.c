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
 * 说明：保存日志流水号快照并初始化指定类型日志的清表和扫描过程。
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
    context->snapshot_changed = false;
    context->progress_pending = false;
    context->total_count = A_GasLog_GetCount(context->log);
    context->scan_logical_index = context->total_count;
    context->snapshot_next_sequence = context->log->next_sequence;
    context->state = A_HMI_LOG_QUERY_CLEAR;
    // 每次请求都建立快照，从最新端单遍扫描并流式发送全部匹配记录。
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
    context->row_ready = false;
    context->current_record_ready = false;
    context->regular_line_phase = 0U;
    context->progress_pending = false;
    context->state = A_HMI_LOG_QUERY_CLEAR;
}

/*
 * 函数名：A_HmiLog_ScanOneRecord。
 * 说明：从快照最新端向旧端读取一条日志，匹配时直接保存为待发记录。
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
    if (context->current_record[0] == A_HmiLog_GetRecordType(context->query_type))
    {
        context->matched_count++;
        context->current_record_ready = true;
        context->regular_line_phase = 0U;
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
 * 函数名：A_HmiLog_SendProgress。
 * 说明：向当前日志画面发送“扫描 已处理/快照总数”进度文本。
 * 输入：context为日志查询上下文输入输出指针。
 * 输出：成功启动发送时返回true，否则返回false并在后续周期重试。
 */
static bool A_HmiLog_SendProgress(A_Hmi_Log_Context *context)
{
    const char scan_text[] = "\xC9\xA8\xC3\xE8\x20"; // 扫描。
    char status[24];
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
    char status[24];
    const char *fixed_text = NULL;
    size_t fixed_length = 0U;
    size_t length = 0U;

    if (context->read_failed)
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
            !A_HmiLog_AppendUnsigned(status, &length, sizeof(status), context->sent_count, 1U) ||
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
 * 函数名：A_HmiLog_Init。
 * 说明：初始化串口屏日志查询实例，并关联HMI、EEPROM日志和气源系统状态。
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
    context->ready = true;
    return true;
}

/*
 * 函数名：A_HmiLog_Request。
 * 说明：请求按指定类型重新加载全部日志，实际EEPROM读取和串口发送由任务分步完成。
 * 输入：context为日志查询上下文；query_type为事件日志或常规日志类型。
 * 输出：模块已初始化且查询类型有效时返回true，否则返回false。
 */
bool A_HmiLog_Request(A_Hmi_Log_Context *context,
                      A_Hmi_Log_Query_Type query_type)
{
    if ((context == NULL) || !context->ready ||
        ((query_type != A_HMI_LOG_QUERY_EVENT) &&
         (query_type != A_HMI_LOG_QUERY_REGULAR)))
    {
        return false;
    }
    context->pending_type = query_type;
    context->request_pending = true;
    return true;
}

/*
 * 函数名：A_HmiLog_Task。
 * 说明：非阻塞执行清表、类型扫描、记录格式转换和串口发送，每次调用最多读取或发送一行数据。
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
    if (context->state == A_HMI_LOG_QUERY_IDLE)
    {
        return;
    }

    if ((context->state != A_HMI_LOG_QUERY_CLEAR) &&
        (context->log->next_sequence != context->snapshot_next_sequence))
    {
        context->snapshot_changed = true;
        context->row_ready = false;
        context->current_record_ready = false;
        context->progress_pending = false;
        context->state = A_HMI_LOG_QUERY_STATUS;
        // 查询期间产生新日志时停止使用旧快照，保留已显示行并提示人员重新刷新。
    }

    page_id = A_HmiLog_GetPageId(context->query_type);
    record_control_id = A_HmiLog_GetRecordControlId(context->query_type);
    switch (context->state)
    {
        case A_HMI_LOG_QUERY_CLEAR:
            if (F_Hmi_SendRecordClear(&context->hmi->function, page_id, record_control_id))
            {
                if (context->read_failed || (context->total_count == 0U))
                {
                    context->state = A_HMI_LOG_QUERY_STATUS;
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
                context->state = A_HMI_LOG_QUERY_STATUS;
            }
            else if (!A_HmiLog_ScanOneRecord(context))
            {
                A_HmiLog_FailAndClear(context);
            }
            else if (context->current_record_ready)
            {
                context->state = A_HMI_LOG_QUERY_ROWS;
            }
            else if (context->scan_logical_index == 0U)
            {
                context->state = A_HMI_LOG_QUERY_STATUS;
            }
            break;

        case A_HMI_LOG_QUERY_ROWS:
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
                    context->state = (context->scan_logical_index > 0U) ?
                                     A_HMI_LOG_QUERY_SCAN : A_HMI_LOG_QUERY_STATUS;
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
            (context->request_pending || (context->state != A_HMI_LOG_QUERY_IDLE)));
}
