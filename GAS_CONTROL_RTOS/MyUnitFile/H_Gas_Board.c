#include "H_Gas_Board.h"

#include "hal_data.h"

/*
 * 函数名：R_BSP_WarmStart。
 * 说明：响应 FSP 启动阶段事件，在相应阶段启用数据闪存读取并应用板级引脚配置。
 * 输入：event 为当前 BSP 启动阶段枚举值。
 * 输出：无；根据 event 完成对应的启动硬件配置。
 */
void R_BSP_WarmStart(bsp_warm_start_event_t event)
{
    if (BSP_WARM_START_RESET == event)
    {
#if BSP_FEATURE_FLASH_LP_VERSION != 0
        R_FACI_LP->DFLCTL = 1U;
#endif
    }

    if (BSP_WARM_START_POST_C == event)
    {
        (void) R_IOPORT_Open(&g_ioport_ctrl, g_ioport.p_cfg);
        (void) R_IOPORT_PinWrite(&g_ioport_ctrl, SCI0_485RES, BSP_IO_LEVEL_HIGH);
        (void) R_IOPORT_PinWrite(&g_ioport_ctrl, PRE_RES, BSP_IO_LEVEL_HIGH);
        // 内外两路 RS485 均使用高电平使能 120 Ω 匹配电阻，方向控制由各自的 EN 信号独立完成。
    }
}
