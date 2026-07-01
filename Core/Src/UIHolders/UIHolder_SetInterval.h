#pragma once
/**
 * UIHolder_SetInterval.h
 * Субкоманда: { 0xA3, b0, b1, b2, b3 }  (UI_NORMAL)
 * Фаза: HDLC_BOOT_READY
 *
 * b0..b3 = uint32_t LE, новый интервал в мс.
 * Clamp: [500 .. 3 600 000] мс.
 *
 * Ответ UI_RESP, 5 байт:
 *   [0]    = 0x00 OK / 0x01 неверная длина / 0x02 применён clamp
 *   [1..4] = uint32_t применённый интервал, LE
 */
#include "../../../HDLC/hdlc.h"
#include "../../../Core/Inc/Utils.h"
#include "PolyTestFSM.h"

#define _SET_INTERVAL_MIN_MS   500u
#define _SET_INTERVAL_MAX_MS   3600000u

static const uint8_t _set_interval_cmd[] = { 0xA3u };

static inline void UIHolder_SetInterval_Handler(
    HDLC_Handle_t *h, const uint8_t *data, uint16_t len)
{
    uint8_t resp[5];

    if (len != 4u) {
        resp[0] = 0x01u;
        uint32_t cur = PolyTest_GetInterval();
        __builtin_memcpy(&resp[1], &cur, 4u);
        HDLC_Transmit(h, HDLC_CMD_UI_RESP, resp, sizeof(resp));
        return;
    }

    uint32_t req = ((uint32_t)data[0])       |
                   ((uint32_t)data[1] <<  8)  |
                   ((uint32_t)data[2] << 16)  |
                   ((uint32_t)data[3] << 24);

    uint32_t clamped = UTIL_CLAMP(req, _SET_INTERVAL_MIN_MS, _SET_INTERVAL_MAX_MS);
    resp[0] = (clamped != req) ? 0x02u : 0x00u;
    PolyTest_SetInterval(clamped);
    __builtin_memcpy(&resp[1], &clamped, 4u);
    HDLC_Transmit(h, HDLC_CMD_UI_RESP, resp, sizeof(resp));
}

static inline void UIHolder_SetInterval_Register(HDLC_Handle_t *h)
{
    HDLC_UI_REGISTER(h, _set_interval_cmd, 1u, false, HDLC_BOOT_READY,
                     UIHolder_SetInterval_Handler);
}
