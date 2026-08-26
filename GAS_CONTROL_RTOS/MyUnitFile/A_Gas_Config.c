#include "A_Gas_Config.h"

#include <stddef.h>
#include <string.h>

#define A_GAS_CONFIG_STORAGE_VALUE_COUNT (13U) // EEPROM V3记录保存的全部参数数量。
#define A_GAS_CONFIG_RECORD_SIZE          (36U) // V3记录总长度，小于AT24C256的64字节单页容量。
#define A_GAS_CONFIG_PAYLOAD_OFFSET       (8U)  // 参数区在记录中的起始字节偏移。
#define A_GAS_CONFIG_PAYLOAD_SIZE         (26U) // 13个无符号16位参数占用的字节数。
#define A_GAS_CONFIG_LEGACY_VERSION       (0x0002U) // 已发布的10项参数记录版本，用于兼容迁移。
#define A_GAS_CONFIG_LEGACY_PAYLOAD_SIZE  (20U) // V2记录的10项参数区长度。
#define A_GAS_CONFIG_LEGACY_RECORD_SIZE   (30U) // V2记录的总长度。

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
    uint16_t crc = 0xFFFFU;
    size_t index;
    uint8_t bit;

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
 * 说明：保留原有10项顺序并在末尾追加3项HMI安全参数，编码EEPROM V3参数区。
 * 输入：config 为只读运行参数；payload 为26字节输出缓存。
 * 输出：无；编码后的参数写入 payload。
 */
static void A_GasConfig_EncodePayload(const Gas_Config *config, uint8_t *payload)
{
    uint16_t values[A_GAS_CONFIG_STORAGE_VALUE_COUNT];
    uint8_t index;

    values[0] = A_GasConfig_PressureToRaw(config->switch_pressure_mpa);
    values[1] = A_GasConfig_PressureToRaw(config->switch_release_mpa);
    values[2] = (uint16_t) config->valve_pull_in_time_ms;
    values[3] = A_GasConfig_PressureToRaw(config->ready_min_pressure_mpa);
    values[4] = A_GasConfig_PressureToRaw(config->pressure_max_mpa);
    values[5] = (uint16_t) config->low_confirm_time_ms;
    values[6] = config->low_confirm_samples;
    values[7] = (uint16_t) config->valve_close_wait_ms;
    values[8] = (uint16_t) config->valve_open_wait_ms;
    values[9] = (uint16_t) config->pressure_fresh_ms;
    values[10] = A_GasConfig_PressureToRaw(config->low_warning_pressure_mpa);
    values[11] = (uint16_t) config->manual_exhaust_time_ms;
    values[12] = (uint16_t) config->test_valve_max_time_ms;
    // 前10项顺序保持V2兼容，新增3项仅写入EEPROM，不增加外部CAN或Modbus地址。

    for (index = 0U; index < A_GAS_CONFIG_STORAGE_VALUE_COUNT; ++index)
    {
        A_GasConfig_WriteU16(&payload[index * 2U], values[index]);
    }
}

/*
 * 函数名：A_GasConfig_DecodePayload。
 * 说明：把EEPROM参数区的前10项公共参数转换为运行参数结构体，供V2和V3共同使用。
 * 输入：payload 为至少20字节的只读参数区；config 为运行参数输出指针。
 * 输出：无；解码后的参数写入 config。
 */
static void A_GasConfig_DecodeCommonPayload(const uint8_t *payload, Gas_Config *config)
{
    uint16_t values[A_GAS_CONFIG_REGISTER_COUNT];
    uint8_t index;

    for (index = 0U; index < A_GAS_CONFIG_REGISTER_COUNT; ++index)
    {
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
 * 函数名：A_GasConfig_DecodeV3Payload。
 * 说明：先解码与V2兼容的10项参数，再解码V3追加的三项HMI安全参数。
 * 输入：payload 为26字节V3参数区；config 为运行参数输出指针。
 * 输出：无；13项解码结果写入config。
 */
static void A_GasConfig_DecodeV3Payload(const uint8_t *payload, Gas_Config *config)
{
    A_GasConfig_DecodeCommonPayload(payload, config);
    config->low_warning_pressure_mpa =
        (float) A_GasConfig_ReadU16(&payload[20]) / GAS_CONFIG_PRESSURE_SCALE;
    config->manual_exhaust_time_ms = A_GasConfig_ReadU16(&payload[22]);
    config->test_valve_max_time_ms = A_GasConfig_ReadU16(&payload[24]);
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
    uint32_t minimum_fresh_ms = GAS_SENSOR_POLL_INTERVAL_MS * GAS_PRESSURE_SENSOR_COUNT;
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
        (config->test_valve_max_time_ms > GAS_TEST_VALVE_MAX_TIME_MAX_MS))
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
 * 说明：读取带版本和CRC16的参数记录；V2有效记录自动补入三项默认值并迁移为V3。
 * 输入：storage 为已经初始化的存储上下文；config 为运行参数输出指针。
 * 输出：记录标识、版本、CRC 和参数均有效时返回 true，否则返回 false。
 */
bool A_GasConfig_Load(A_Storage_Context *storage, Gas_Config *config)
{
    uint8_t header[A_GAS_CONFIG_PAYLOAD_OFFSET];
    uint8_t record[A_GAS_CONFIG_RECORD_SIZE];
    uint16_t version;
    uint16_t payload_size;
    size_t record_size;
    uint16_t stored_crc;

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

    version = A_GasConfig_ReadU16(&header[4]);
    payload_size = A_GasConfig_ReadU16(&header[6]);
    if ((version == A_GAS_CONFIG_RECORD_VERSION) &&
        (payload_size == A_GAS_CONFIG_PAYLOAD_SIZE))
    {
        record_size = A_GAS_CONFIG_RECORD_SIZE;
    }
    else if ((version == A_GAS_CONFIG_LEGACY_VERSION) &&
             (payload_size == A_GAS_CONFIG_LEGACY_PAYLOAD_SIZE))
    {
        record_size = A_GAS_CONFIG_LEGACY_RECORD_SIZE;
    }
    else
    {
        return false;
    }

    if (!A_Storage_Read(storage, A_GAS_CONFIG_STORAGE_ADDRESS, record, record_size))
    {
        return false;
    }

    stored_crc = A_GasConfig_ReadU16(&record[record_size - 2U]);
    if (stored_crc != A_GasConfig_Crc16(record, record_size - 2U))
    {
        // CRC 校验在字段解码前完成，掉电写入或存储位翻转的数据不会进入运行配置。
        return false;
    }

    if (version == A_GAS_CONFIG_RECORD_VERSION)
    {
        A_GasConfig_DecodeV3Payload(&record[A_GAS_CONFIG_PAYLOAD_OFFSET], config);
    }
    else
    {
        A_GasConfig_LoadDefaults(config);
        A_GasConfig_DecodeCommonPayload(&record[A_GAS_CONFIG_PAYLOAD_OFFSET], config);
        // V2没有新增三项，沿用已确认默认值；校验通过后尽力原址升级，不因迁移写失败丢失旧参数。
    }

    if (A_GasConfig_Validate(config) != A_GAS_CONFIG_VALID)
    {
        return false;
    }
    if (version == A_GAS_CONFIG_LEGACY_VERSION)
    {
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
    uint8_t record[A_GAS_CONFIG_RECORD_SIZE];
    uint8_t verify[A_GAS_CONFIG_RECORD_SIZE];
    uint16_t crc;

    if ((storage == NULL) || (config == NULL) ||
        (A_GasConfig_Validate(config) != A_GAS_CONFIG_VALID))
    {
        return false;
    }

    (void) memset(record, 0xFF, sizeof(record));
    record[0] = 'G';
    record[1] = 'C';
    record[2] = 'F';
    record[3] = 'G';
    A_GasConfig_WriteU16(&record[4], A_GAS_CONFIG_RECORD_VERSION);
    A_GasConfig_WriteU16(&record[6], A_GAS_CONFIG_PAYLOAD_SIZE);
    A_GasConfig_EncodePayload(config, &record[A_GAS_CONFIG_PAYLOAD_OFFSET]);
    crc = A_GasConfig_Crc16(record, A_GAS_CONFIG_RECORD_SIZE - 2U);
    A_GasConfig_WriteU16(&record[A_GAS_CONFIG_RECORD_SIZE - 2U], crc);
    // 参数记录整体写入后立即读回逐字节核对，只有完整一致的记录才视为保存成功。

    if (!A_Storage_Write(storage, A_GAS_CONFIG_STORAGE_ADDRESS, record, sizeof(record)) ||
        !A_Storage_Read(storage, A_GAS_CONFIG_STORAGE_ADDRESS, verify, sizeof(verify)))
    {
        return false;
    }

    return (memcmp(record, verify, sizeof(record)) == 0);
}
