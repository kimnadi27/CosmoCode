#pragma once
/**
 * UIHolder_ForceRun.h
 * Субкоманда: { 0xA1 }  (UI_NORMAL)
 * Фаза: HDLC_BOOT_READY
 *
 * Ответ UI_RESP, 1 байт:
 *   0x00 -- тест запущен
 *   0x01 -- тест уже выполняется
 */
#include "../../../HDLC/hdlc.h"
#include "PolyTestFSM.h"

static const uint8_t _force_run_cmd[] = { 0xA1u };

static inline void UIHolder_ForceRun_Handler(
    HDLC_Handle_t *h, const uint8_t *data, uint16_t len)
{
    (void)data; (void)len;
    uint8_t status;
    if (PolyTest_IsIdle()) { PolyTest_ForceStart(); status = 0x00u; }
    else                   { status = 0x01u; }
    HDLC_Transmit(h, HDLC_CMD_UI_RESP, &status, 1u);
}

static inline void UIHolder_ForceRun_Register(HDLC_Handle_t *h)
{
    HDLC_UI_REGISTER(h, _force_run_cmd, 1u, false, HDLC_BOOT_READY,
                     UIHolder_ForceRun_Handler);
}
