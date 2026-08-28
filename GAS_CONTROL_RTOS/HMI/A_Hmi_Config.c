#include "A_Hmi_Config.h"

#include <stddef.h>
#include <string.h>

#define A_HMI_CONFIG_FIELD_MASK                (0x07FFU) // 11个可见参数对应刷新位图bit0～bit10。
#define A_HMI_CONFIG_REFRESH_ALL_MASK          (0x0FFFU) // 11项参数和确认/结果提示共12个刷新位。
#define A_HMI_CONFIG_STATUS_SLOT               (11U)     // 提示文本在刷新位图中的槽号。
#define A_HMI_CONFIG_DIALOG_NAME_SLOT          (12U)     // 确认子画面参数名称在刷新位图中的槽号。
#define A_HMI_CONFIG_DIALOG_OLD_SLOT           (13U)     // 确认子画面当前值在刷新位图中的槽号。
#define A_HMI_CONFIG_DIALOG_NEW_SLOT           (14U)     // 确认子画面候选值在刷新位图中的槽号。
#define A_HMI_CONFIG_DIALOG_INFO_SLOT          (15U)     // 确认子画面说明在刷新位图中的槽号。
#define A_HMI_CONFIG_DIALOG_MASK               (0xF000U) // 确认子画面四项文本对应刷新位图bit12～bit15。
#define A_HMI_CONFIG_ALL_FIELDS                (0xFFU)   // 恢复默认值确认时表示候选值包含全部字段。
#define A_HMI_CONFIG_SWITCH_RELEASE_OFFSET_RAW (100U)    // 串口屏保存时自动增加的0.100 MPa回差定点值。
#define A_HMI_LOG_CLEAR_COUNT_REFRESH          (0x01U)   // Screen7当前日志数量刷新位。
#define A_HMI_LOG_CLEAR_STATUS_REFRESH         (0x02U)   // Screen7等待、进度或结果文本刷新位。
#define A_HMI_LOG_CLEAR_ALL_REFRESH            (0x03U)   // Screen7全部动态文本刷新位。

// 参数页内部提示状态，不对外部通信公布。
typedef enum
{
    A_HMI_CONFIG_STATUS_READY = 0,
    A_HMI_CONFIG_STATUS_INPUT_FORMAT_ERROR,
    A_HMI_CONFIG_STATUS_CONFIRM,
    A_HMI_CONFIG_STATUS_CANCELLED,
    A_HMI_CONFIG_STATUS_SAVING,
    A_HMI_CONFIG_STATUS_SUCCESS,
    A_HMI_CONFIG_STATUS_RANGE_ERROR,
    A_HMI_CONFIG_STATUS_RELATION_ERROR,
    A_HMI_CONFIG_STATUS_STORAGE_ERROR,
    A_HMI_CONFIG_STATUS_DEFAULT_LOADED,
    A_HMI_CONFIG_STATUS_LOG_CLEARING,
    A_HMI_CONFIG_STATUS_LOG_CLEAR_SUCCESS,
    A_HMI_CONFIG_STATUS_LOG_CLEAR_FAILED
} A_Hmi_Config_Status;

// 单个输入字段的解析结果，用于区分文本格式错误和数值超范围错误。
typedef enum
{
    A_HMI_CONFIG_INPUT_VALID = 0,
    A_HMI_CONFIG_INPUT_FORMAT_ERROR,
    A_HMI_CONFIG_INPUT_RANGE_ERROR
} A_Hmi_Config_Input_Result;

/*
 * 函数名：A_HmiConfig_FormatUnsigned。
 * 说明：把无符号整数格式化为不带前导零的ASCII十进制文本。
 * 输入：value为数值；text为输出缓存；capacity为缓存容量。
 * 输出：成功时返回文本长度，容量不足时返回0。
 */
static size_t A_HmiConfig_FormatUnsigned(uint32_t value, char *text, size_t capacity)
{
    char reverse[10];
    size_t length = 0U;
    size_t index;

    do
    {
        reverse[length++] = (char) ('0' + (value % 10U));
        value /= 10U;
    } while ((value != 0U) && (length < sizeof(reverse)));

    if (length > capacity)
    {
        return 0U;
    }
    for (index = 0U; index < length; ++index)
    {
        text[index] = reverse[length - index - 1U];
    }
    return length;
}

/*
 * 函数名：A_HmiConfig_FormatFixed3。
 * 说明：把数值格式化为固定三位小数的ASCII文本，供MPa压力和秒制排气时间共用。
 * 输入：value为待格式化数值；text为输出缓存；capacity为缓存容量。
 * 输出：成功时返回文本长度，容量不足时返回0。
 */
static size_t A_HmiConfig_FormatFixed3(float value, char *text, size_t capacity)
{
    uint32_t raw = (uint32_t) ((value * GAS_CONFIG_PRESSURE_SCALE) + 0.5F);
    uint32_t fraction = raw % 1000U;
    size_t length = A_HmiConfig_FormatUnsigned(raw / 1000U, text, capacity);

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
 * 函数名：A_HmiConfig_TrimText。
 * 说明：去除串口屏文本首尾的空格、制表符和回车换行，并允许一个正号前缀。
 * 输入：text为文本指针输入输出参数；length为有效长度输入输出参数。
 * 输出：清理后仍含有效字符时返回true，否则返回false。
 */
static bool A_HmiConfig_TrimText(const char **text, size_t *length)
{
    if ((text == NULL) || (*text == NULL) || (length == NULL))
    {
        return false;
    }
    while ((*length != 0U) &&
           (((*text)[0] == ' ') || ((*text)[0] == '\t') ||
            ((*text)[0] == '\r') || ((*text)[0] == '\n')))
    {
        ++(*text);
        --(*length);
    }
    while ((*length != 0U) &&
           (((*text)[*length - 1U] == ' ') || ((*text)[*length - 1U] == '\t') ||
            ((*text)[*length - 1U] == '\r') || ((*text)[*length - 1U] == '\n')))
    {
        --(*length);
    }
    if ((*length != 0U) && ((*text)[0] == '+'))
    {
        ++(*text);
        --(*length);
    }
    return (*length != 0U);
}

/*
 * 函数名：A_HmiConfig_ParseUnsigned。
 * 说明：把允许首尾空白的ASCII十进制文本转换为32位无符号整数并检查溢出。
 * 输入：text为文本；length为有效字符数；value为结果输出指针。
 * 输出：格式和数值有效时返回true，否则返回false。
 */
static bool A_HmiConfig_ParseUnsigned(const char *text, size_t length, uint32_t *value)
{
    uint32_t result = 0U;
    size_t index;

    if ((value == NULL) || !A_HmiConfig_TrimText(&text, &length))
    {
        return false;
    }
    for (index = 0U; index < length; ++index)
    {
        uint8_t digit;
        if ((text[index] < '0') || (text[index] > '9'))
        {
            return false;
        }
        digit = (uint8_t) (text[index] - '0');
        if (result > ((UINT32_MAX - digit) / 10U))
        {
            return false;
        }
        result = (result * 10U) + digit;
    }
    *value = result;
    return true;
}

/*
 * 函数名：A_HmiConfig_ParseFixed3Raw。
 * 说明：解析整数或最多三位小数的非负文本，并转换为原值乘1000的定点整数。
 * 输入：text为待解析文本；length为字符数；raw为定点结果输出指针。
 * 输出：格式正确且32位定点计算未溢出时返回true，否则返回false；范围由调用者检查。
 */
static bool A_HmiConfig_ParseFixed3Raw(const char *text, size_t length, uint32_t *raw)
{
    uint32_t integer = 0U;
    uint32_t fraction = 0U;
    uint32_t scale = 100U;
    size_t index;
    bool decimal_seen = false;
    uint8_t fraction_digits = 0U;

    if ((raw == NULL) || !A_HmiConfig_TrimText(&text, &length))
    {
        return false;
    }
    for (index = 0U; index < length; ++index)
    {
        if (text[index] == '.')
        {
            if (decimal_seen)
            {
                return false;
            }
            decimal_seen = true;
            continue;
        }
        if ((text[index] < '0') || (text[index] > '9'))
        {
            return false;
        }
        if (!decimal_seen)
        {
            uint32_t digit = (uint32_t) (text[index] - '0');
            if (integer > ((UINT32_MAX - digit) / 10U))
            {
                return false;
            }
            integer = (integer * 10U) + digit;
        }
        else
        {
            if (fraction_digits >= 3U)
            {
                return false;
            }
            fraction += (uint32_t) (text[index] - '0') * scale;
            scale /= 10U;
            ++fraction_digits;
        }
    }
    if (integer > ((UINT32_MAX - fraction) / 1000U))
    {
        return false;
    }
    *raw = (integer * 1000U) + fraction;
    return true;
}

/*
 * 函数名：A_HmiConfig_ApplyHiddenParameters。
 * 说明：根据切瓶压力自动生成0.100 MPa回差，并把压力新鲜度固定为2500 ms。
 * 输入：config为参数页完整编辑缓存输入输出指针。
 * 输出：无；更新switch_release_mpa和pressure_fresh_ms，最终关系仍由统一参数校验确认。
 */
static void A_HmiConfig_ApplyHiddenParameters(Gas_Config *config)
{
    uint32_t switch_raw;
    uint32_t release_raw;

    if (config == NULL)
    {
        return;
    }

    switch_raw = (uint32_t) ((config->switch_pressure_mpa * GAS_CONFIG_PRESSURE_SCALE) + 0.5F);
    release_raw = switch_raw + A_HMI_CONFIG_SWITCH_RELEASE_OFFSET_RAW;
    if (release_raw > 65535U)
    {
        release_raw = 65535U;
    }
    config->switch_release_mpa = (float) release_raw / GAS_CONFIG_PRESSURE_SCALE;
    config->pressure_fresh_ms = GAS_DEFAULT_PRESSURE_FRESH_MS;
    // 两项不再出现在串口屏参数页，统一规则可避免隐藏旧值与11项可见参数发生冲突。
}

/*
 * 函数名：A_HmiConfig_SetStatus。
 * 说明：更新参数页提示状态并把提示槽加入待刷新位图。
 * 输入：context为参数模块上下文；status为内部提示编号。
 * 输出：无；context中的状态和刷新位被更新。
 */
static void A_HmiConfig_SetStatus(A_Hmi_Config_Context *context,
                                  A_Hmi_Config_Status status)
{
    context->status = (uint8_t) status;
    context->refresh_mask |= (uint16_t) (1U << A_HMI_CONFIG_STATUS_SLOT);
}

/*
 * 函数名：A_HmiConfig_UpdateField。
 * 说明：将一个文本输入按控件序号转换并写入对应编辑参数，排气时间使用三位小数秒输入。
 * 输入：context为参数模块上下文；field为0～10字段序号；text和length为上传文本。
 * 输出：返回有效、格式错误或范围错误，调用者据此显示明确提示。
 */
static A_Hmi_Config_Input_Result A_HmiConfig_UpdateField(A_Hmi_Config_Context *context,
                                                          uint8_t field,
                                                          const char *text,
                                                          size_t length)
{
    uint32_t value;

    if (field <= 3U)
    {
        if (!A_HmiConfig_ParseFixed3Raw(text, length, &value))
        {
            return A_HMI_CONFIG_INPUT_FORMAT_ERROR;
        }
        if ((value == 0U) || (value > 65535U) ||
            ((field == 2U) &&
             ((value < 1500U) || (value > 5000U))))
        {
            return A_HMI_CONFIG_INPUT_RANGE_ERROR;
        }
        switch (field)
        {
            case 0U:
                context->edit_config.switch_pressure_mpa = (float) value / 1000.0F;
                A_HmiConfig_ApplyHiddenParameters(&context->edit_config);
                break;
            case 1U: context->edit_config.ready_min_pressure_mpa = (float) value / 1000.0F; break;
            case 2U: context->edit_config.low_warning_pressure_mpa = (float) value / 1000.0F; break;
            default: context->edit_config.pressure_max_mpa = (float) value / 1000.0F; break;
        }
        return A_HMI_CONFIG_INPUT_VALID;
    }

    if (field == 9U)
    {
        if (!A_HmiConfig_ParseFixed3Raw(text, length, &value))
        {
            return A_HMI_CONFIG_INPUT_FORMAT_ERROR;
        }
        if ((value < GAS_MANUAL_EXHAUST_TIME_MIN_MS) ||
            (value > GAS_MANUAL_EXHAUST_TIME_MAX_MS))
        {
            return A_HMI_CONFIG_INPUT_RANGE_ERROR;
        }
        context->edit_config.manual_exhaust_time_ms = value;
        // 排气时间以秒显示并允许三位小数，定点结果恰好对应运行时毫秒数。
        return A_HMI_CONFIG_INPUT_VALID;
    }

    if (!A_HmiConfig_ParseUnsigned(text, length, &value))
    {
        return A_HMI_CONFIG_INPUT_FORMAT_ERROR;
    }
    switch (field)
    {
        case 4U:
            if ((value == 0U) || (value > GAS_CONFIG_TIME_MAX_MS))
            {
                return A_HMI_CONFIG_INPUT_RANGE_ERROR;
            }
            context->edit_config.valve_pull_in_time_ms = value;
            break;
        case 5U:
            if (value > GAS_CONFIG_TIME_MAX_MS) { return A_HMI_CONFIG_INPUT_RANGE_ERROR; }
            context->edit_config.low_confirm_time_ms = value;
            break;
        case 6U:
            if ((value == 0U) || (value > GAS_CONFIG_SAMPLE_MAX_COUNT))
            {
                return A_HMI_CONFIG_INPUT_RANGE_ERROR;
            }
            context->edit_config.low_confirm_samples = (uint16_t) value;
            break;
        case 7U:
            if (value > GAS_CONFIG_TIME_MAX_MS) { return A_HMI_CONFIG_INPUT_RANGE_ERROR; }
            context->edit_config.valve_close_wait_ms = value;
            break;
        case 8U:
            if (value > GAS_CONFIG_TIME_MAX_MS) { return A_HMI_CONFIG_INPUT_RANGE_ERROR; }
            context->edit_config.valve_open_wait_ms = value;
            break;
        case 10U:
            if ((value < 5U) || (value > 60U)) { return A_HMI_CONFIG_INPUT_RANGE_ERROR; }
            context->edit_config.test_valve_max_time_ms = value * 1000U;
            break;
        default:
            return A_HMI_CONFIG_INPUT_FORMAT_ERROR;
    }
    return A_HMI_CONFIG_INPUT_VALID;
}

/*
 * 函数名：A_HmiConfig_FormatField。
 * 说明：按参数页单位把指定编辑参数格式化为ASCII显示文本，排气时间保留三位小数秒。
 * 输入：config为编辑参数；field为0～10字段序号；text为输出缓存；capacity为容量。
 * 输出：成功时返回文本长度，字段或容量无效时返回0。
 */
static size_t A_HmiConfig_FormatField(const Gas_Config *config,
                                      uint8_t field,
                                      char *text,
                                      size_t capacity)
{
    switch (field)
    {
        case 0U: return A_HmiConfig_FormatFixed3(config->switch_pressure_mpa, text, capacity);
        case 1U: return A_HmiConfig_FormatFixed3(config->ready_min_pressure_mpa, text, capacity);
        case 2U: return A_HmiConfig_FormatFixed3(config->low_warning_pressure_mpa, text, capacity);
        case 3U: return A_HmiConfig_FormatFixed3(config->pressure_max_mpa, text, capacity);
        case 4U: return A_HmiConfig_FormatUnsigned(config->valve_pull_in_time_ms, text, capacity);
        case 5U: return A_HmiConfig_FormatUnsigned(config->low_confirm_time_ms, text, capacity);
        case 6U: return A_HmiConfig_FormatUnsigned(config->low_confirm_samples, text, capacity);
        case 7U: return A_HmiConfig_FormatUnsigned(config->valve_close_wait_ms, text, capacity);
        case 8U: return A_HmiConfig_FormatUnsigned(config->valve_open_wait_ms, text, capacity);
        case 9U: return A_HmiConfig_FormatFixed3((float) config->manual_exhaust_time_ms / 1000.0F,
                                                    text,
                                                    capacity);
        case 10U: return A_HmiConfig_FormatUnsigned(config->test_valve_max_time_ms / 1000U, text, capacity);
        default: return 0U;
    }
}

/*
 * 函数名：A_HmiConfig_FormatLogClearCount。
 * 说明：把日志数量格式化为Screen7使用的“当前日志：N条”GBK文本。
 * 输入：count为日志数量；text为输出缓存；capacity为缓存容量。
 * 输出：成功时返回文本字节数，容量不足时返回0。
 */
static size_t A_HmiConfig_FormatLogClearCount(uint16_t count,
                                               char *text,
                                               size_t capacity)
{
    const char prefix[] = "\xB5\xB1\xC7\xB0\xC8\xD5\xD6\xBE\xA3\xBA"; // 当前日志：。
    const char suffix[] = "\xCC\xF5"; // 条。
    size_t length = sizeof(prefix) - 1U;
    size_t number_length;

    if ((text == NULL) || (capacity < (length + sizeof(suffix))))
    {
        return 0U;
    }
    (void) memcpy(text, prefix, length);
    number_length = A_HmiConfig_FormatUnsigned(count,
                                                &text[length],
                                                capacity - length - (sizeof(suffix) - 1U));
    if (number_length == 0U)
    {
        return 0U;
    }
    length += number_length;
    (void) memcpy(&text[length], suffix, sizeof(suffix) - 1U);
    return length + sizeof(suffix) - 1U;
}

/*
 * 函数名：A_HmiConfig_FormatLogClearStatus。
 * 说明：把日志清除等待、百分比进度和最终结果格式化为GBK文本。
 * 输入：status为日志清除状态；progress为0～100进度；text为输出缓存；capacity为容量。
 * 输出：成功时返回文本字节数，容量不足或参数无效时返回0。
 */
static size_t A_HmiConfig_FormatLogClearStatus(A_Hmi_Log_Clear_Status status,
                                                uint8_t progress,
                                                char *text,
                                                size_t capacity)
{
    const char wait_text[] = "\xB5\xC8\xB4\xFD\xC8\xB7\xC8\xCF"; // 等待确认。
    const char busy_text[] = "\xD5\xFD\xD4\xDA\xC7\xE5\xB3\xFD\xA3\xBA"; // 正在清除：。
    const char success_text[] = "\xC7\xE5\xB3\xFD\xB3\xC9\xB9\xA6\xA3\xAC\xB5\xB1\xC7\xB0\x30\xCC\xF5"; // 清除成功，当前0条。
    const char failed_text[] = "\xC7\xE5\xB3\xFD\xCA\xA7\xB0\xDC\xA3\xAC\xC7\xEB\xD6\xD8\xCA\xD4"; // 清除失败，请重试。
    const char *fixed_text = wait_text;
    size_t fixed_length = sizeof(wait_text) - 1U;
    size_t length;
    size_t number_length;

    if ((text == NULL) || (capacity == 0U))
    {
        return 0U;
    }
    if (status == A_HMI_LOG_CLEAR_SUCCESS)
    {
        fixed_text = success_text;
        fixed_length = sizeof(success_text) - 1U;
    }
    else if (status == A_HMI_LOG_CLEAR_FAILED)
    {
        fixed_text = failed_text;
        fixed_length = sizeof(failed_text) - 1U;
    }
    else if (status == A_HMI_LOG_CLEAR_BUSY)
    {
        length = sizeof(busy_text) - 1U;
        if (capacity < (length + 2U))
        {
            return 0U;
        }
        (void) memcpy(text, busy_text, length);
        number_length = A_HmiConfig_FormatUnsigned(progress,
                                                    &text[length],
                                                    capacity - length - 1U);
        if (number_length == 0U)
        {
            return 0U;
        }
        length += number_length;
        text[length++] = '%';
        return length;
    }
    if (fixed_length > capacity)
    {
        return 0U;
    }
    (void) memcpy(text, fixed_text, fixed_length);
    return fixed_length;
}

/*
 * 函数名：A_HmiConfig_GetStatusText。
 * 说明：把内部提示状态转换为串口屏GBK文本字节串。
 * 输入：status为内部提示编号；length为文本字节数输出指针。
 * 输出：返回只读GBK文本指针，未知状态返回准备提示。
 */
static const char *A_HmiConfig_GetStatusText(uint8_t status, size_t *length)
{
    const char *text;

    switch ((A_Hmi_Config_Status) status)
    {
        case A_HMI_CONFIG_STATUS_INPUT_FORMAT_ERROR:
            text = "\xCA\xE4\xC8\xEB\xB8\xF1\xCA\xBD\xD3\xD0\xCE\xF3\xA3\xAC\xD2\xD1\xBB\xD6\xB8\xB4\xD4\xAD\xD6\xB5";
            break;
        case A_HMI_CONFIG_STATUS_CONFIRM:
            text = "\xB5\xC8\xB4\xFD\xC8\xB7\xC8\xCF\xA3\xAC\xB1\xBE\xB4\xCE\xD0\xDE\xB8\xC4\xC9\xD0\xCE\xB4\xC9\xFA\xD0\xA7";
            break;
        case A_HMI_CONFIG_STATUS_CANCELLED:
            text = "\xD2\xD1\xC8\xA1\xCF\xFB\xA3\xAC\xB1\xBE\xB4\xCE\xCE\xB4\xD0\xDE\xB8\xC4\xD4\xCB\xD0\xD0\xB2\xCE\xCA\xFD";
            break;
        case A_HMI_CONFIG_STATUS_SAVING:
            text = "\xD5\xFD\xD4\xDA\xD0\xB4\xC8\xEB" "EEPROM"
                   "\xB2\xA2\xD3\xA6\xD3\xC3\xB2\xCE\xCA\xFD";
            break;
        case A_HMI_CONFIG_STATUS_SUCCESS:
            text = "\xB2\xCE\xCA\xFD\xB1\xA3\xB4\xE6\xB3\xC9\xB9\xA6\xB2\xA2\xD2\xD1\xC9\xFA\xD0\xA7";
            break;
        case A_HMI_CONFIG_STATUS_RANGE_ERROR:
            text = "\xCA\xE4\xC8\xEB\xD6\xB5\xB3\xAC\xB3\xF6\xD4\xCA\xD0\xED\xB7\xB6\xCE\xA7\xA3\xAC\xD2\xD1\xBB\xD6\xB8\xB4\xD4\xAD\xD6\xB5";
            break;
        case A_HMI_CONFIG_STATUS_RELATION_ERROR:
            text = "\xB2\xCE\xCA\xFD\xB9\xD8\xCF\xB5\xB2\xBB\xBA\xCF\xB7\xA8\xA3\xAC\xD2\xD1\xBB\xD6\xB8\xB4\xD4\xAD\xD6\xB5";
            break;
        case A_HMI_CONFIG_STATUS_STORAGE_ERROR:
            text = "\xB1\xA3\xB4\xE6\xCA\xA7\xB0\xDC\xA3\xBA" "EEPROM"
                   "\xB6\xC1\xD0\xB4\xD2\xEC\xB3\xA3";
            break;
        case A_HMI_CONFIG_STATUS_DEFAULT_LOADED:
            text = "\xB5\xC8\xB4\xFD\xC8\xB7\xC8\xCF\xBB\xD6\xB8\xB4\xC8\xAB\xB2\xBF\xC4\xAC\xC8\xCF\xB2\xCE\xCA\xFD";
            break;
        case A_HMI_CONFIG_STATUS_LOG_CLEARING:
            text = "\xD5\xFD\xD4\xDA\xC7\xE5\xB3\xFD\xC8\xD5\xD6\xBE\xA3\xAC\xC7\xEB\xCE\xF0\xB6\xCF\xB5\xE7";
            break;
        case A_HMI_CONFIG_STATUS_LOG_CLEAR_SUCCESS:
            text = "\xC8\xD5\xD6\xBE\xC7\xE5\xB3\xFD\xB3\xC9\xB9\xA6";
            break;
        case A_HMI_CONFIG_STATUS_LOG_CLEAR_FAILED:
            text = "\xC8\xD5\xD6\xBE\xC7\xE5\xB3\xFD\xCA\xA7\xB0\xDC";
            break;
        default:
            text = "\xD0\xDE\xB8\xC4\xC8\xCE\xD2\xBB\xB2\xCE\xCA\xFD\xBA\xF3\xBD\xAB\xD7\xD4\xB6\xAF\xB5\xAF\xB3\xF6\xC8\xB7\xC8\xCF\xB4\xB0\xBF\xDA";
            break;
    }
    *length = strlen(text);
    return text;
}

/*
 * 函数名：A_HmiConfig_GetFieldName。
 * 说明：把待确认字段序号转换为确认子画面使用的GBK参数名称。
 * 输入：field为0～10字段序号，0xFF表示全部运行参数；length为字节数输出指针。
 * 输出：返回只读GBK文本指针；字段无效时返回“全部运行参数”。
 */
static const char *A_HmiConfig_GetFieldName(uint8_t field, size_t *length)
{
    static const char *const names[A_HMI_CONFIG_TEXT_COUNT] =
    {
        "\xC7\xD0\xC6\xBF\xD1\xB9\xC1\xA6",
        "\xB4\xFD\xD3\xC3\xD7\xEE\xB5\xCD\xD1\xB9\xC1\xA6",
        "\xB5\xCD\xD1\xB9\xBE\xAF\xB8\xE6\xD1\xB9\xC1\xA6",
        "\xD1\xB9\xC1\xA6\xBA\xCF\xB7\xA8\xC9\xCF\xCF\xDE",
        "12V\xC7\xBF\xCE\xFC\xBA\xCF\xCA\xB1\xBC\xE4",
        "\xB5\xCD\xD1\xB9\xC8\xB7\xC8\xCF\xCA\xB1\xBC\xE4",
        "\xB5\xCD\xD1\xB9\xC8\xB7\xC8\xCF\xD1\xF9\xB1\xBE\xCA\xFD",
        "\xB9\xD8\xB1\xD5\xB7\xA7\xB5\xC8\xB4\xFD",
        "\xB4\xF2\xBF\xAA\xB7\xA7\xB5\xC8\xB4\xFD",
        "\xCA\xD6\xB6\xAF\xC5\xC5\xC6\xF8\xCA\xB1\xBC\xE4",
        "\xB2\xE2\xCA\xD4\xB7\xA7\xD7\xEE\xB3\xA4\xBF\xAA\xC6\xF4"
    };
    const char *text = "\xC8\xAB\xB2\xBF\xD4\xCB\xD0\xD0\xB2\xCE\xCA\xFD";

    if (field < A_HMI_CONFIG_TEXT_COUNT)
    {
        text = names[field];
    }
    *length = strlen(text);
    return text;
}

/*
 * 函数名：A_HmiConfig_GetDialogInfoText。
 * 说明：根据当前校验状态生成确认子画面的GBK操作说明。
 * 输入：status为参数模块内部状态；length为字节数输出指针。
 * 输出：返回只读GBK文本指针，正常候选值提示确认后立即生效。
 */
static const char *A_HmiConfig_GetDialogInfoText(uint8_t status, size_t *length)
{
    const char *text;

    switch ((A_Hmi_Config_Status) status)
    {
        case A_HMI_CONFIG_STATUS_DEFAULT_LOADED:
            text = "\xC8\xB7\xC8\xCF\xBA\xF3\xBB\xD6\xB8\xB4\xC8\xAB\xB2\xBF\xC4\xAC\xC8\xCF\xB2\xCE\xCA\xFD";
            break;
        case A_HMI_CONFIG_STATUS_INPUT_FORMAT_ERROR:
            text = "\xCA\xE4\xC8\xEB\xB8\xF1\xCA\xBD\xD3\xD0\xCE\xF3\xA3\xAC\xC7\xEB\xB7\xB5\xBB\xD8\xD0\xDE\xB8\xC4";
            break;
        case A_HMI_CONFIG_STATUS_RANGE_ERROR:
            text = "\xCA\xE4\xC8\xEB\xD6\xB5\xB3\xAC\xB3\xF6\xD4\xCA\xD0\xED\xB7\xB6\xCE\xA7\xA3\xAC\xC7\xEB\xB7\xB5\xBB\xD8\xD0\xDE\xB8\xC4";
            break;
        case A_HMI_CONFIG_STATUS_RELATION_ERROR:
            text = "\xB2\xCE\xCA\xFD\xB9\xD8\xCF\xB5\xB2\xBB\xBA\xCF\xB7\xA8\xA3\xAC\xC7\xEB\xB7\xB5\xBB\xD8\xD0\xDE\xB8\xC4";
            break;
        default:
            text = "\xC8\xB7\xC8\xCF\xBA\xF3\xC1\xA2\xBC\xB4\xB1\xA3\xB4\xE6\xB2\xA2\xC9\xFA\xD0\xA7";
            break;
    }
    *length = strlen(text);
    return text;
}

/*
 * 函数名：A_HmiConfig_CopyInputText。
 * 说明：复制去除首尾空白后的用户输入，供非法候选值在确认子画面中回显。
 * 输入：destination和capacity为目标缓存；text和length为串口屏输入文本。
 * 输出：无；目标缓存始终以空字符结尾，输入无效时得到空字符串。
 */
static void A_HmiConfig_CopyInputText(char *destination,
                                      size_t capacity,
                                      const char *text,
                                      size_t length)
{
    if ((destination == NULL) || (capacity == 0U))
    {
        return;
    }
    destination[0] = '\0';
    if (!A_HmiConfig_TrimText(&text, &length))
    {
        return;
    }
    if (length >= capacity)
    {
        length = capacity - 1U;
    }
    (void) memcpy(destination, text, length);
    destination[length] = '\0';
}

/*
 * 函数名：A_HmiConfig_RequestDialogRefresh。
 * 说明：把确认子画面的名称、当前值、候选值和说明四个文本加入优先刷新队列。
 * 输入：context为参数模块上下文。
 * 输出：无；更新refresh_mask的高四位。
 */
static void A_HmiConfig_RequestDialogRefresh(A_Hmi_Config_Context *context)
{
    context->refresh_mask |= A_HMI_CONFIG_DIALOG_MASK;
}

/*
 * 函数名：A_HmiConfig_Init。
 * 说明：初始化串口屏参数编辑模块并关联已有HMI通信实例。
 * 输入：context为参数模块上下文；hmi为已经初始化或即将初始化的HMI应用实例。
 * 输出：参数有效时返回true，否则返回false。
 */
bool A_HmiConfig_Init(A_Hmi_Config_Context *context, A_Hmi_Context *hmi)
{
    if ((context == NULL) || (hmi == NULL))
    {
        return false;
    }
    (void) memset(context, 0, sizeof(*context));
    context->hmi = hmi;
    return true;
}

/*
 * 函数名：A_HmiConfig_Open。
 * 说明：进入参数页时复制完整运行参数、应用两项本地固定规则，并安排11个输入控件分时刷新。
 * 输入：context为参数模块上下文；config为当前生效参数。
 * 输出：参数有效时返回true，否则返回false。
 */
bool A_HmiConfig_Open(A_Hmi_Config_Context *context,
                      const Gas_Config *config)
{
    if ((context == NULL) || (config == NULL) || (context->hmi == NULL))
    {
        return false;
    }
    context->edit_config = *config;
    A_HmiConfig_ApplyHiddenParameters(&context->edit_config);
    context->active = true;
    context->confirm_pending = false;
    context->save_pending = false;
    context->pending_field = A_HMI_CONFIG_ALL_FIELDS;
    context->pending_old_text[0] = '\0';
    context->pending_new_text[0] = '\0';
    context->refresh_mask = A_HMI_CONFIG_REFRESH_ALL_MASK;
    context->status = (uint8_t) A_HMI_CONFIG_STATUS_READY;
    context->log_clear_dialog_active = false;
    context->log_clear_request_pending = false;
    return true;
}

/*
 * 函数名：A_HmiConfig_HandleButton。
 * 说明：识别确认子画面、恢复默认及返回按钮，确认前不直接写EEPROM。
 * 输入：context为参数模块上下文；button_id和value为按钮事件；current_config为当前生效参数。
 * 输出：按钮属于参数模块并被识别时返回true，否则返回false。
 */
bool A_HmiConfig_HandleButton(A_Hmi_Config_Context *context,
                              uint16_t button_id,
                              uint8_t value,
                              const Gas_Config *current_config)
{
    if ((context == NULL) || (current_config == NULL))
    {
        return false;
    }
    if (button_id == A_HMI_CONFIG_MENU_BUTTON_ID)
    {
        return (value != 0U) ? A_HmiConfig_Open(context, current_config) : true;
    }
    if ((button_id != A_HMI_CONFIG_CONFIRM_BUTTON_ID) &&
        (button_id != A_HMI_CONFIG_CANCEL_BUTTON_ID) &&
        (button_id != A_HMI_CONFIG_DEFAULT_BUTTON_ID) &&
        (button_id != A_HMI_CONFIG_BACK_BUTTON_ID))
    {
        return false;
    }
    if (!context->active)
    {
        (void) A_HmiConfig_Open(context, current_config);
        // 即使导航按钮未上传，参数页上的第一个业务按钮也能建立安全的当前参数编辑副本。
    }
    if (button_id == A_HMI_CONFIG_CONFIRM_BUTTON_ID)
    {
        if (value != 0U)
        {
            if (context->confirm_pending)
            {
                context->confirm_pending = false;
                context->save_pending = true;
                A_HmiConfig_SetStatus(context, A_HMI_CONFIG_STATUS_SAVING);
            }
            // 非法输入没有可保存候选值，确认按钮只关闭子画面，不得生成保存请求。
        }
        return true;
    }
    if (button_id == A_HMI_CONFIG_CANCEL_BUTTON_ID)
    {
        if (value != 0U)
        {
            context->edit_config = *current_config;
            A_HmiConfig_ApplyHiddenParameters(&context->edit_config);
            context->confirm_pending = false;
            context->save_pending = false;
            if (context->pending_field < A_HMI_CONFIG_TEXT_COUNT)
            {
                context->refresh_mask |= (uint16_t) (1U << context->pending_field);
            }
            else
            {
                context->refresh_mask |= A_HMI_CONFIG_FIELD_MASK;
            }
            A_HmiConfig_SetStatus(context, A_HMI_CONFIG_STATUS_CANCELLED);
            // 返回修改始终恢复当前已生效参数，避免被取消的候选值留在主页面。
        }
        return true;
    }
    if (button_id == A_HMI_CONFIG_DEFAULT_BUTTON_ID)
    {
        if (value != 0U)
        {
            const char *current_text = "\xB5\xB1\xC7\xB0\xB2\xCE\xCA\xFD";
            const char *default_text = "\xC4\xAC\xC8\xCF\xB2\xCE\xCA\xFD";

            A_GasConfig_LoadDefaults(&context->edit_config);
            A_HmiConfig_ApplyHiddenParameters(&context->edit_config);
            context->confirm_pending = true;
            context->save_pending = false;
            context->pending_field = A_HMI_CONFIG_ALL_FIELDS;
            (void) strcpy(context->pending_old_text, current_text);
            (void) strcpy(context->pending_new_text, default_text);
            A_HmiConfig_SetStatus(context, A_HMI_CONFIG_STATUS_DEFAULT_LOADED);
            A_HmiConfig_RequestDialogRefresh(context);
            // 恢复默认仅建立完整候选参数，Lua同步打开子画面，确认后才持久化和生效。
        }
        return true;
    }
    if (button_id == A_HMI_CONFIG_BACK_BUTTON_ID)
    {
        context->active = false;
        context->confirm_pending = false;
        context->save_pending = false;
        return true;
    }
    return false;
}

/*
 * 函数名：A_HmiConfig_InputTask。
 * 说明：取出一条参数文本上传事件，建立单字段候选值并安排确认子画面刷新。
 * 输入：context为参数模块上下文；current_config为当前参数。
 * 输出：无；编辑缓存、提示状态和刷新位图可能被更新。
 */
void A_HmiConfig_InputTask(A_Hmi_Config_Context *context,
                           const Gas_Config *current_config)
{
    char text[F_HMI_TEXT_MAX_SIZE + 1U];
    uint16_t page_id;
    uint16_t control_id;
    size_t length;
    size_t formatted_length;
    uint8_t field;
    A_Hmi_Config_Input_Result input_result;
    A_Gas_Config_Validation validation;

    if ((context == NULL) || (current_config == NULL) || (context->hmi == NULL))
    {
        return;
    }
    if (!A_Hmi_PeekTextEvent(context->hmi, &page_id, &control_id) ||
        (page_id != A_HMI_CONFIG_PAGE_ID) ||
        (control_id < A_HMI_CONFIG_TEXT_BASE) ||
        (control_id >= (A_HMI_CONFIG_TEXT_BASE + A_HMI_CONFIG_TEXT_COUNT)))
    {
        return;
    }
    length = A_Hmi_TakeTextEvent(context->hmi,
                                 &page_id,
                                 &control_id,
                                 text,
                                 sizeof(text));
    if (length == 0U)
    {
        return;
    }
    if (!context->active)
    {
        (void) A_HmiConfig_Open(context, current_config);
        // 文本控件页面ID可作为兜底的入页证据，先复制当前参数再应用本字段，避免其他字段使用静态初值。
    }

    field = (uint8_t) (control_id - A_HMI_CONFIG_TEXT_BASE);
    context->edit_config = *current_config;
    A_HmiConfig_ApplyHiddenParameters(&context->edit_config);
    context->pending_field = field;
    context->confirm_pending = false;
    context->save_pending = false;

    formatted_length = A_HmiConfig_FormatField(current_config,
                                                field,
                                                context->pending_old_text,
                                                sizeof(context->pending_old_text) - 1U);
    context->pending_old_text[formatted_length] = '\0';
    A_HmiConfig_CopyInputText(context->pending_new_text,
                              sizeof(context->pending_new_text),
                              text,
                              length);

    input_result = A_HmiConfig_UpdateField(context, field, text, length);
    if (input_result == A_HMI_CONFIG_INPUT_VALID)
    {
        A_HmiConfig_ApplyHiddenParameters(&context->edit_config);
        validation = A_GasConfig_Validate(&context->edit_config);
        if (validation == A_GAS_CONFIG_VALID)
        {
            formatted_length = A_HmiConfig_FormatField(&context->edit_config,
                                                        field,
                                                        context->pending_new_text,
                                                        sizeof(context->pending_new_text) - 1U);
            context->pending_new_text[formatted_length] = '\0';
            context->confirm_pending = true;
            A_HmiConfig_SetStatus(context, A_HMI_CONFIG_STATUS_CONFIRM);
            // 每次只提交一个字段候选值，确认成功后下一次编辑重新以当前运行参数为基准。
        }
        else
        {
            context->edit_config = *current_config;
            A_HmiConfig_ApplyHiddenParameters(&context->edit_config);
            context->refresh_mask |= (uint16_t) (1U << field);
            A_HmiConfig_SetStatus(context,
                (validation == A_GAS_CONFIG_INVALID_RELATION) ?
                A_HMI_CONFIG_STATUS_RELATION_ERROR : A_HMI_CONFIG_STATUS_RANGE_ERROR);
        }
    }
    else
    {
        context->edit_config = *current_config;
        A_HmiConfig_ApplyHiddenParameters(&context->edit_config);
        context->refresh_mask |= (uint16_t) (1U << field);
        A_HmiConfig_SetStatus(context,
            (input_result == A_HMI_CONFIG_INPUT_FORMAT_ERROR) ?
            A_HMI_CONFIG_STATUS_INPUT_FORMAT_ERROR : A_HMI_CONFIG_STATUS_RANGE_ERROR);
        // 非法文本不生成保存请求，主页面恢复原值，子画面显示具体格式或范围原因。
    }
    A_HmiConfig_RequestDialogRefresh(context);
}

/*
 * 函数名：A_HmiConfig_TakeSaveRequest。
 * 说明：根据11项可见输入补齐自动回差和固定新鲜度，向业务层交付完整13项参数。
 * 输入：context为参数模块上下文；config为候选参数输出指针。
 * 输出：存在待处理保存请求时返回true，否则返回false。
 */
bool A_HmiConfig_TakeSaveRequest(A_Hmi_Config_Context *context, Gas_Config *config)
{
    if ((context == NULL) || (config == NULL) || !context->save_pending)
    {
        return false;
    }
    *config = context->edit_config;
    A_HmiConfig_ApplyHiddenParameters(config);
    context->edit_config = *config;
    context->confirm_pending = false;
    context->save_pending = false;
    return true;
}

/*
 * 函数名：A_HmiConfig_ReportResult。
 * 说明：接收气源业务层保存结果，并在成功时用实际生效参数同步编辑缓存。
 * 输入：context为参数模块上下文；result为保存结果；current_config为当前实际生效参数。
 * 输出：无；更新提示文本并安排参数页刷新。
 */
void A_HmiConfig_ReportResult(A_Hmi_Config_Context *context,
                              A_Hmi_Config_Result result,
                              const Gas_Config *current_config)
{
    A_Hmi_Config_Status status;

    if ((context == NULL) || (current_config == NULL))
    {
        return;
    }
    context->confirm_pending = false;
    context->save_pending = false;
    switch (result)
    {
        case A_HMI_CONFIG_RESULT_SUCCESS:
            context->edit_config = *current_config;
            A_HmiConfig_ApplyHiddenParameters(&context->edit_config);
            context->refresh_mask |= A_HMI_CONFIG_FIELD_MASK;
            status = A_HMI_CONFIG_STATUS_SUCCESS;
            break;
        case A_HMI_CONFIG_RESULT_INVALID_RELATION:
            context->edit_config = *current_config;
            A_HmiConfig_ApplyHiddenParameters(&context->edit_config);
            status = A_HMI_CONFIG_STATUS_RELATION_ERROR;
            break;
        case A_HMI_CONFIG_RESULT_STORAGE_FAILED:
            context->edit_config = *current_config;
            A_HmiConfig_ApplyHiddenParameters(&context->edit_config);
            status = A_HMI_CONFIG_STATUS_STORAGE_ERROR;
            break;
        default:
            context->edit_config = *current_config;
            A_HmiConfig_ApplyHiddenParameters(&context->edit_config);
            status = A_HMI_CONFIG_STATUS_RANGE_ERROR;
            break;
    }
    if (result != A_HMI_CONFIG_RESULT_SUCCESS)
    {
        context->refresh_mask |= (context->pending_field < A_HMI_CONFIG_TEXT_COUNT) ?
                                 (uint16_t) (1U << context->pending_field) :
                                 A_HMI_CONFIG_FIELD_MASK;
    }
    A_HmiConfig_SetStatus(context, status);
}

/*
 * 函数名：A_HmiConfig_OpenLogClear。
 * 说明：从密码参数页进入日志清除确认画面，并显示清除前有效日志数量。
 * 输入：context为参数模块上下文；log_count为当前事件和常规日志总数。
 * 输出：参数页会话有效且成功建立确认画面状态时返回true，否则返回false。
 */
bool A_HmiConfig_OpenLogClear(A_Hmi_Config_Context *context, uint16_t log_count)
{
    if ((context == NULL) || (context->hmi == NULL) || !context->active)
    {
        return false;
    }

    context->log_clear_dialog_active = true;
    context->log_clear_request_pending = false;
    context->log_clear_count = log_count;
    context->log_clear_progress = 0U;
    context->log_clear_status = A_HMI_LOG_CLEAR_WAIT_CONFIRM;
    context->log_clear_refresh_mask = A_HMI_LOG_CLEAR_ALL_REFRESH;
    return true;
}

/*
 * 函数名：A_HmiConfig_HandleLogClearButton。
 * 说明：处理日志清除画面的确认和返回按钮；确认只产生一次后台清除请求。
 * 输入：context为参数模块上下文；button_id和value为串口屏按钮事件。
 * 输出：按钮属于日志清除画面时返回true，否则返回false。
 */
bool A_HmiConfig_HandleLogClearButton(A_Hmi_Config_Context *context,
                                      uint16_t button_id,
                                      uint8_t value)
{
    if ((context == NULL) ||
        ((button_id != A_HMI_LOG_CLEAR_CONFIRM_BUTTON_ID) &&
         (button_id != A_HMI_LOG_CLEAR_BACK_BUTTON_ID)))
    {
        return false;
    }
    if (button_id == A_HMI_LOG_CLEAR_CONFIRM_BUTTON_ID)
    {
        if ((value != 0U) && context->active && context->log_clear_dialog_active &&
            (context->log_clear_status == A_HMI_LOG_CLEAR_WAIT_CONFIRM) &&
            !context->log_clear_request_pending)
        {
            context->log_clear_request_pending = true;
            context->log_clear_status = A_HMI_LOG_CLEAR_BUSY;
            context->log_clear_progress = 0U;
            context->log_clear_refresh_mask |= A_HMI_LOG_CLEAR_STATUS_REFRESH;
            A_HmiConfig_SetStatus(context, A_HMI_CONFIG_STATUS_LOG_CLEARING);
        }
        return true;
    }

    if (value != 0U)
    {
        context->log_clear_dialog_active = false;
        // 清除开始后的返回只离开进度页，EEPROM后台任务继续完成，不提供中途取消入口。
    }
    return true;
}

/*
 * 函数名：A_HmiConfig_TakeLogClearRequest。
 * 说明：取出人员已经二次确认的日志物理清除请求，防止同一次触摸重复执行。
 * 输入：context为参数模块上下文。
 * 输出：存在新的清除请求时返回true，否则返回false。
 */
bool A_HmiConfig_TakeLogClearRequest(A_Hmi_Config_Context *context)
{
    if ((context == NULL) || !context->log_clear_request_pending)
    {
        return false;
    }
    context->log_clear_request_pending = false;
    return true;
}

/*
 * 函数名：A_HmiConfig_ReportLogClear。
 * 说明：接收日志模块的清除状态、进度和当前数量，并安排Screen7及参数页提示刷新。
 * 输入：context为参数模块上下文；status为清除状态；progress为0～100进度；log_count为当前日志数量。
 * 输出：无；只有内容发生变化时才加入串口屏刷新队列。
 */
void A_HmiConfig_ReportLogClear(A_Hmi_Config_Context *context,
                                A_Hmi_Log_Clear_Status status,
                                uint8_t progress,
                                uint16_t log_count)
{
    A_Hmi_Config_Status page_status;

    if (context == NULL)
    {
        return;
    }
    if (progress > 100U)
    {
        progress = 100U;
    }
    if (context->log_clear_count != log_count)
    {
        context->log_clear_count = log_count;
        context->log_clear_refresh_mask |= A_HMI_LOG_CLEAR_COUNT_REFRESH;
    }
    if ((context->log_clear_status != status) ||
        (context->log_clear_progress != progress))
    {
        context->log_clear_status = status;
        context->log_clear_progress = progress;
        context->log_clear_refresh_mask |= A_HMI_LOG_CLEAR_STATUS_REFRESH;
    }

    if (status == A_HMI_LOG_CLEAR_BUSY)
    {
        page_status = A_HMI_CONFIG_STATUS_LOG_CLEARING;
    }
    else if (status == A_HMI_LOG_CLEAR_SUCCESS)
    {
        page_status = A_HMI_CONFIG_STATUS_LOG_CLEAR_SUCCESS;
    }
    else if (status == A_HMI_LOG_CLEAR_FAILED)
    {
        page_status = A_HMI_CONFIG_STATUS_LOG_CLEAR_FAILED;
    }
    else
    {
        return;
    }
    if (context->status != (uint8_t) page_status)
    {
        A_HmiConfig_SetStatus(context, page_status);
    }
}

/*
 * 函数名：A_HmiConfig_Task。
 * 说明：按照待刷新位图分时发送一个参数值或确认/结果提示，避免阻塞SCI9。
 * 输入：context为参数模块上下文。
 * 输出：无；每次最多启动一帧异步发送。
 */
void A_HmiConfig_Task(A_Hmi_Config_Context *context)
{
    char text[64];
    const char *fixed_text;
    size_t length = 0U;
    uint16_t page_id;
    uint16_t control_id;
    uint8_t slot;

    if ((context == NULL) || !context->active || (context->hmi == NULL))
    {
        return;
    }
    if (context->refresh_mask == 0U)
    {
        if (!context->log_clear_dialog_active ||
            (context->log_clear_refresh_mask == 0U))
        {
            return;
        }
    }

    if (context->log_clear_dialog_active &&
        (context->log_clear_refresh_mask != 0U))
    {
        if ((context->log_clear_refresh_mask & A_HMI_LOG_CLEAR_COUNT_REFRESH) != 0U)
        {
            control_id = A_HMI_LOG_CLEAR_COUNT_TEXT_ID;
            length = A_HmiConfig_FormatLogClearCount(context->log_clear_count,
                                                      text,
                                                      sizeof(text));
            slot = 0U;
        }
        else
        {
            control_id = A_HMI_LOG_CLEAR_STATUS_TEXT_ID;
            length = A_HmiConfig_FormatLogClearStatus(context->log_clear_status,
                                                       context->log_clear_progress,
                                                       text,
                                                       sizeof(text));
            slot = 1U;
        }
        if ((length != 0U) && A_Hmi_SendText(context->hmi,
                                             A_HMI_LOG_CLEAR_PAGE_ID,
                                             control_id,
                                             text,
                                             length))
        {
            context->log_clear_refresh_mask &= (uint8_t) ~(uint8_t) (1U << slot);
        }
        return;
    }

    if ((context->refresh_mask & A_HMI_CONFIG_DIALOG_MASK) != 0U)
    {
        for (slot = A_HMI_CONFIG_DIALOG_NAME_SLOT;
             slot <= A_HMI_CONFIG_DIALOG_INFO_SLOT;
             ++slot)
        {
            if ((context->refresh_mask & (uint16_t) (1U << slot)) != 0U)
            {
                break;
            }
        }
    }
    else
    {
        for (slot = 0U; slot <= A_HMI_CONFIG_STATUS_SLOT; ++slot)
        {
            if ((context->refresh_mask & (uint16_t) (1U << slot)) != 0U)
            {
                break;
            }
        }
    }

    page_id = A_HMI_CONFIG_PAGE_ID;
    if (slot < A_HMI_CONFIG_TEXT_COUNT)
    {
        control_id = (uint16_t) (A_HMI_CONFIG_TEXT_BASE + slot);
        length = A_HmiConfig_FormatField(&context->edit_config, slot, text, sizeof(text));
        fixed_text = text;
    }
    else if (slot == A_HMI_CONFIG_STATUS_SLOT)
    {
        control_id = A_HMI_CONFIG_STATUS_TEXT_ID;
        fixed_text = A_HmiConfig_GetStatusText(context->status, &length);
    }
    else
    {
        page_id = A_HMI_CONFIG_DIALOG_PAGE_ID;
        switch (slot)
        {
            case A_HMI_CONFIG_DIALOG_NAME_SLOT:
                control_id = A_HMI_CONFIG_DIALOG_NAME_TEXT_ID;
                fixed_text = A_HmiConfig_GetFieldName(context->pending_field, &length);
                break;
            case A_HMI_CONFIG_DIALOG_OLD_SLOT:
                control_id = A_HMI_CONFIG_DIALOG_OLD_TEXT_ID;
                fixed_text = context->pending_old_text;
                length = strlen(fixed_text);
                break;
            case A_HMI_CONFIG_DIALOG_NEW_SLOT:
                control_id = A_HMI_CONFIG_DIALOG_NEW_TEXT_ID;
                fixed_text = context->pending_new_text;
                length = strlen(fixed_text);
                break;
            default:
                control_id = A_HMI_CONFIG_DIALOG_INFO_TEXT_ID;
                fixed_text = A_HmiConfig_GetDialogInfoText(context->status, &length);
                break;
        }
        if (length == 0U)
        {
            fixed_text = "--";
            length = 2U;
        }
    }

    if ((length != 0U) && A_Hmi_SendText(context->hmi,
                                          page_id,
                                          control_id,
                                          fixed_text,
                                          length))
    {
        context->refresh_mask &= (uint16_t) ~(uint16_t) (1U << slot);
    }
}

/*
 * 函数名：A_HmiConfig_IsActive。
 * 说明：查询参数编辑页面是否处于活动会话，用于暂停监控画面后台刷新。
 * 输入：context为只读参数模块上下文。
 * 输出：参数页活动时返回true，否则返回false。
 */
bool A_HmiConfig_IsActive(const A_Hmi_Config_Context *context)
{
    return ((context != NULL) && context->active);
}
