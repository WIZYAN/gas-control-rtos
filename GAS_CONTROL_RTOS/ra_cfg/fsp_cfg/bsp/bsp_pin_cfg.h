/* generated configuration header file - do not edit */
#ifndef BSP_PIN_CFG_H_
#define BSP_PIN_CFG_H_
#include "r_ioport.h"

/* Common macro for FSP header files. There is also a corresponding FSP_FOOTER macro at the end of this file. */
FSP_HEADER

#define GAS_PRE_AD7 (BSP_IO_PORT_00_PIN_00) /* GAS_PRE_AD7 */
#define GAS_PRE_AD6 (BSP_IO_PORT_00_PIN_01) /* GAS_PRE_AD6 */
#define GAS_PRE6_CHANGE (BSP_IO_PORT_00_PIN_02) /* GAS_PRE6_CHANGE */
#define GAS_PRE_AD5 (BSP_IO_PORT_00_PIN_03) /* GAS_PRE_AD5 */
#define GAS_PRE5_CHANGE (BSP_IO_PORT_00_PIN_04) /* GAS_PRE5_CHANGE */
#define GAS_PRE_AD4 (BSP_IO_PORT_00_PIN_05) /* GAS_PRE_AD4 */
#define GAS_PRE4_CHANGE (BSP_IO_PORT_00_PIN_06) /* GAS_PRE4_CHANGE */
#define GAS_PRE_AD3 (BSP_IO_PORT_00_PIN_07) /* GAS_PRE_AD3 */
#define GAS_PRE3_CHANGE (BSP_IO_PORT_00_PIN_08) /* GAS_PRE3_CHANGE */
#define GAS_PRE2_AD2 (BSP_IO_PORT_00_PIN_14) /* GAS_PRE2_AD2 */
#define GAS_PRE2_CHANGE (BSP_IO_PORT_00_PIN_15) /* GAS_PRE2_CHANGE */
#define SCI0_RXD (BSP_IO_PORT_01_PIN_00) /* SCI0_RXD */
#define SCI0_TXD (BSP_IO_PORT_01_PIN_01) /* SCI0_TXD */
#define CAN_RXD0 (BSP_IO_PORT_01_PIN_02) /* CAN_RXD0 */
#define CAN_TXD0 (BSP_IO_PORT_01_PIN_03) /* CAN_TXD0 */
#define SCI0_485RES (BSP_IO_PORT_01_PIN_04) /* SCI0_485RES */
#define SCI0_EN (BSP_IO_PORT_01_PIN_05) /* SCI0_EN */
#define DISP_POWER (BSP_IO_PORT_01_PIN_13) /* DISP_POWER */
#define DISP_EN (BSP_IO_PORT_01_PIN_14) /* DISP_EN */
#define AT24C_WP (BSP_IO_PORT_01_PIN_15) /* AT24C_WP */
#define VAL6 (BSP_IO_PORT_02_PIN_05) /* VAL6 */
#define VAL8 (BSP_IO_PORT_02_PIN_06) /* VAL8 */
#define STATUS (BSP_IO_PORT_03_PIN_01) /* STATUS */
#define VAL_CAL (BSP_IO_PORT_03_PIN_02) /* VAL_CAL */
#define VAL2 (BSP_IO_PORT_03_PIN_03) /* VAL2 */
#define VAL3 (BSP_IO_PORT_03_PIN_04) /* VAL3 */
#define VAL5 (BSP_IO_PORT_03_PIN_05) /* VAL5 */
#define VAL4 (BSP_IO_PORT_03_PIN_06) /* VAL4 */
#define VAL_P2 (BSP_IO_PORT_03_PIN_07) /* VAL_P2 */
#define GAS_PRE7_CHANGE (BSP_IO_PORT_04_PIN_00) /* GAS_PRE_CHANGE */
#define VAL18 (BSP_IO_PORT_04_PIN_01) /* VAL18 */
#define VAL_P6 (BSP_IO_PORT_04_PIN_02) /* VAL_P6 */
#define VAL16 (BSP_IO_PORT_04_PIN_03) /* VAL16 */
#define VAL17 (BSP_IO_PORT_04_PIN_04) /* VAL17 */
#define VAL15 (BSP_IO_PORT_04_PIN_05) /* VAL15 */
#define VAL_P5 (BSP_IO_PORT_04_PIN_06) /* VAL_P5 */
#define VAL7 (BSP_IO_PORT_04_PIN_08) /* VAL7 */
#define VAL_P3 (BSP_IO_PORT_04_PIN_09) /* VAL_P3 */
#define VAL9 (BSP_IO_PORT_04_PIN_10) /* VAL9 */
#define VAL11 (BSP_IO_PORT_04_PIN_11) /* VAL11 */
#define VAL10 (BSP_IO_PORT_04_PIN_12) /* VAL10 */
#define VAL_P4 (BSP_IO_PORT_04_PIN_13) /* VAL_P4 */
#define VAL12 (BSP_IO_PORT_04_PIN_14) /* VAL12 */
#define VAL14 (BSP_IO_PORT_04_PIN_15) /* VAL14 */
#define PRE_RES (BSP_IO_PORT_05_PIN_00) /* PRE_RES */
#define PRE_TXD (BSP_IO_PORT_05_PIN_01) /* PRE_RXD */
#define PRE_RXD (BSP_IO_PORT_05_PIN_02) /* PRE_RXD */
#define PRE_EN (BSP_IO_PORT_05_PIN_03) /* PRE_EN */
#define GAS_PRE1_CHANGE (BSP_IO_PORT_05_PIN_04) /* GAS_PRE1_CHANGE */
#define GAS_PRE_AD1 (BSP_IO_PORT_05_PIN_05) /* GAS_PRE_AD1 */
#define AT24C_SCL (BSP_IO_PORT_06_PIN_08) /* AT24C_SCL */
#define AT24C_SDA (BSP_IO_PORT_06_PIN_09) /* AT24C_SDA */
#define VAL13 (BSP_IO_PORT_07_PIN_08) /* VAL13 */
#define VAL_P1 (BSP_IO_PORT_08_PIN_08) /* VAL_P1 */
#define VAL1 (BSP_IO_PORT_08_PIN_09) /* VAL1 */
extern const ioport_cfg_t g_bsp_pin_cfg; /* R7FA4M1AB3CFP.pincfg */

void BSP_PinConfigSecurityInit();

/* Common macro for FSP header files. There is also a corresponding FSP_HEADER macro at the top of this file. */
FSP_FOOTER

#endif /* BSP_PIN_CFG_H_ */
