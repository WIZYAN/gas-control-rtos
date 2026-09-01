/*
 * Version: v1.13
 * Author: YXZ
 * Created: 2026-08-24
 * Description: 实现外部Modbus RTU从站帧解析、CRC校验和功能码处理。
 */

// 本文件实现 SCI0/RS485 外部 Modbus RTU 从站功能层。
#include "F_Modbus.h"

#include <stddef.h>
#include <string.h>

#define MODBUS_FUNCTION_READ_HOLDING    (0x03U) // 读取保持寄存器功能码。
#define MODBUS_FUNCTION_READ_INPUT      (0x04U) // 读取输入寄存器功能码。
#define MODBUS_FUNCTION_WRITE_SINGLE    (0x06U) // 写单个保持寄存器功能码。
#define MODBUS_FUNCTION_WRITE_MULTIPLE  (0x10U) // 写多个保持寄存器功能码。

#define MODBUS_EXCEPTION_FUNCTION       (0x01U) // 非法功能码异常响应码。
#define MODBUS_EXCEPTION_ADDRESS        (0x02U) // 非法寄存器地址异常响应码。
#define MODBUS_EXCEPTION_VALUE          (0x03U) // 非法寄存器数量或数值异常响应码。

/*
 * 函数名：F_Modbus_ReadU16BigEndian。
 * 说明：从两个连续字节按 Modbus 高字节在前的顺序读取 16 位无符号数。
 * 输入：data 为至少包含两个字节的只读数据指针。
 * 输出：返回组合后的 16 位无符号数。
 */
static uint16_t F_Modbus_ReadU16BigEndian(const uint8_t *data)
{
    return (uint16_t) (((uint16_t) data[0] << 8U) | data[1]);
}

/*
 * 函数名：F_Modbus_WriteU16BigEndian。
 * 说明：将 16 位无符号数按 Modbus 高字节在前的顺序写入两个连续字节。
 * 输入：data 为至少包含两个字节的输出缓存；value 为待写入数值。
 * 输出：无；转换后的两个字节写入 data。
 */
static void F_Modbus_WriteU16BigEndian(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) (value >> 8U);
    data[1] = (uint8_t) value;
}

/*
 * 函数名：F_Modbus_AppendCrc。
 * 说明：为已经生成的 Modbus 响应追加低字节在前的 CRC16。
 * 输入：frame 为响应缓存；payload_length 为不含 CRC 的字节数。
 * 输出：无；CRC 写入 frame 的最后两个位置。
 */
static void F_Modbus_AppendCrc(uint8_t *frame, uint16_t payload_length)
{
    uint16_t crc = F_Modbus_Crc16(frame, payload_length); // 当前作用域变量，用于保存CRC校验值。

    frame[payload_length] = (uint8_t) crc;
    frame[payload_length + 1U] = (uint8_t) (crc >> 8U);
}

/*
 * 函数名：F_Modbus_FrameCrcIsValid。
 * 说明：检查一帧 Modbus RTU 请求的 CRC16 是否正确。
 * 输入：frame 为完整请求帧；length 为包含 CRC 的总字节数。
 * 输出：CRC 正确时返回 true，否则返回 false。
 */
static bool F_Modbus_FrameCrcIsValid(const uint8_t *frame, uint16_t length)
{
    uint16_t received_crc; // 当前作用域变量，用于保存CRC校验值。
    uint16_t calculated_crc; // 当前作用域变量，用于保存CRC校验值。

    if ((frame == NULL) || (length < 4U))
    {
        return false;
    }

    received_crc = (uint16_t) (frame[length - 2U] | ((uint16_t) frame[length - 1U] << 8U));
    calculated_crc = F_Modbus_Crc16(frame, (uint16_t) (length - 2U));
    return (received_crc == calculated_crc);
}

/*
 * 函数名：F_Modbus_SendException。
 * 说明：向外部 Modbus 主站发送标准三字节 PDU 异常响应。
 * 输入：context 为功能层上下文；function_code 为请求功能码；exception_code 为异常码。
 * 输出：无；生成的五字节 RTU 响应通过硬件层发送。
 */
static void F_Modbus_SendException(F_Modbus_Context *context,
                          uint8_t function_code,
                          uint8_t exception_code)
{
    uint8_t response[5]; // 当前作用域变量，用于保存协议响应数组。

    response[0] = context->slave_address;
    response[1] = (uint8_t) (function_code | 0x80U);
    response[2] = exception_code;
    F_Modbus_AppendCrc(response, 3U);
    (void) H_Modbus_Send(context->hardware, response, sizeof(response));
}

/*
 * 函数名：F_Modbus_HandleReadRegisters。
 * 说明：处理功能码 03 或 04 的寄存器读取请求并生成响应。
 * 输入：context 为功能层上下文；request 为完整请求帧；function_code 为 03 或 04。
 * 输出：无；合法请求通过硬件层发送寄存器数据，非法请求发送异常响应。
 */
static void F_Modbus_HandleReadRegisters(F_Modbus_Context *context,
                                const uint8_t *request,
                                uint8_t function_code)
{
    uint8_t response[H_MODBUS_FRAME_MAX_LENGTH]; // 当前作用域变量，用于保存协议响应数组。
    const uint16_t *register_table; // 当前作用域变量，用于保存当前处理数据指针。
    uint16_t table_count; // 当前作用域变量，用于保存数量计数。
    uint16_t start_address = F_Modbus_ReadU16BigEndian(&request[2]); // 当前作用域变量，用于保存存储或寄存器地址。
    uint16_t quantity = F_Modbus_ReadU16BigEndian(&request[4]); // 当前作用域变量，用于保存当前处理数据。
    uint16_t start_offset; // 当前作用域变量，用于保存数据偏移量。
    uint16_t i; // 当前作用域变量，用于保存当前处理数据。
    uint16_t response_length; // 当前作用域变量，用于保存有效数据长度。

    if (quantity == 0U)
    {
        F_Modbus_SendException(context, function_code, MODBUS_EXCEPTION_VALUE);
        return;
    }

    if (function_code == MODBUS_FUNCTION_READ_INPUT)
    {
        register_table = context->input_register; // 当前作用域变量，用于保存当前处理数据。
        table_count = F_MODBUS_INPUT_REGISTER_COUNT;
        start_offset = start_address;
    }
    else
    {
        register_table = context->holding_register; // 当前作用域变量，用于保存当前处理数据。
        table_count = F_MODBUS_HOLDING_REGISTER_COUNT;
        if (start_address < F_MODBUS_HOLDING_BASE_ADDRESS)
        {
            F_Modbus_SendException(context, function_code, MODBUS_EXCEPTION_ADDRESS);
            return;
        }
        start_offset = (uint16_t) (start_address - F_MODBUS_HOLDING_BASE_ADDRESS);
    }
    // 输入寄存器从0开始，保持寄存器从0x0100开始，两张表在此统一转换为数组偏移。

    if ((start_offset >= table_count) || (quantity > (uint16_t) (table_count - start_offset)) ||
        ((uint32_t) (5U + (quantity * 2U)) > H_MODBUS_FRAME_MAX_LENGTH))
    {
        F_Modbus_SendException(context, function_code, MODBUS_EXCEPTION_ADDRESS);
        return;
    }
    // 同时检查地址范围和响应缓存容量，避免数量合法但组帧后越过128字节缓存。

    response[0] = context->slave_address;
    response[1] = function_code;
    response[2] = (uint8_t) (quantity * 2U);
    for (i = 0U; i < quantity; ++i)
    {
        F_Modbus_WriteU16BigEndian(&response[3U + (i * 2U)], register_table[start_offset + i]);
    }

    response_length = (uint16_t) (3U + (quantity * 2U));
    F_Modbus_AppendCrc(response, response_length);
    (void) H_Modbus_Send(context->hardware, response, (uint16_t) (response_length + 2U));
}

/*
 * 函数名：F_Modbus_HandleWriteSingle。
 * 说明：处理功能码 06 的单个保持寄存器写入请求。
 * 输入：context 为功能层上下文；request 为完整的八字节请求帧。
 * 输出：无；合法请求回送原请求，非法地址发送异常响应。
 */
static void F_Modbus_HandleWriteSingle(F_Modbus_Context *context, const uint8_t *request)
{
    uint8_t response[8]; // 当前作用域变量，用于保存协议响应数组。
    uint16_t address = F_Modbus_ReadU16BigEndian(&request[2]); // 当前作用域变量，用于保存存储或寄存器地址。
    uint16_t value = F_Modbus_ReadU16BigEndian(&request[4]); // 当前作用域变量，用于保存当前处理值。
    uint16_t offset; // 当前作用域变量，用于保存数据偏移量。

    if ((address < F_MODBUS_HOLDING_BASE_ADDRESS) ||
        (address >= (F_MODBUS_HOLDING_BASE_ADDRESS + F_MODBUS_HOLDING_REGISTER_COUNT)))
    {
        F_Modbus_SendException(context, MODBUS_FUNCTION_WRITE_SINGLE, MODBUS_EXCEPTION_ADDRESS);
        return;
    }

    offset = (uint16_t) (address - F_MODBUS_HOLDING_BASE_ADDRESS);
    context->holding_register[offset] = value;
    context->write_start_offset = offset;
    context->write_register_count = 1U;
    context->write_pending = true;
    memcpy(response, request, sizeof(response));
    // 功能码06要求成功响应原样回显，应用层通过write_pending异步解释该寄存器含义。
    (void) H_Modbus_Send(context->hardware, response, sizeof(response));
}

/*
 * 函数名：F_Modbus_HandleWriteMultiple。
 * 说明：处理功能码 10 的多个连续保持寄存器写入请求。
 * 输入：context 为功能层上下文；request 为完整请求帧；request_length 为包含 CRC 的总长度。
 * 输出：无；合法请求返回起始地址和数量，非法参数发送异常响应。
 */
static void F_Modbus_HandleWriteMultiple(F_Modbus_Context *context,
                                const uint8_t *request,
                                uint16_t request_length)
{
    uint8_t response[8]; // 当前作用域变量，用于保存协议响应数组。
    uint16_t address = F_Modbus_ReadU16BigEndian(&request[2]); // 当前作用域变量，用于保存存储或寄存器地址。
    uint16_t quantity = F_Modbus_ReadU16BigEndian(&request[4]); // 当前作用域变量，用于保存当前处理数据。
    uint8_t byte_count = request[6]; // 当前作用域变量，用于保存数量计数。
    uint16_t offset; // 当前作用域变量，用于保存数据偏移量。
    uint16_t i; // 当前作用域变量，用于保存当前处理数据。

    if ((quantity == 0U) || (byte_count != (uint8_t) (quantity * 2U)) ||
        (request_length != (uint16_t) (9U + byte_count)))
    {
        F_Modbus_SendException(context, MODBUS_FUNCTION_WRITE_MULTIPLE, MODBUS_EXCEPTION_VALUE);
        return;
    }

    if ((address < F_MODBUS_HOLDING_BASE_ADDRESS) ||
        (address >= (F_MODBUS_HOLDING_BASE_ADDRESS + F_MODBUS_HOLDING_REGISTER_COUNT)))
    {
        F_Modbus_SendException(context, MODBUS_FUNCTION_WRITE_MULTIPLE, MODBUS_EXCEPTION_ADDRESS);
        return;
    }

    offset = (uint16_t) (address - F_MODBUS_HOLDING_BASE_ADDRESS);
    if (quantity > (uint16_t) (F_MODBUS_HOLDING_REGISTER_COUNT - offset))
    {
        F_Modbus_SendException(context, MODBUS_FUNCTION_WRITE_MULTIPLE, MODBUS_EXCEPTION_ADDRESS);
        return;
    }

    for (i = 0U; i < quantity; ++i)
    {
        context->holding_register[offset + i] = F_Modbus_ReadU16BigEndian(&request[7U + (i * 2U)]);
    }
    // 先完整校验长度和地址再批量写表，防止非法帧造成部分寄存器已经被修改。

    context->write_start_offset = offset;
    context->write_register_count = quantity;
    context->write_pending = true;
    memcpy(response, request, 6U);
    F_Modbus_AppendCrc(response, 6U);
    (void) H_Modbus_Send(context->hardware, response, sizeof(response));
}

/*
 * 函数名：F_Modbus_Init。
 * 说明：初始化外部 Modbus 硬件层、从站功能层及输入、保持寄存器表。
 * 输入：context 为待初始化的功能层上下文；hardware 为待初始化的硬件层实例；slave_address 为从站地址。
 * 输出：参数有效且硬件层初始化成功时返回 true，否则返回 false。
 */
bool F_Modbus_Init(F_Modbus_Context *context,
                 H_Modbus_Context *hardware,
                 uint8_t slave_address)
{
    if ((context == NULL) || (hardware == NULL) || (slave_address == 0U) || (slave_address > 247U))
    {
        return false;
    }

    memset(context, 0, sizeof(*context));
    if (!H_Modbus_Init(hardware))
    {
        return false;
    }
    context->hardware = hardware;
    context->slave_address = slave_address;
    return true;
}

/*
 * 函数名：F_Modbus_Deinit。
 * 说明：通过硬件层关闭SCI0外部Modbus接口并清除功能层状态。
 * 输入：context为功能层上下文输入输出指针。
 * 输出：接口已经关闭或成功关闭时返回true，否则返回false。
 */
bool F_Modbus_Deinit(F_Modbus_Context *context)
{
    bool success; // success 状态标志；使用范围：当前声明作用域内使用；取值范围：false/true，false表示操作失败，true表示操作成功。

    if (context == NULL)
    {
        return false;
    }
    if (context->hardware == NULL)
    {
        return true;
    }
    success = H_Modbus_Deinit(context->hardware);
    context->hardware = NULL;
    context->write_pending = false;
    return success;
}

/*
 * 函数名：F_Modbus_HasFault。
 * 说明：逐层查询SCI0外部Modbus是否存在硬件通讯故障。
 * 输入：context为只读功能层上下文。
 * 输出：功能层无硬件实例或硬件故障时返回true，否则返回false。
 */
bool F_Modbus_HasFault(const F_Modbus_Context *context)
{
    return ((context == NULL) || H_Modbus_HasFault(context->hardware));
}

/*
 * 函数名：F_Modbus_Task。
 * 说明：处理一帧外部 Modbus RTU 请求，支持功能码 03、04、06 和 10。
 * 输入：context 为功能层上下文输入输出指针。
 * 输出：无；响应通过硬件层发送，写寄存器事件记录在 context 中。
 */
void F_Modbus_Task(F_Modbus_Context *context)
{
    uint8_t request[H_MODBUS_FRAME_MAX_LENGTH]; // 当前作用域变量，用于保存待处理请求数组。
    uint16_t request_length; // 当前作用域变量，用于保存有效数据长度。
    uint8_t function_code; // 当前作用域变量，用于保存当前处理数据。

    if ((context == NULL) || (context->hardware == NULL) ||
        H_Modbus_IsTransmitBusy(context->hardware) ||
        !H_Modbus_TakeFrame(context->hardware, request, sizeof(request), &request_length))
    {
        return;
    }

    if (!F_Modbus_FrameCrcIsValid(request, request_length) || (request[0] != context->slave_address))
    {
        return; // CRC 错误及其他从站地址的请求不应答。
    }
    // RTU总线对CRC错误保持静默，避免噪声帧触发异常响应并进一步占用总线。

    function_code = request[1];
    if (((function_code == MODBUS_FUNCTION_READ_HOLDING) ||
         (function_code == MODBUS_FUNCTION_READ_INPUT) ||
         (function_code == MODBUS_FUNCTION_WRITE_SINGLE)) && (request_length != 8U))
    {
        F_Modbus_SendException(context, function_code, MODBUS_EXCEPTION_VALUE);
        return;
    }
    // 03、04、06请求长度固定为8字节；10的长度由字节计数字段在专用处理函数中校验。

    switch (function_code)
    {
        case MODBUS_FUNCTION_READ_HOLDING:
        case MODBUS_FUNCTION_READ_INPUT:
            F_Modbus_HandleReadRegisters(context, request, function_code);
            break;

        case MODBUS_FUNCTION_WRITE_SINGLE:
            F_Modbus_HandleWriteSingle(context, request);
            break;

        case MODBUS_FUNCTION_WRITE_MULTIPLE:
            F_Modbus_HandleWriteMultiple(context, request, request_length);
            break;

        default:
            F_Modbus_SendException(context, function_code, MODBUS_EXCEPTION_FUNCTION);
            break;
    }
}

/*
 * 函数名：F_Modbus_SetInputRegister。
 * 说明：更新一个供功能码 04 读取的输入寄存器。
 * 输入：context 为功能层上下文；offset 为输入寄存器偏移；value 为待写入数值。
 * 输出：偏移有效并成功更新时返回 true，否则返回 false。
 */
bool F_Modbus_SetInputRegister(F_Modbus_Context *context, uint16_t offset, uint16_t value)
{
    if ((context == NULL) || (offset >= F_MODBUS_INPUT_REGISTER_COUNT))
    {
        return false;
    }

    context->input_register[offset] = value;
    return true;
}

/*
 * 函数名：F_Modbus_GetHoldingRegister。
 * 说明：读取一个保持寄存器的当前数值，供应用层处理主站命令。
 * 输入：context 为只读功能层上下文；offset 为保持寄存器偏移；value 为数值输出指针。
 * 输出：参数有效时返回 true，否则返回 false；寄存器数值通过 value 输出。
 */
bool F_Modbus_GetHoldingRegister(const F_Modbus_Context *context,
                               uint16_t offset,
                               uint16_t *value)
{
    if ((context == NULL) || (value == NULL) || (offset >= F_MODBUS_HOLDING_REGISTER_COUNT))
    {
        return false;
    }

    *value = context->holding_register[offset];
    return true;
}

/*
 * 函数名：F_Modbus_SetHoldingRegister。
 * 说明：由本机应用层更新一个保持寄存器的当前数值。
 * 输入：context 为功能层上下文；offset 为保持寄存器偏移；value 为待写入数值。
 * 输出：参数有效并成功更新时返回 true，否则返回 false。
 */
bool F_Modbus_SetHoldingRegister(F_Modbus_Context *context, uint16_t offset, uint16_t value)
{
    if ((context == NULL) || (offset >= F_MODBUS_HOLDING_REGISTER_COUNT))
    {
        return false;
    }

    context->holding_register[offset] = value;
    return true;
}

/*
 * 函数名：F_Modbus_TakeWriteEvent。
 * 说明：取出并清除最近一次由外部主站产生的保持寄存器写入事件。
 * 输入：context 为功能层上下文；start_offset 为起始偏移输出指针；register_count 为写入数量输出指针。
 * 输出：存在待处理写事件时返回 true，否则返回 false。
 */
bool F_Modbus_TakeWriteEvent(F_Modbus_Context *context,
                           uint16_t *start_offset,
                           uint16_t *register_count)
{
    if ((context == NULL) || (start_offset == NULL) || (register_count == NULL) ||
        !context->write_pending)
    {
        return false;
    }

    *start_offset = context->write_start_offset;
    *register_count = context->write_register_count;
    context->write_pending = false;
    // 写事件只允许应用层消费一次，命令寄存器本身是否清零由对应应用逻辑决定。
    return true;
}

/*
 * 函数名：F_Modbus_Crc16。
 * 说明：计算 Modbus RTU 使用的 CRC16 校验值。
 * 输入：data 为待校验数据；length 为字节数。
 * 输出：返回 CRC16 数值，发送时低字节在前。
 */
uint16_t F_Modbus_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU; // 当前作用域变量，用于保存CRC校验值。
    uint16_t index; // 当前作用域变量，用于保存遍历索引。
    uint8_t bit; // 当前作用域变量，用于保存位掩码。

    if (data == NULL)
    {
        return 0U;
    }

    for (index = 0U; index < length; ++index)
    {
        crc = (uint16_t) (crc ^ (uint16_t) data[index]);
        for (bit = 0U; bit < 8U; ++bit)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t) ((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}
