#include "A_Gas_Log.h"

#include <stddef.h>
#include <string.h>

#define A_GAS_LOG_HEADER_MAGIC_0          ('G')        // 管理头标识第1字节。
#define A_GAS_LOG_HEADER_MAGIC_1          ('L')        // 管理头标识第2字节。
#define A_GAS_LOG_HEADER_MAGIC_2          ('H')        // 管理头标识第3字节。
#define A_GAS_LOG_HEADER_MAGIC_3          ('D')        // 管理头标识第4字节。
#define A_GAS_LOG_LAST_REGULAR_KEY_NONE   (0xFFFFFFFFUL) // 尚未保存常规记录时使用的无效时段键。

// 从管理头解码得到的候选状态，仅在标识、版本、范围和CRC均正确时使用。
typedef struct
{
    uint32_t generation; // 管理头更新代数。
    uint32_t next_sequence; // 下一条日志流水号。
    uint32_t last_regular_key; // 最近常规记录时段键。
    uint16_t write_index; // 下一写入物理槽位。
    uint16_t valid_count; // 当前有效记录数量。
    bool valid; // 当前管理头候选是否通过完整校验。
} A_Gas_Log_Header_Candidate;

/*
 * 函数名：A_GasLog_WriteU16。
 * 说明：按照高字节在前的固定格式编码一个无符号16位整数。
 * 输入：data 为两字节输出位置；value 为待编码数值。
 * 输出：无；编码结果写入data。
 */
static void A_GasLog_WriteU16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) (value >> 8U);
    data[1] = (uint8_t) value;
}

/*
 * 函数名：A_GasLog_ReadU16。
 * 说明：按照高字节在前的固定格式解码一个无符号16位整数。
 * 输入：data 为包含两个字节的只读缓存。
 * 输出：返回解码后的无符号16位数值。
 */
static uint16_t A_GasLog_ReadU16(const uint8_t *data)
{
    return (uint16_t) (((uint16_t) data[0] << 8U) | data[1]);
}

/*
 * 函数名：A_GasLog_WriteU32。
 * 说明：按照高字节在前的固定格式编码一个无符号32位整数。
 * 输入：data 为四字节输出位置；value 为待编码数值。
 * 输出：无；编码结果写入data。
 */
static void A_GasLog_WriteU32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t) (value >> 24U);
    data[1] = (uint8_t) (value >> 16U);
    data[2] = (uint8_t) (value >> 8U);
    data[3] = (uint8_t) value;
}

/*
 * 函数名：A_GasLog_ReadU32。
 * 说明：按照高字节在前的固定格式解码一个无符号32位整数。
 * 输入：data 为包含四个字节的只读缓存。
 * 输出：返回解码后的无符号32位数值。
 */
static uint32_t A_GasLog_ReadU32(const uint8_t *data)
{
    return ((uint32_t) data[0] << 24U) |
           ((uint32_t) data[1] << 16U) |
           ((uint32_t) data[2] << 8U) |
           (uint32_t) data[3];
}

/*
 * 函数名：A_GasLog_Crc16。
 * 说明：使用Modbus多项式计算日志管理头和记录的CRC16校验值。
 * 输入：data 为只读数据缓存；length 为参与计算的字节数。
 * 输出：返回计算得到的CRC16数值。
 */
static uint16_t A_GasLog_Crc16(const uint8_t *data, size_t length)
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
 * 函数名：A_GasLog_HeaderAddress。
 * 说明：把管理头副本编号转换为对应的EEPROM字节地址。
 * 输入：copy 为副本编号，0表示A，其他值表示B。
 * 输出：返回对应管理头的EEPROM起始地址。
 */
static uint16_t A_GasLog_HeaderAddress(uint8_t copy)
{
    return (copy == 0U) ? A_GAS_LOG_HEADER_A_ADDRESS : A_GAS_LOG_HEADER_B_ADDRESS;
}

/*
 * 函数名：A_GasLog_RecordAddress。
 * 说明：把日志物理槽位索引转换为EEPROM中的32字节记录起始地址。
 * 输入：physical_index 为范围0～1017的物理槽位索引。
 * 输出：返回该槽位对应的EEPROM起始地址。
 */
static uint16_t A_GasLog_RecordAddress(uint16_t physical_index)
{
    return (uint16_t) (A_GAS_LOG_DATA_START_ADDRESS +
                       ((uint32_t) physical_index * A_GAS_LOG_RECORD_SIZE));
}

/*
 * 函数名：A_GasLog_PageIsErased。
 * 说明：检查一个AT24C256物理页是否已经全部写成0xFF。
 * 输入：data为包含64字节读回数据的只读缓存。
 * 输出：全部字节均为0xFF时返回true，参数无效或存在其他数值时返回false。
 */
static bool A_GasLog_PageIsErased(const uint8_t *data)
{
    size_t index;

    if (data == NULL)
    {
        return false;
    }
    for (index = 0U; index < AT24C256_PAGE_SIZE_BYTES; ++index)
    {
        if (data[index] != 0xFFU)
        {
            return false;
        }
    }
    return true;
}

/*
 * 函数名：A_GasLog_DecodeHeader。
 * 说明：校验并解码一份32字节日志管理头。
 * 输入：data 为只读管理头缓存；candidate 为解码结果输出指针。
 * 输出：管理头标识、版本、范围和CRC全部正确时返回true，否则返回false。
 */
static bool A_GasLog_DecodeHeader(const uint8_t *data,
                                  A_Gas_Log_Header_Candidate *candidate)
{
    uint16_t stored_crc;

    if ((data == NULL) || (candidate == NULL))
    {
        return false;
    }

    (void) memset(candidate, 0, sizeof(*candidate));
    stored_crc = A_GasLog_ReadU16(&data[A_GAS_LOG_HEADER_SIZE - 2U]);
    if ((data[0] != (uint8_t) A_GAS_LOG_HEADER_MAGIC_0) ||
        (data[1] != (uint8_t) A_GAS_LOG_HEADER_MAGIC_1) ||
        (data[2] != (uint8_t) A_GAS_LOG_HEADER_MAGIC_2) ||
        (data[3] != (uint8_t) A_GAS_LOG_HEADER_MAGIC_3) ||
        (data[4] != A_GAS_LOG_HEADER_VERSION) ||
        (data[5] != A_GAS_LOG_HEADER_SIZE) ||
        (stored_crc != A_GasLog_Crc16(data, A_GAS_LOG_HEADER_SIZE - 2U)))
    {
        return false;
    }

    candidate->generation = A_GasLog_ReadU32(&data[6]);
    candidate->next_sequence = A_GasLog_ReadU32(&data[10]);
    candidate->write_index = A_GasLog_ReadU16(&data[14]);
    candidate->valid_count = A_GasLog_ReadU16(&data[16]);
    candidate->last_regular_key = A_GasLog_ReadU32(&data[18]);
    if ((candidate->write_index >= A_GAS_LOG_PHYSICAL_SLOT_COUNT) ||
        (candidate->valid_count > A_GAS_LOG_RECORD_CAPACITY) ||
        (candidate->next_sequence == 0U))
    {
        return false;
    }
    // CRC通过后仍校验索引范围，防止随机数据碰巧形成合法CRC后造成EEPROM越界访问。

    candidate->valid = true;
    return true;
}

/*
 * 函数名：A_GasLog_WriteHeader。
 * 说明：把指定循环日志状态编码、写入并读回校验到一个管理头副本。
 * 输入：context 为日志上下文；copy 为目标副本；generation、next_sequence、write_index、valid_count和last_regular_key为待保存状态。
 * 输出：EEPROM写入和逐字节读回校验均成功时返回true，否则返回false。
 */
static bool A_GasLog_WriteHeader(A_Gas_Log_Context *context,
                                 uint8_t copy,
                                 uint32_t generation,
                                 uint32_t next_sequence,
                                 uint16_t write_index,
                                 uint16_t valid_count,
                                 uint32_t last_regular_key)
{
    uint8_t data[A_GAS_LOG_HEADER_SIZE];
    uint8_t verify[A_GAS_LOG_HEADER_SIZE];
    uint16_t crc;
    uint16_t address;

    if ((context == NULL) || (context->storage == NULL))
    {
        return false;
    }

    (void) memset(data, 0xFF, sizeof(data));
    data[0] = (uint8_t) A_GAS_LOG_HEADER_MAGIC_0;
    data[1] = (uint8_t) A_GAS_LOG_HEADER_MAGIC_1;
    data[2] = (uint8_t) A_GAS_LOG_HEADER_MAGIC_2;
    data[3] = (uint8_t) A_GAS_LOG_HEADER_MAGIC_3;
    data[4] = A_GAS_LOG_HEADER_VERSION;
    data[5] = A_GAS_LOG_HEADER_SIZE;
    A_GasLog_WriteU32(&data[6], generation);
    A_GasLog_WriteU32(&data[10], next_sequence);
    A_GasLog_WriteU16(&data[14], write_index);
    A_GasLog_WriteU16(&data[16], valid_count);
    A_GasLog_WriteU32(&data[18], last_regular_key);
    crc = A_GasLog_Crc16(data, A_GAS_LOG_HEADER_SIZE - 2U);
    A_GasLog_WriteU16(&data[A_GAS_LOG_HEADER_SIZE - 2U], crc);
    address = A_GasLog_HeaderAddress(copy);
    // 管理头写后立即读回逐字节比较，只有确认落盘才允许推进内存循环索引。

    return (A_Storage_Write(context->storage, address, data, sizeof(data)) &&
            A_Storage_Read(context->storage, address, verify, sizeof(verify)) &&
            (memcmp(data, verify, sizeof(data)) == 0));
}

/*
 * 函数名：A_GasLog_CopyDateTime。
 * 说明：把系统日期时间编码到日志记录的固定六字节时间区。
 * 输入：record 为32字节日志缓存；date_time 为有效的只读系统日期时间。
 * 输出：无；2000～2099年份编码为相对2000年的单字节数值。
 */
static void A_GasLog_CopyDateTime(uint8_t *record, const Gas_Date_Time *date_time)
{
    record[6] = (uint8_t) (date_time->year - 2000U);
    record[7] = date_time->month;
    record[8] = date_time->day;
    record[9] = date_time->hour;
    record[10] = date_time->minute;
    record[11] = date_time->second;
}

/*
 * 函数名：A_GasLog_PressureToRaw。
 * 说明：把有效MPa压力转换为乘以1000的无符号16位定点数，并对范围进行饱和保护。
 * 输入：pressure_mpa 为压力值；quality 为对应压力数据质量。
 * 输出：数据无效或不大于0时返回0，超出编码范围时返回65535，否则返回四舍五入后的原始值。
 */
static uint16_t A_GasLog_PressureToRaw(float pressure_mpa,
                                       gas_pressure_quality_t quality)
{
    if ((quality != GAS_PRESSURE_VALID) || !(pressure_mpa > 0.0F))
    {
        return 0U;
    }
    if (pressure_mpa >= GAS_CONFIG_PRESSURE_MAX_ENCODED_MPA)
    {
        return 65535U;
    }
    return (uint16_t) ((pressure_mpa * GAS_CONFIG_PRESSURE_SCALE) + 0.5F);
}

/*
 * 函数名：A_GasLog_EncodeRecordPrefix。
 * 说明：初始化记录并编码类型、版本、流水号和日期时间公共字段。
 * 输入：record 为32字节输出缓存；type 为日志类型；sequence 为流水号；date_time 为有效时间。
 * 输出：无；公共字段写入record，其余字段先清零。
 */
static void A_GasLog_EncodeRecordPrefix(uint8_t *record,
                                        A_Gas_Log_Type type,
                                        uint32_t sequence,
                                        const Gas_Date_Time *date_time)
{
    (void) memset(record, 0, A_GAS_LOG_RECORD_SIZE);
    record[0] = (uint8_t) type;
    record[1] = A_GAS_LOG_FORMAT_VERSION;
    A_GasLog_WriteU32(&record[2], sequence);
    A_GasLog_CopyDateTime(record, date_time);
}

/*
 * 函数名：A_GasLog_EncodeRegularRecord。
 * 说明：编码包含时间、六瓶压力、总压力、六瓶状态和压力有效位图的32字节常规记录。
 * 输入：context 为只读日志上下文；system 为只读气源状态；record 为输出缓存。
 * 输出：无；完整记录和CRC16写入record。
 */
static void A_GasLog_EncodeRegularRecord(const A_Gas_Log_Context *context,
                                         const Gas_System *system,
                                         uint8_t *record)
{
    uint32_t packed_states = 0U;
    uint8_t quality_mask = 0U;
    uint8_t index;
    uint16_t crc;

    A_GasLog_EncodeRecordPrefix(record,
                                A_GAS_LOG_TYPE_REGULAR,
                                context->next_sequence,
                                &system->date_time);
    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        uint16_t pressure = A_GasLog_PressureToRaw(system->cylinder[index].pressure_mpa,
                                                   system->cylinder[index].pressure_quality);

        A_GasLog_WriteU16(&record[12U + ((uint16_t) index * 2U)], pressure);
        packed_states |= ((uint32_t) system->cylinder[index].state & 0x07UL) << (index * 3U);
        if (system->cylinder[index].pressure_quality == GAS_PRESSURE_VALID)
        {
            quality_mask = (uint8_t) (quality_mask | (uint8_t) (1U << index));
        }
    }
    A_GasLog_WriteU16(&record[24],
                      A_GasLog_PressureToRaw(system->total_pressure.pressure_mpa,
                                             system->total_pressure.pressure_quality));
    record[26] = (uint8_t) (packed_states >> 16U);
    record[27] = (uint8_t) (packed_states >> 8U);
    record[28] = (uint8_t) packed_states;
    if (system->total_pressure.pressure_quality == GAS_PRESSURE_VALID)
    {
        quality_mask = (uint8_t) (quality_mask | 0x40U);
    }
    record[29] = quality_mask;
    crc = A_GasLog_Crc16(record, A_GAS_LOG_RECORD_SIZE - 2U);
    A_GasLog_WriteU16(&record[A_GAS_LOG_RECORD_SIZE - 2U], crc);
    // 压力无效时数值写0，必须结合第29字节有效位图区分真实零压力。
}

/*
 * 函数名：A_GasLog_BuildValveMask。
 * 说明：根据阀门种类生成六路阀门当前命令位图。
 * 输入：system 为只读气源状态；valve_kind 为0进气、1排气、2测试。
 * 输出：返回bit0～bit5对应1～6号瓶的阀门开关位图。
 */
static uint8_t A_GasLog_BuildValveMask(const Gas_System *system, uint8_t valve_kind)
{
    uint8_t mask = 0U;
    uint8_t index;

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        bool on = (valve_kind == 0U) ? system->cylinder[index].supply_cmd :
                  ((valve_kind == 1U) ? system->cylinder[index].exhaust_cmd :
                                       system->cylinder[index].test_cmd);
        if (on)
        {
            mask = (uint8_t) (mask | (uint8_t) (1U << index));
        }
    }
    return mask;
}

/*
 * 函数名：A_GasLog_BuildQualifiedMask。
 * 说明：生成六瓶工作人员测试合格标志位图。
 * 输入：system 为只读气源状态。
 * 输出：返回bit0～bit5对应1～6号瓶的测试合格位图。
 */
static uint8_t A_GasLog_BuildQualifiedMask(const Gas_System *system)
{
    uint8_t mask = 0U;
    uint8_t index;

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        if (system->cylinder[index].qualification_passed)
        {
            mask = (uint8_t) (mask | (uint8_t) (1U << index));
        }
    }
    return mask;
}

/*
 * 函数名：A_GasLog_EncodeEventRecord。
 * 说明：编码一次气瓶状态变化及变化时压力、阀门、模式和报警信息。
 * 输入：context 为日志上下文；system 为只读气源状态；index 为变化气瓶索引；old_state为原状态；record为输出缓存。
 * 输出：无；完整事件记录和CRC16写入record。
 */
static void A_GasLog_EncodeEventRecord(const A_Gas_Log_Context *context,
                                       const Gas_System *system,
                                       uint8_t index,
                                       gas_cylinder_state_t old_state,
                                       uint8_t *record)
{
    const Gas_Cylinder *cylinder = &system->cylinder[index];
    uint16_t crc;

    A_GasLog_EncodeRecordPrefix(record,
                                A_GAS_LOG_TYPE_EVENT,
                                context->next_sequence,
                                &system->date_time);
    record[12] = (uint8_t) (index + 1U);
    record[13] = (uint8_t) old_state;
    record[14] = (uint8_t) cylinder->state;
    record[15] = A_GAS_LOG_EVENT_STATE_CHANGED;
    A_GasLog_WriteU16(&record[16],
                      A_GasLog_PressureToRaw(cylinder->pressure_mpa,
                                             cylinder->pressure_quality));
    record[18] = (uint8_t) cylinder->pressure_quality;
    record[19] = (uint8_t) system->mode;
    record[20] = A_GasLog_BuildValveMask(system, 0U);
    record[21] = A_GasLog_BuildValveMask(system, 1U);
    record[22] = A_GasLog_BuildValveMask(system, 2U);
    record[23] = A_GasLog_BuildQualifiedMask(system);
    A_GasLog_WriteU32(&record[24], system->alarm_bits);
    record[28] = (system->active_index < GAS_CYLINDER_COUNT) ?
                 (uint8_t) (system->active_index + 1U) : 0U;
    record[29] = (uint8_t) system->switch_state;
    crc = A_GasLog_Crc16(record, A_GAS_LOG_RECORD_SIZE - 2U);
    A_GasLog_WriteU16(&record[A_GAS_LOG_RECORD_SIZE - 2U], crc);
    // 事件同时保存阀门、报警和切瓶子状态，便于上位机还原状态变化现场。
}

/*
 * 函数名：A_GasLog_MakeRegularKey。
 * 说明：把年月日时和当前半小时转换为可持久化比较的唯一时段键。
 * 输入：date_time 为有效的只读系统日期时间。
 * 输出：返回对应半小时时段的无符号32位键值。
 */
static uint32_t A_GasLog_MakeRegularKey(const Gas_Date_Time *date_time)
{
    uint32_t key = date_time->year;

    key = (key * 13UL) + date_time->month;
    key = (key * 32UL) + date_time->day;
    key = (key * 24UL) + date_time->hour;
    key = (key * 2UL) + ((date_time->minute >= 30U) ? 1UL : 0UL);
    return key;
}

/*
 * 函数名：A_GasLog_Append。
 * 说明：先写入一条记录，再交替更新管理头副本，以掉电安全方式推进循环索引。
 * 输入：context 为日志上下文；record 为32字节记录；regular_key为本次提交后保存的常规时段键。
 * 输出：记录和新管理头均写入并读回成功时返回true，否则返回false且内存索引不推进。
 */
static bool A_GasLog_Append(A_Gas_Log_Context *context,
                            const uint8_t *record,
                            uint32_t regular_key)
{
    uint8_t verify[A_GAS_LOG_RECORD_SIZE];
    uint8_t next_copy;
    uint32_t next_generation;
    uint32_t next_sequence;
    uint16_t next_write_index;
    uint16_t next_valid_count;
    uint16_t address;

    if ((context == NULL) || (record == NULL) || !context->ready ||
        (context->storage == NULL))
    {
        return false;
    }

    address = A_GasLog_RecordAddress(context->write_index);
    if (!A_Storage_Write(context->storage, address, record, A_GAS_LOG_RECORD_SIZE) ||
        !A_Storage_Read(context->storage, address, verify, sizeof(verify)) ||
        (memcmp(record, verify, sizeof(verify)) != 0))
    {
        return false;
    }
    // 先把新记录写入备用槽；此时即使掉电，旧管理头仍只引用此前已提交的记录。

    next_generation = context->generation + 1UL;
    next_sequence = context->next_sequence + 1UL;
    if (next_sequence == 0U)
    {
        next_sequence = 1UL;
    }
    next_write_index = (uint16_t) (context->write_index + 1U);
    if (next_write_index >= A_GAS_LOG_PHYSICAL_SLOT_COUNT)
    {
        next_write_index = 0U;
    }
    next_valid_count = (context->valid_count < A_GAS_LOG_RECORD_CAPACITY) ?
                       (uint16_t) (context->valid_count + 1U) : context->valid_count;
    next_copy = (uint8_t) (context->active_header_copy ^ 1U);
    // A/B管理头交替提交，新副本成功前绝不修改context中的有效索引。

    if (!A_GasLog_WriteHeader(context,
                              next_copy,
                              next_generation,
                              next_sequence,
                              next_write_index,
                              next_valid_count,
                              regular_key))
    {
        return false;
    }

    context->generation = next_generation;
    context->next_sequence = next_sequence;
    context->write_index = next_write_index;
    context->valid_count = next_valid_count;
    context->last_regular_key = regular_key;
    context->active_header_copy = next_copy;
    // 管理头落盘成功后再一次性提交内存状态，形成完整的两阶段写入。
    return true;
}

/*
 * 函数名：A_GasLog_Init。
 * 说明：加载双副本日志管理头，或在首次使用时创建空循环日志，并建立六瓶状态快照。
 * 输入：context 为日志上下文；storage 为已初始化EEPROM实例；system 为当前只读气源状态。
 * 输出：日志区成功恢复或初始化时返回true，否则返回false。
 */
bool A_GasLog_Init(A_Gas_Log_Context *context,
                   A_Storage_Context *storage,
                   const Gas_System *system)
{
    uint8_t data_a[A_GAS_LOG_HEADER_SIZE];
    uint8_t data_b[A_GAS_LOG_HEADER_SIZE];
    A_Gas_Log_Header_Candidate candidate_a;
    A_Gas_Log_Header_Candidate candidate_b;
    const A_Gas_Log_Header_Candidate *selected = NULL;
    uint8_t index;

    if ((context == NULL) || (storage == NULL) || (system == NULL) || !storage->ready)
    {
        return false;
    }

    (void) memset(context, 0, sizeof(*context));
    context->storage = storage;
    (void) memset(&candidate_a, 0, sizeof(candidate_a));
    (void) memset(&candidate_b, 0, sizeof(candidate_b));
    if (A_Storage_Read(storage, A_GAS_LOG_HEADER_A_ADDRESS, data_a, sizeof(data_a)))
    {
        (void) A_GasLog_DecodeHeader(data_a, &candidate_a);
    }
    if (A_Storage_Read(storage, A_GAS_LOG_HEADER_B_ADDRESS, data_b, sizeof(data_b)))
    {
        (void) A_GasLog_DecodeHeader(data_b, &candidate_b);
    }

    if (candidate_a.valid && candidate_b.valid)
    {
        bool b_is_newer = ((int32_t) (candidate_b.generation - candidate_a.generation) > 0);

        selected = b_is_newer ? &candidate_b : &candidate_a;
        context->active_header_copy = b_is_newer ? 1U : 0U;
        // 两份管理头均有效时按代数选择较新副本，兼容32位代数自然回绕。
    }
    else if (candidate_a.valid)
    {
        selected = &candidate_a;
        context->active_header_copy = 0U;
    }
    else if (candidate_b.valid)
    {
        selected = &candidate_b;
        context->active_header_copy = 1U;
    }

    if (selected != NULL)
    {
        context->generation = selected->generation;
        context->next_sequence = selected->next_sequence;
        context->write_index = selected->write_index;
        context->valid_count = selected->valid_count;
        context->last_regular_key = selected->last_regular_key;
    }
    else
    {
        context->generation = 1UL;
        context->next_sequence = 1UL;
        context->write_index = 0U;
        context->valid_count = 0U;
        context->last_regular_key = A_GAS_LOG_LAST_REGULAR_KEY_NONE;
        context->active_header_copy = 0U;
        if (!A_GasLog_WriteHeader(context,
                                  0U,
                                  context->generation,
                                  context->next_sequence,
                                  context->write_index,
                                  context->valid_count,
                                  context->last_regular_key))
        {
            return false;
        }
        // 两份管理头都无效表示首次使用或索引损坏，仅重建空索引，不批量擦写数据区。
    }

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        context->previous_state[index] = system->cylinder[index].state;
    }
    context->ready = true;
    return true;
}

/*
 * 函数名：A_GasLog_FinishClear。
 * 说明：结束日志清除状态机并用当前六瓶状态建立新的事件比较快照。
 * 输入：context为日志上下文；system为当前只读系统状态；result为成功或失败结果。
 * 输出：无；清除状态回到空闲，结果和状态快照写入context。
 */
static void A_GasLog_FinishClear(A_Gas_Log_Context *context,
                                 const Gas_System *system,
                                 A_Gas_Log_Clear_Result result)
{
    uint8_t index;

    context->clear_state = A_GAS_LOG_CLEAR_IDLE;
    context->clear_result = result;
    if (context->clear_empty_header_committed && (system != NULL))
    {
        for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
        {
            context->previous_state[index] = system->cylinder[index].state;
        }
        // 空索引已经生效后，以清除结束时的状态作为新基线，避免生成六条伪状态变化记录。
    }
}

/*
 * 函数名：A_GasLog_RequestClear。
 * 说明：请求物理清除全部事件和常规日志；只建立任务，不在本函数中长时间擦写EEPROM。
 * 输入：context为已经初始化的日志上下文。
 * 输出：成功建立清除任务时返回true，参数无效、日志未就绪或已有清除任务时返回false。
 */
bool A_GasLog_RequestClear(A_Gas_Log_Context *context)
{
    if ((context == NULL) || !context->ready ||
        (context->clear_result == A_GAS_LOG_CLEAR_RESULT_BUSY))
    {
        return false;
    }

    context->clear_state = A_GAS_LOG_CLEAR_WRITE_HEADER;
    context->clear_result = A_GAS_LOG_CLEAR_RESULT_BUSY;
    context->clear_address = 0U;
    context->clear_pages_completed = 0U;
    context->clear_empty_header_copy = (uint8_t) (context->active_header_copy ^ 1U);
    context->clear_empty_header_committed = false;
    return true;
}

/*
 * 函数名：A_GasLog_ClearTask。
 * 说明：分步提交空日志头、擦除旧管理头和全部数据页，并逐页读回校验。
 * 输入：context为日志上下文；system为清除期间当前只读系统状态，用于建立新的状态快照和半小时时段。
 * 输出：无；清除进度和最终结果保存在context中。
 */
void A_GasLog_ClearTask(A_Gas_Log_Context *context, const Gas_System *system)
{
    uint8_t verify[AT24C256_PAGE_SIZE_BYTES];
    uint32_t next_generation;
    uint32_t regular_key;
    uint32_t next_address;

    if ((context == NULL) || (system == NULL) || !context->ready ||
        (context->clear_result != A_GAS_LOG_CLEAR_RESULT_BUSY))
    {
        return;
    }

    switch (context->clear_state)
    {
        case A_GAS_LOG_CLEAR_WRITE_HEADER:
            next_generation = context->generation + 1UL;
            regular_key = system->date_time.valid ?
                          A_GasLog_MakeRegularKey(&system->date_time) :
                          A_GAS_LOG_LAST_REGULAR_KEY_NONE;
            if (!A_GasLog_WriteHeader(context,
                                      context->clear_empty_header_copy,
                                      next_generation,
                                      1UL,
                                      0U,
                                      0U,
                                      regular_key))
            {
                A_GasLog_FinishClear(context, system, A_GAS_LOG_CLEAR_RESULT_FAILED);
                break;
            }

            context->generation = next_generation;
            context->next_sequence = 1UL;
            context->write_index = 0U;
            context->valid_count = 0U;
            context->last_regular_key = regular_key;
            context->active_header_copy = context->clear_empty_header_copy;
            context->clear_empty_header_committed = true;
            context->clear_address = A_GasLog_HeaderAddress(
                (uint8_t) (context->clear_empty_header_copy ^ 1U));
            context->clear_state = A_GAS_LOG_CLEAR_ERASE_OLD_HEADER;
            // 先提交代数更高的空管理头，后续任意时刻掉电都不会重新引用旧日志数据。
            break;

        case A_GAS_LOG_CLEAR_ERASE_OLD_HEADER:
            if (!A_Storage_EraseRange(context->storage,
                                      context->clear_address,
                                      AT24C256_PAGE_SIZE_BYTES))
            {
                A_GasLog_FinishClear(context, system, A_GAS_LOG_CLEAR_RESULT_FAILED);
                break;
            }
            context->clear_state = A_GAS_LOG_CLEAR_VERIFY_OLD_HEADER;
            break;

        case A_GAS_LOG_CLEAR_VERIFY_OLD_HEADER:
            if (!A_Storage_Read(context->storage,
                                context->clear_address,
                                verify,
                                sizeof(verify)) ||
                !A_GasLog_PageIsErased(verify))
            {
                A_GasLog_FinishClear(context, system, A_GAS_LOG_CLEAR_RESULT_FAILED);
                break;
            }
            context->clear_pages_completed = 1U;
            context->clear_address = A_GAS_LOG_DATA_START_ADDRESS;
            context->clear_state = A_GAS_LOG_CLEAR_ERASE_DATA;
            break;

        case A_GAS_LOG_CLEAR_ERASE_DATA:
            if (!A_Storage_EraseRange(context->storage,
                                      context->clear_address,
                                      AT24C256_PAGE_SIZE_BYTES))
            {
                A_GasLog_FinishClear(context, system, A_GAS_LOG_CLEAR_RESULT_FAILED);
                break;
            }
            context->clear_state = A_GAS_LOG_CLEAR_VERIFY_DATA;
            break;

        case A_GAS_LOG_CLEAR_VERIFY_DATA:
            if (!A_Storage_Read(context->storage,
                                context->clear_address,
                                verify,
                                sizeof(verify)) ||
                !A_GasLog_PageIsErased(verify))
            {
                A_GasLog_FinishClear(context, system, A_GAS_LOG_CLEAR_RESULT_FAILED);
                break;
            }

            context->clear_pages_completed++;
            next_address = (uint32_t) context->clear_address + AT24C256_PAGE_SIZE_BYTES;
            if (next_address >= AT24C256_CAPACITY_BYTES)
            {
                context->clear_pages_completed = A_GAS_LOG_CLEAR_PAGE_COUNT;
                A_GasLog_FinishClear(context, system, A_GAS_LOG_CLEAR_RESULT_SUCCESS);
            }
            else
            {
                context->clear_address = (uint16_t) next_address;
                context->clear_state = A_GAS_LOG_CLEAR_ERASE_DATA;
            }
            break;

        default:
            A_GasLog_FinishClear(context, system, A_GAS_LOG_CLEAR_RESULT_FAILED);
            break;
    }
}

/*
 * 函数名：A_GasLog_Task。
 * 说明：在时间有效时记录六瓶状态变化，并按半小时时段保存一条常规运行记录。
 * 输入：context 为日志上下文；system 为包含时间、压力和状态的只读气源系统。
 * 输出：本周期无须写入或全部处理成功时返回true，EEPROM写入失败时返回false。
 */
bool A_GasLog_Task(A_Gas_Log_Context *context, const Gas_System *system)
{
    uint8_t record[A_GAS_LOG_RECORD_SIZE];
    uint8_t index;
    uint32_t regular_key;

    if ((context == NULL) || (system == NULL) || !context->ready)
    {
        return false;
    }
    if (context->clear_result == A_GAS_LOG_CLEAR_RESULT_BUSY)
    {
        return true;
    }
    if (!system->date_time.valid)
    {
        return true; // 日志必须带有效时间，无RTC时间时保留状态快照并等待后续有效时间。
    }

    for (index = 0U; index < GAS_CYLINDER_COUNT; ++index)
    {
        if (context->previous_state[index] != system->cylinder[index].state)
        {
            A_GasLog_EncodeEventRecord(context,
                                       system,
                                       index,
                                       context->previous_state[index],
                                       record);
            if (!A_GasLog_Append(context, record, context->last_regular_key))
            {
                return false;
            }
            context->previous_state[index] = system->cylinder[index].state;
            return true; // 每个主循环最多写一条EEPROM记录，避免连续页写长时间阻塞通信任务。
        }
    }

    regular_key = A_GasLog_MakeRegularKey(&system->date_time);
    if (regular_key != context->last_regular_key)
    {
        A_GasLog_EncodeRegularRecord(context, system, record);
        if (!A_GasLog_Append(context, record, regular_key))
        {
            return false;
        }
    }
    return true;
}

/*
 * 函数名：A_GasLog_ReadRecord。
 * 说明：按照从最旧到最新的逻辑顺序读取并校验一条32字节原始日志，供后续通信模块调用。
 * 输入：context 为日志上下文；logical_index 为零起始逻辑序号；record 为32字节输出缓存。
 * 输出：索引有效且记录版本、类型和CRC16全部正确时返回true，否则返回false。
 */
bool A_GasLog_ReadRecord(A_Gas_Log_Context *context,
                         uint16_t logical_index,
                         uint8_t record[A_GAS_LOG_RECORD_SIZE])
{
    uint16_t oldest_index;
    uint16_t physical_index;
    uint16_t stored_crc;

    if ((context == NULL) || (record == NULL) || !context->ready ||
        (context->clear_result == A_GAS_LOG_CLEAR_RESULT_BUSY) ||
        (logical_index >= context->valid_count))
    {
        return false;
    }

    oldest_index = 0U;
    if (context->valid_count >= A_GAS_LOG_RECORD_CAPACITY)
    {
        oldest_index = (uint16_t) (context->write_index + 1U);
        if (oldest_index >= A_GAS_LOG_PHYSICAL_SLOT_COUNT)
        {
            oldest_index = 0U;
        }
        // 写满后write_index指向备用槽，真正最旧记录位于其后一个物理槽位。
    }
    physical_index = (uint16_t) (oldest_index + logical_index);
    if (physical_index >= A_GAS_LOG_PHYSICAL_SLOT_COUNT)
    {
        physical_index = (uint16_t) (physical_index - A_GAS_LOG_PHYSICAL_SLOT_COUNT);
    }
    // 上位机看到的逻辑序号始终从最旧到最新，物理环形地址对外不可见。
    if (!A_Storage_Read(context->storage,
                        A_GasLog_RecordAddress(physical_index),
                        record,
                        A_GAS_LOG_RECORD_SIZE))
    {
        return false;
    }

    stored_crc = A_GasLog_ReadU16(&record[A_GAS_LOG_RECORD_SIZE - 2U]);
    return ((record[1] == A_GAS_LOG_FORMAT_VERSION) &&
            ((record[0] == A_GAS_LOG_TYPE_REGULAR) ||
             (record[0] == A_GAS_LOG_TYPE_EVENT)) &&
            (stored_crc == A_GasLog_Crc16(record, A_GAS_LOG_RECORD_SIZE - 2U)));
}

/*
 * 函数名：A_GasLog_GetCount。
 * 说明：查询循环日志区当前保存的有效记录数量。
 * 输入：context 为只读日志上下文。
 * 输出：返回有效记录数量；上下文未就绪时返回0。
 */
uint16_t A_GasLog_GetCount(const A_Gas_Log_Context *context)
{
    return ((context != NULL) && context->ready &&
            (context->clear_result != A_GAS_LOG_CLEAR_RESULT_BUSY)) ?
           context->valid_count : 0U;
}

/*
 * 函数名：A_GasLog_IsReady。
 * 说明：查询日志管理模块是否已经成功初始化。
 * 输入：context 为只读日志上下文。
 * 输出：日志模块可用时返回true，否则返回false。
 */
bool A_GasLog_IsReady(const A_Gas_Log_Context *context)
{
    return ((context != NULL) && context->ready &&
            (context->clear_result != A_GAS_LOG_CLEAR_RESULT_BUSY));
}

/*
 * 函数名：A_GasLog_GetClearResult。
 * 说明：查询最近一次日志物理清除的当前状态或最终结果。
 * 输入：context为只读日志上下文。
 * 输出：返回空闲、清除中、成功或失败；参数无效时返回失败。
 */
A_Gas_Log_Clear_Result A_GasLog_GetClearResult(const A_Gas_Log_Context *context)
{
    return (context != NULL) ? context->clear_result : A_GAS_LOG_CLEAR_RESULT_FAILED;
}

/*
 * 函数名：A_GasLog_GetClearProgress。
 * 说明：把已完成读回校验的页数转换为0～100的清除百分比。
 * 输入：context为只读日志上下文。
 * 输出：返回0～100的百分比；参数无效时返回0。
 */
uint8_t A_GasLog_GetClearProgress(const A_Gas_Log_Context *context)
{
    uint32_t progress;

    if (context == NULL)
    {
        return 0U;
    }
    if (context->clear_result == A_GAS_LOG_CLEAR_RESULT_SUCCESS)
    {
        return 100U;
    }
    progress = ((uint32_t) context->clear_pages_completed * 100UL) /
               A_GAS_LOG_CLEAR_PAGE_COUNT;
    return (progress > 100UL) ? 100U : (uint8_t) progress;
}

/*
 * 函数名：A_GasLog_IsClearBusy。
 * 说明：查询日志物理清除状态机是否仍占用EEPROM日志区。
 * 输入：context为只读日志上下文。
 * 输出：正在清除时返回true，否则返回false。
 */
bool A_GasLog_IsClearBusy(const A_Gas_Log_Context *context)
{
    return ((context != NULL) &&
            (context->clear_result == A_GAS_LOG_CLEAR_RESULT_BUSY));
}
