#pragma once
/**
 * catch-all для неизвестных команд.
 *
 * Протокол (Ошибка при обработке запроса?), опкод 0x97, payload 17 байт:
 *   [0..7]   -- поле команды ошибочного запроса (8 байт)
 *   [8..11]  -- VR(S) << 1 (4 байта LE)
 *   [12..15] -- VR(R) << 1 (4 байта LE)
 *   [16]     -- признаки ошибки (LSB first)
 *               бит 0 -- неизвестная команда
 *               бит 1 -- неверный формат
 *               бит 3 -- неверный номер пакета
 */
#include "../hdlc.h"

static inline bool Holder_Error(HDLC_Handle_t *h, const HDLC_Frame_t *f)
{
    uint8_t body[17u];

    for (uint8_t i = 0u; i < 8u; i++)
        body[i] = (i < f->cmd_len) ? f->cmd[i] : 0u;

    uint32_t vrs_shifted = h->rr_vr_s << 1u;
    body[ 8] = (uint8_t)( vrs_shifted        & 0xFFu);
    body[ 9] = (uint8_t)((vrs_shifted >>  8) & 0xFFu);
    body[10] = (uint8_t)((vrs_shifted >> 16) & 0xFFu);
    body[11] = (uint8_t)((vrs_shifted >> 24) & 0xFFu);

    uint32_t vrr_shifted = h->rr_vr_r << 1u;
    body[12] = (uint8_t)( vrr_shifted        & 0xFFu);
    body[13] = (uint8_t)((vrr_shifted >>  8) & 0xFFu);
    body[14] = (uint8_t)((vrr_shifted >> 16) & 0xFFu);
    body[15] = (uint8_t)((vrr_shifted >> 24) & 0xFFu);

    body[16] = HDLC_FRMR_UNKNOWN_CMD;

    HDLC_Transmit(h, HDLC_CMD_FRMR, body, sizeof(body));
    return true;
}
