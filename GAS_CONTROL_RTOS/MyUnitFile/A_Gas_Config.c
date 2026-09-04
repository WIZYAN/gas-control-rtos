/*
 * Version: v1.14
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现运行参数默认值、合法性校验、EEPROM保存和版本迁移。
 */

#include "A_Gas_Config.h"

#include <stddef.h>
#include <string.h>

#define A_GAS_CONFIG_STORAGE_VALUE_COUNT (13U) // EEPROM V4记录保存的全部参数数量。
#define A_GAS_CONFIG_RECORD_SIZE          (36U) // V4记录仍为36字节，不移动后续通讯方式记录。
#define A_GAS_CONFIG_PAYLOAD_OFFSET       (8U)  // 参数区在记录中的起始字节偏移。
#define A_GAS_CONFIG_PAYLOAD_SIZE         (26U) // 13个无符号16位参数占用的字节数。
#define A_GAS_CONFIG_V3_VERSION           (0x0003U) // V3末项以5000～60000毫秒保存。
#define A_GAS_CONFIG_V2_VERSION           (0x0002U) // 已发布的10项参数记录版本。
#define A_GAS_CONFIG_V2_PAYLOAD_SIZE      (20U) // V2记录的10项参数区长度。
#define A_GAS_CONFIG_V2_RECORD_SIZE       (30U) // V2记录的总长度。

/*
 * 函数名：A_GasConfig_WriteU16。
 * 说明：按高字节在前的固定顺序把 16 位数写入参数记录。
 * 输入：data 为两字节输出位置；value 为待编码数值。
 * 输出：无；编码结果写入 data。
 */
static void A_GasConfig_WriteU16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) (value >> 8U);
    data[1] = (uint8_t) value;
}

/*
 * 函数名：A_GasConfig_ReadU16。
 * 说明：按高字节在前的固定顺序从参数记录读取 16 位数。
 * 输入：data 为包含两个字节的只读位置。
 * 输出：返回解码得到的无符号 16 位数。
 */
static uint16_t A_GasConfig_ReadU16(const uint8_t *data)
{
    return (uint16_t) (((uint16_t) data[0] << 8U) | data[1]);
}

/*
 * 函数名：A_GasConfig_Crc16。
 * 说明：计算 EEPROM 参数记录使用的 Modbus 多项式 CRC16。
 * 输入：data 为只读数据；length 为参与计算的字节数。
 * 输出：返回 16 位 CRC 校验值。
 */
static uint16_t A_GasConfig_Crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU; // 当前作用域变量，用于保存CRC校验值。
    size_t index; // 当前作用域变量，用于保存遍历索引。
    uint8_t bit; // 当前作用域变量，用于保存位掩码。

    for (index = 0U; index < length; ++index)
    {
        crc = (uint16_t) (crc ^ data[index]);
        for (bit = 0U; bit < 8U; ++bit)
        {
            crc = ((crc & 1U) != 0U) ?
                  (uint16_t) ((crc >> 1U) ^ 0xA001U) : (uint16_t) (crc >> 1U);
        }
    }
    return crc;
}

/*
 * 函数名：A_GasConfig_PressureToRaw。
 * 说明：把 MPa 压力转换为乘以 1000 的无符号 16 位定点数。
 * 输入：pressure_mpa 为待编码压力，单位 MPa。
 * 输出：返回四舍五入后的寄存器原始值。
 */
static uint16_t A_GasConfig_PressureToRaw(float pressure_mpa)
{
    return (uint16_t) ((pressure_mpa * GAS_CONFIG_PRESSURE_SCALE) + 0.5F);
}

/*
 * 函数名：A_GasConfig_EncodePayload。
 * 说明：保留原有13项顺序，V4末项改为测试阀最长开启分钟数。
 * 输入：config 为只读运行参数；payload 为26字节输出缓存。
 * 输出：无；编码后的参数写入 payload。
 */
static void A_GasConfig_EncodePayload(const Gas_Config *config, uint8_t *payload)
{
    uint16_t values[A_GAS_CONFIG_STORAGE_VALUE_COUNT]; // 当前作用域变量，用于保存当前处理值数组。
    uint8_t index; // 当前作用域变量，用于保存遍历索引。

    //把 MPa 压力转换为乘以 1000 的无符号 16 位定点数
    values[0] = A_GasConfig_PressureToRaw(config->switch_pressure_mpa);
    //把 MPa 压力转换为乘以 1000 的无符号 16 位定点数
    values[1] = A_GasConfig_PressureToRaw(config->switch_release_mpa);
    values[2] = (uint16_t) config->valve_pull_in_time_ms;
    //把 MPa 压力转换为乘以 1000 的无符号 16 位定点数
    values[3] = A_GasConfig_PressureToRaw(config->ready_min_pressure_mpa);
    //把 MPa 压力转换为乘以 1000 的无符号 16 位定点数
    values[4] = A_GasConfig_PressureToRaw(config->pressure_max_mpa);
    values[5] = (uint16_t) config->low_confirm_time_ms;
    values[6] = config->low_confirm_samples;
    values[7] = (uint16_t) config->valve_close_wait_ms;
    values[8] = (uint16_t) config->valve_open_wait_ms;
    values[9] = (uint16_t) config->pressure_fresh_ms;
    //把 MPa 压力转换为乘以 1000 的无符号 16 位定点数
    values[10] = A_GasConfig_PressureToRaw(config->low_warning_pressure_mpa);
    values[11] = (uint16_t) config->manual_exhaust_time_ms;
    values[12] = (uint16_t) (config->test_valve_max_time_ms /
                             GAS_MILLISECONDS_PER_MINUTE);
    // 前10项顺序保持V2兼容，新增3项仅写入EEPROM，不增加外部CAN或Modbus地址。

    for (index = 0U; index < A_GAS_CONFIG_STORAGE_VALUE_COUNT; ++index)
    {
        //按高字节在前的固定顺序把 16 位数写入参数记录
        A_GasConfig_WriteU16(&payload[index * 2U], values[index]);
    }
}

/*
 * 函数名：A_GasConfig_DecodePayload。
 * 说明：把EEPROM参数区的前10项公共参数转换为运行参数结构体，供V2、V3和V4共同使用。
 * 输入：payload 为至少20字节的只读参数区；config 为运行参数输出指针。
 * 输出：无；解码后的参数写入 config。
 */
static void A_GasConfig_DecodeCommonPayload(const uint8_t *payload, Gas_Config *config)
{
    uint16_t values[A_GAS_CONFIG_REGISTER_COUNT]; // 当前作用域变量，用于保存当前处理值数组。
    uint8_t index; // 当前作用域变量，用于保存遍历索引。

    for (index = 0U; index < A_GAS_CONFIG_REGISTER_COUNT; ++index)
    {
        //按高字节在前的固定顺序从参数记录读取 16 位数
        values[index] = A_GasConfig_ReadU16(&payload[index * 2U]);
    }

    config->switch_pressure_mpa = (float) values[0] / GAS_CONFIG_PRESSURE_SCALE;
    config->switch_release_mpa = (float) values[1] / GAS_CONFIG_PRESSURE_SCALE;
    config->valve_pull_in_time_ms = values[2];
    config->ready_min_pressure_mpa = (float) values[3] / GAS_CONFIG_PRESSURE_SCALE;
    config->pressure_max_mpa = (float) values[4] / GAS_CONFIG_PRESSURE_SCALE;
    config->low_confirm_time_ms = values[5];
    config->low_confirm_samples = values[6];
    config->valve_close_wait_ms = values[7];
    config->valve_open_wait_ms = values[8];
    config->pressure_fresh_ms = values[9];
}

/*
 * 函数名：A_GasConfig_DecodeV4Payload。
 * 说明：解码V4参数，末项分钟数转换为运行时毫秒数。
 * 输入：payload 为26字节V4参数区；config 为运行参数输出指针。
 * 输出：无；13项解码结果写入config。
 */
static void A_GasConfig_DecodeV4Payload(const uint8_t *payload, Gas_Config *config)
{
    //解析配置记录的公共字段
    A_GasConfig_DecodeCommonPayload(payload, config);
    //按高字节在前的固定顺序从参数记录读取 16 位数
    config->low_warning_pressure_mpa =
        (float) A_GasConfig_ReadU16(&payload[20]) / GAS_CONFIG_PRESSURE_SCALE;
    //按高字节在前的固定顺序从参数记录读取 16 位数
    config->manual_exhaust_time_ms = A_GasConfig_ReadU16(&payload[22]);
    //按高字节在前的固定顺序从参数记录读取 16 位数
    config->test_valve_max_time_ms =
        (uint32_t) A_GasConfig_ReadU16(&payload[24]) * GAS_MILLISECONDS_PER_MINUTE;
}

/*
 * 函数名：A_GasConfig_DecodeV3Payload。
 * 说明：解码旧V3参数，并把原界面秒数按相同数值迁移为分钟数。
 * 输入：payload 为26字节V3参数区；config 为运行参数输出指针。
 * 输出：旧测试阀字段有效时返回true；否则返回false。
 */
static bool A_GasConfig_DecodeV3Payload(const uint8_t *payload, Gas_Config *config)
{
    uint16_t legacy_test_time_ms; // 当前作用域变量，用于保存毫秒时间值。
    uint32_t migrated_minutes; // 当前作用域变量，用于保存日期时间字段。

    //解析配置记录的公共字段
    A_GasConfig_DecodeCommonPayload(payload, config);
    //按高字节在前的固定顺序从参数记录读取 16 位数
    config->low_warning_pressure_mpa =
        (float) A_GasConfig_ReadU16(&payload[20]) / GAS_CONFIG_PRESSURE_SCALE;
    //按高字节在前的固定顺序从参数记录读取 16 位数
    config->manual_exhaust_time_ms = A_GasConfig_ReadU16(&payload[22]);
    legacy_test_time_ms = A_GasConfig_ReadU16(&payload[24]); // 当前作用域变量，用于保存毫秒时间值。
    if ((legacy_test_time_ms < 5000U) || (legacy_test_time_ms > 60000U))
    {
        return false;
    }

    migrated_minutes = ((uint32_t) legacy_test_time_ms + 500U) / 1000U;
    config->test_valve_max_time_ms = migrated_minutes * GAS_MILLISECONDS_PER_MINUTE;
    // 正式界面只会产生整秒值；四舍五入兼容可能由旧调试程序保存的零散毫秒值。
    return true;
}

/*
 * 函数名：A_GasConfig_LoadDefaults。
 * 说明：把编译期默认值写入一个运行参数结构体。
 * 输入：config 为待初始化的运行参数输出指针。
 * 输出：无；参数有效时通过 config 输出全部默认值。
 */
void A_GasConfig_LoadDefaults(Gas_Config *config)
{
    if (config == NULL)
    {
        return;
    }

    config->switch_pressure_mpa = GAS_DEFAULT_SWITCH_PRESSURE_MPA;
    config->switch_release_mpa = GAS_DEFAULT_SWITCH_RELEASE_MPA;
    config->valve_pull_in_time_ms = GAS_DEFAULT_VALVE_PULL_IN_TIME_MS;
    config->ready_min_pressure_mpa = GAS_DEFAULT_READY_MIN_PRESSURE_MPA;
    config->pressure_max_mpa = GAS_DEFAULT_PRESSURE_MAX_MPA;
    config->low_confirm_time_ms = GAS_DEFAULT_LOW_CONFIRM_TIME_MS;
    config->low_confirm_samples = GAS_DEFAULT_LOW_CONFIRM_SAMPLES;
    config->valve_close_wait_ms = GAS_DEFAULT_VALVE_CLOSE_WAIT_MS;
    config->valve_open_wait_ms = GAS_DEFAULT_VALVE_OPEN_WAIT_MS;
    config->pressure_fresh_ms = GAS_DEFAULT_PRESSURE_FRESH_MS;
    config->low_warning_pressure_mpa = GAS_DEFAULT_LOW_WARNING_PRESSURE_MPA;
    config->manual_exhaust_time_ms = GAS_DEFAULT_MANUAL_EXHAUST_TIME_MS;
    config->test_valve_max_time_ms = GAS_DEFAULT_TEST_VALVE_MAX_TIME_MS;
}

/*
 * 函数名：A_GasConfig_Validate。
 * 说明：检查运行参数的单项范围和压力阈值关系。
 * 输入：config 为只读运行参数指针。
 * 输出：返回参数有效、范围错误或关系错误的校验结果。
 */
A_Gas_Config_Validation A_GasConfig_Validate(const Gas_Config *config)
{
    uint32_t minimum_fresh_ms = GAS_SENSOR_POLL_INTERVAL_MS * GAS_PRESSURE_SENSOR_COUNT; // 当前作用域变量，用于保存毫秒时间值。
    // 新鲜度下限至少覆盖 1～7 号传感器的一轮轮询，避免正常轮询中的样本被提前判旧。

    if ((config == NULL) ||
        !(config->switch_pressure_mpa > 0.0F) ||
        !(config->switch_release_mpa > 0.0F) ||
        !(config->ready_min_pressure_mpa > 0.0F) ||
        !(config->pressure_max_mpa > 0.0F) ||
        !(config->low_warning_pressure_mpa > 0.0F) ||
        (config->switch_pressure_mpa > GAS_CONFIG_PRESSURE_MAX_ENCODED_MPA) ||
        (config->switch_release_mpa > GAS_CONFIG_PRESSURE_MAX_ENCODED_MPA) ||
        (config->ready_min_pressure_mpa > GAS_CONFIG_PRESSURE_MAX_ENCODED_MPA) ||
        (config->pressure_max_mpa > GAS_CONFIG_PRESSURE_MAX_ENCODED_MPA) ||
        (config->low_warning_pressure_mpa < GAS_LOW_WARNING_PRESSURE_MIN_MPA) ||
        (config->low_warning_pressure_mpa > GAS_LOW_WARNING_PRESSURE_MAX_MPA) ||
        (config->valve_pull_in_time_ms == 0U) ||
        (config->valve_pull_in_time_ms > GAS_CONFIG_TIME_MAX_MS) ||
        (config->low_confirm_time_ms > GAS_CONFIG_TIME_MAX_MS) ||
        (config->low_confirm_samples == 0U) ||
        (config->low_confirm_samples > GAS_CONFIG_SAMPLE_MAX_COUNT) ||
        (config->valve_close_wait_ms > GAS_CONFIG_TIME_MAX_MS) ||
        (config->valve_open_wait_ms > GAS_CONFIG_TIME_MAX_MS) ||
        (config->pressure_fresh_ms < minimum_fresh_ms) ||
        (config->pressure_fresh_ms > GAS_CONFIG_TIME_MAX_MS) ||
        (config->manual_exhaust_time_ms < GAS_MANUAL_EXHAUST_TIME_MIN_MS) ||
        (config->manual_exhaust_time_ms > GAS_MANUAL_EXHAUST_TIME_MAX_MS) ||
        (config->test_valve_max_time_ms < GAS_TEST_VALVE_MAX_TIME_MIN_MS) ||
        (config->test_valve_max_time_ms > GAS_TEST_VALVE_MAX_TIME_MAX_MS) ||
        ((config->test_valve_max_time_ms % GAS_MILLISECONDS_PER_MINUTE) != 0U))
    {
        return A_GAS_CONFIG_INVALID_RANGE;
    }

    if (!((config->switch_pressure_mpa < config->switch_release_mpa) &&
          (config->switch_release_mpa <= config->ready_min_pressure_mpa) &&
          (config->ready_min_pressure_mpa <= config->low_warning_pressure_mpa) &&
          (config->low_warning_pressure_mpa <= config->pressure_max_mpa)))
    {
        // 切换、回差、待用、低压警告和物理上限必须依次递增，防止状态机在矛盾阈值间振荡。
        return A_GAS_CONFIG_INVALID_RELATION;
    }

    return A_GAS_CONFIG_VALID;
}

/*
 * 函数名：A_GasConfig_Load。
 * 说明：读取带版本和CRC16的参数记录；V2、V3有效记录自动迁移为V4。
 * 输入：storage 为已经初始化的存储上下文；config 为运行参数输出指针。
 * 输出：记录标识、版本、CRC 和参数均有效时返回 true，否则返回 false。
 */
bool A_GasConfig_Load(A_Storage_Context *storage, Gas_Config *config)
{
    uint8_t header[A_GAS_CONFIG_PAYLOAD_OFFSET]; // 当前作用域变量，用于保存队列头位置数组。
    uint8_t record[A_GAS_CONFIG_RECORD_SIZE]; // 当前作用域变量，用于保存日志或配置记录缓冲区。
    uint16_t version; // 当前作用域变量，用于保存当前处理数据。
    uint16_t payload_size; // 当前作用域变量，用于保存当前处理数据。
    size_t record_size; // 当前作用域变量，用于保存日志或配置记录缓冲区。
    uint16_t stored_crc; // 当前作用域变量，用于保存CRC校验值。

    //从 EEPROM 指定地址读取一段原始数据
    if ((storage == NULL) || (config == NULL) ||
        !A_Storage_Read(storage, A_GAS_CONFIG_STORAGE_ADDRESS, header, sizeof(header)))
    {
        return false;
    }

    if ((header[0] != 'G') || (header[1] != 'C') ||
        (header[2] != 'F') || (header[3] != 'G'))
    {
        // 固定标识错误时不继续读取负载，防止把未初始化EEPROM误作参数记录。
        return false;
    }

    //按高字节在前的固定顺序从参数记录读取 16 位数
    version = A_GasConfig_ReadU16(&header[4]);
    //按高字节在前的固定顺序从参数记录读取 16 位数
    payload_size = A_GasConfig_ReadU16(&header[6]);
    if ((version == A_GAS_CONFIG_RECORD_VERSION) &&
        (payload_size == A_GAS_CONFIG_PAYLOAD_SIZE))
    {
        record_size = A_GAS_CONFIG_RECORD_SIZE;
    }
    else if ((version == A_GAS_CONFIG_V3_VERSION) &&
             (payload_size == A_GAS_CONFIG_PAYLOAD_SIZE))
    {
        record_size = A_GAS_CONFIG_RECORD_SIZE;
    }
    else if ((version == A_GAS_CONFIG_V2_VERSION) &&
             (payload_size == A_GAS_CONFIG_V2_PAYLOAD_SIZE))
    {
        record_size = A_GAS_CONFIG_V2_RECORD_SIZE;
    }
    else
    {
        return false;
    }

    //从 EEPROM 指定地址读取一段原始数据
    if (!A_Storage_Read(storage, A_GAS_CONFIG_STORAGE_ADDRESS, record, record_size))
    {
        return false;
    }

    //按高字节在前的固定顺序从参数记录读取 16 位数
    stored_crc = A_GasConfig_ReadU16(&record[record_size - 2U]);
    //计算 EEPROM 参数记录使用的 Modbus 多项式 CRC16
    if (stored_crc != A_GasConfig_Crc16(record, record_size - 2U))
    {
        // CRC 校验在字段解码前完成，掉电写入或存储位翻转的数据不会进入运行配置。
        return false;
    }

    if (version == A_GAS_CONFIG_RECORD_VERSION)
    {
        //解码V4参数
        A_GasConfig_DecodeV4Payload(&record[A_GAS_CONFIG_PAYLOAD_OFFSET], config);
    }
    else if (version == A_GAS_CONFIG_V3_VERSION)
    {
        //解码旧V3参数
        if (!A_GasConfig_DecodeV3Payload(&record[A_GAS_CONFIG_PAYLOAD_OFFSET], config))
        {
            return false;
        }
    }
    else
    {
        //把编译期默认值写入一个运行参数结构体
        A_GasConfig_LoadDefaults(config);
        //解析配置记录的公共字段
        A_GasConfig_DecodeCommonPayload(&record[A_GAS_CONFIG_PAYLOAD_OFFSET], config);
        // V2没有新增三项，沿用V1.11默认值；校验通过后尽力原址升级。
    }

    //检查运行参数的单项范围和压力阈值关系
    if (A_GasConfig_Validate(config) != A_GAS_CONFIG_VALID)
    {
        return false;
    }
    if (version != A_GAS_CONFIG_RECORD_VERSION)
    {
        //把运行参数编码为单页记录并写入 AT24C256
        (void) A_GasConfig_Save(storage, config);
    }
    return true;
}

/*
 * 函数名：A_GasConfig_Save。
 * 说明：把运行参数编码为单页记录并写入 AT24C256，随后读回校验。
 * 输入：storage 为已经初始化的存储上下文；config 为待保存的只读运行参数。
 * 输出：参数有效且写入、读回全部成功时返回 true，否则返回 false。
 */
bool A_GasConfig_Save(A_Storage_Context *storage, const Gas_Config *config)
{
    uint8_t record[A_GAS_CONFIG_RECORD_SIZE]; // 当前作用域变量，用于保存日志或配置记录缓冲区。
    uint8_t verify[A_GAS_CONFIG_RECORD_SIZE]; // 当前作用域变量，用于保存读回校验缓冲区。
    uint16_t crc; // 当前作用域变量，用于保存CRC校验值。

    //检查运行参数的单项范围和压力阈值关系
    if ((storage == NULL) || (config == NULL) ||
        (A_GasConfig_Validate(config) != A_GAS_CONFIG_VALID))
    {
        return false;
    }

    //初始化或清空内存数据
    (void) memset(record, 0xFF, sizeof(record));
    record[0] = 'G';
    record[1] = 'C';
    record[2] = 'F';
    record[3] = 'G';
    //按高字节在前的固定顺序把 16 位数写入参数记录
    A_GasConfig_WriteU16(&record[4], A_GAS_CONFIG_RECORD_VERSION);
    //按高字节在前的固定顺序把 16 位数写入参数记录
    A_GasConfig_WriteU16(&record[6], A_GAS_CONFIG_PAYLOAD_SIZE);
    //保留原有13项顺序
    A_GasConfig_EncodePayload(config, &record[A_GAS_CONFIG_PAYLOAD_OFFSET]);
    //计算 EEPROM 参数记录使用的 Modbus 多项式 CRC16
    crc = A_GasConfig_Crc16(record, A_GAS_CONFIG_RECORD_SIZE - 2U);
    //按高字节在前的固定顺序把 16 位数写入参数记录
    A_GasConfig_WriteU16(&record[A_GAS_CONFIG_RECORD_SIZE - 2U], crc);
    // 参数记录整体写入后立即读回逐字节核对，只有完整一致的记录才视为保存成功。

    if (!A_Storage_Write(storage, A_GAS_CONFIG_STORAGE_ADDRESS, record, sizeof(record)) ||
        !A_Storage_Read(storage, A_GAS_CONFIG_STORAGE_ADDRESS, verify, sizeof(verify)))
    {
        return false;
    }

    //比较两段内存数据
    return (memcmp(record, verify, sizeof(record)) == 0);
}
