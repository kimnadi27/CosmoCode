#pragma once
/**
 * UIHolder_GetData.h
 * Субкоманда: { 0xA2 }  (UI_NORMAL)
 * Фаза: HDLC_BOOT_READY
 *
 * Снимает вес с тензодатчика без движения мотора.
 *
 * Ответ UI_RESP, 5 байт:
 *   [0]    = 0x00 -- FSM в IDLE / 0x01 -- тест идёт (данные предварительные)
 *   [1..4] = float weight_mg, little-endian
 */
#include "../../../HDLC/hdlc.h"
#include "PolyTestFSM.h"

static const uint8_t _get_data_cmd[] = { 0xA2u };

static inline void UIHolder_GetData_Handler(
    HDLC_Handle_t *h, const uint8_t *data, uint16_t len)
{
    (void)data; (void)len;
    uint8_t resp[5];
    resp[0] = PolyTest_IsIdle() ? 0x00u : 0x01u;
    float w = PolyTest_ReadWeightNow();
    __builtin_memcpy(&resp[1], &w, sizeof(float));
    HDLC_Transmit(h, HDLC_CMD_UI_RESP, resp, sizeof(resp));
}

static inline void UIHolder_GetData_Register(HDLC_Handle_t *h)
{
    HDLC_UI_REGISTER(h, _get_data_cmd, 1u, false, HDLC_BOOT_READY,
                     UIHolder_GetData_Handler);
}
