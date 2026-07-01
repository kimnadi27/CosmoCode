#pragma once
/**
 * обработка SNRM (0x83) / SARM (0x0F) / DISC (0x53).
 *
 * SNRM/SARM:
 *   1. Отвечает UA.
 *   2. При первом успешном init переводит boot_phase -> READY
 *      и вызывает on_periph_enable (UA уже в pipeline).
 *
 * DISC:
 *   1. Отвечает UA.
 *   2. Сбрасывает initialized (сессия закрыта).
 *   3. data_buf очищается.
 *   4. boot_phase НЕ сбрасывается -- периферия остаётся включённой.
 */
#include "../hdlc.h"

static inline bool Holder_Init(HDLC_Handle_t *h, const HDLC_Frame_t *f)
{
    switch (f->cmd[0]) {

    case HDLC_CMD_SNRM:
    case HDLC_CMD_SARM:
        h->initialized = true;
        if (h->boot_phase == HDLC_BOOT_PRE) {
            h->boot_phase = HDLC_BOOT_READY;
            HDLC_Transmit(h, HDLC_CMD_UA, NULL, 0u);
            if (h->on_periph_enable) h->on_periph_enable(h);
        } else {
            HDLC_Transmit(h, HDLC_CMD_UA, NULL, 0u);
        }
        return true;

    case HDLC_CMD_DISC:
        HDLC_Transmit(h, HDLC_CMD_UA, NULL, 0u);
        h->initialized = false;
        HDLC_DB_Clear(&h->data_buf);
        return true;

    default:
        return false;
    }
}
