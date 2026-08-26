/* generated HAL header file - do not edit */
#ifndef HAL_DATA_H_
#define HAL_DATA_H_
#include <stdint.h>
#include "bsp_api.h"
#include "common_data.h"
#include "r_can.h"
#include "r_can_api.h"
#include "r_sci_uart.h"
#include "r_uart_api.h"
FSP_HEADER
/** CAN on CAN Instance. */
extern const can_instance_t g_can0;
/** Access the CAN instance using these structures when calling API functions directly (::p_api is not used). */
extern can_instance_ctrl_t g_can0_ctrl;
extern const can_cfg_t g_can0_cfg;
extern const can_extended_cfg_t g_can0_cfg_extend;

#ifndef can_callback
void can_callback(can_callback_args_t *p_args);
#endif
#define CAN_NO_OF_MAILBOXES_g_can0 (4)
/** UART on SCI Instance. */
extern const uart_instance_t rs485_out;

/** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
extern sci_uart_instance_ctrl_t rs485_out_ctrl;
extern const uart_cfg_t rs485_out_cfg;
extern const sci_uart_extended_cfg_t rs485_out_cfg_extend;

#ifndef rs485_out_callback
void rs485_out_callback(uart_callback_args_t *p_args);
#endif
/** UART on SCI Instance. */
extern const uart_instance_t dis_uart;

/** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
extern sci_uart_instance_ctrl_t dis_uart_ctrl;
extern const uart_cfg_t dis_uart_cfg;
extern const sci_uart_extended_cfg_t dis_uart_cfg_extend;

#ifndef dis_uart_callback
void dis_uart_callback(uart_callback_args_t *p_args);
#endif
/** UART on SCI Instance. */
extern const uart_instance_t rs485_sensor;

/** Access the UART instance using these structures when calling API functions directly (::p_api is not used). */
extern sci_uart_instance_ctrl_t rs485_sensor_ctrl;
extern const uart_cfg_t rs485_sensor_cfg;
extern const sci_uart_extended_cfg_t rs485_sensor_cfg_extend;

#ifndef rs485_sensor_callback
void rs485_sensor_callback(uart_callback_args_t *p_args);
#endif
void hal_entry(void);
void g_hal_init(void);
FSP_FOOTER
#endif /* HAL_DATA_H_ */
