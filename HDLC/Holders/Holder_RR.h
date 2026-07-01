#pragma once
/**
 * обработка запроса накопленных данных (RR, 8-байтный cmd).
 *
 * Логика счётчика (= референсная реализация):
 *
 *   1. Первый запрос: синхронизируем rr_vr_s = N(R).
 *   2. N(R) != rr_vr_s: rr_vr_s = (rr_vr_s + 1) & MAX, отвечаем с этим N(S).
 *   3. N(R) == rr_vr_s: отдаём N(S) = rr_vr_s, после отправки rr_vr_s++.
 *   4. Пустой буфер: RR_RESP с пустым payload, счётчик не меняется.
 *   5. FRMR только при data_len != 0 в запросе.
 */
#include "../hdlc.h"

#define _RR_MAX_INDEX  0x7FFFFFFFu

static inline bool Holder_RR(HDLC_Handle_t *h, const HDLC_Frame_t *f)
{
    if (f->cmd_len < 8u) return false;
    uint64_t raw = 0u;
    for (uint8_t i = 0u; i < 8u; i++)
        raw |= ((uint64_t)f->cmd[i]) << (i * 8u);
    if ((raw & 0x100000001ULL) != 0x100000001ULL) return false;

    if (f->data_len != 0u) {
        uint8_t frmr = HDLC_FRMR_BAD_FORMAT;
        HDLC_Transmit(h, HDLC_CMD_FRMR, &frmr, 1u);
        return true;
    }

    uint32_t nr = (uint32_t)((raw >> 33u) & _RR_MAX_INDEX);

    if (h->rr_first) {
        h->rr_vr_s  = nr;
        h->rr_first = 0u;
    } else if (nr != h->rr_vr_s) {
        h->rr_vr_s = (h->rr_vr_s + 1u) & _RR_MAX_INDEX;
    }

    uint64_t resp_raw = 0x100000000ULL
                       | ((uint64_t)h->rr_vr_r << 33u)
                       | ((uint64_t)h->rr_vr_s <<  1u);
    uint8_t cmd8[8u];
    for (uint8_t i = 0u; i < 8u; i++)
        cmd8[i] = (uint8_t)((resp_raw >> (i * 8u)) & 0xFFu);

    bool had_data = HDLC_FlushOne_WithCmd(h, cmd8);
    if (!had_data) {
        HDLC_Transmit8(h, cmd8, NULL, 0u);
    } else {
        h->rr_vr_s = (h->rr_vr_s + 1u) & _RR_MAX_INDEX;
    }

    return true;
}
