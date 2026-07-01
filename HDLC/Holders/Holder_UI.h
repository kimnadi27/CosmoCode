#pragma once
/**
 * диспетчер UI_NORMAL (0x13) и UI_ONE_WAY (0x03).
 *
 * UI_NORMAL: ожидает ответ.
 *   - Если UI-обработчик найден в h->ui_table -- вызывает его.
 *   - Если не найден -- FRMR(UNKNOWN_CMD).
 *   - Если пустой payload -- FRMR(BAD_FORMAT).
 *
 * UI_ONE_WAY: fire&forget, ответа нет.
 *   - Диспетчеризация через ui_table как обычно.
 *   - Если обработчик не найден -- молчим (no FRMR).
 *
 * Фильтрация по boot_phase -- внутри _hdlc_ui_dispatch (hdlc.h).
 */
#include "../hdlc.h"

static inline bool Holder_UI_Normal(HDLC_Handle_t *h, const HDLC_Frame_t *f)
{
    if (f->cmd[0] != HDLC_CMD_UI_NORMAL) return false;
    if (!f->data || !f->data_len) {
        uint8_t frmr = HDLC_FRMR_BAD_FORMAT;
        HDLC_Transmit(h, HDLC_CMD_FRMR, &frmr, 1u);
        return true;
    }
    if (!_hdlc_ui_dispatch(h, f->data, f->data_len)) {
        uint8_t frmr = HDLC_FRMR_UNKNOWN_CMD;
        HDLC_Transmit(h, HDLC_CMD_FRMR, &frmr, 1u);
    }
    return true;
}

static inline bool Holder_UI_OneWay(HDLC_Handle_t *h, const HDLC_Frame_t *f)
{
    if (f->cmd[0] != HDLC_CMD_UI_ONE_WAY) return false;
    if (f->data && f->data_len)
        _hdlc_ui_dispatch(h, f->data, f->data_len);
    return true;
}
