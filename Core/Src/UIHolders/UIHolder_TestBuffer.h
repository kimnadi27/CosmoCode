#pragma once
/**
 * тестовое заполнение RR-буфера.
 * Субкоманда: {0xF5, data...}
 * Фаза: BOOT_PRE (всегда доступна)
 *
 * Все байты после 0xF5 помещаются в DataBuffer как один пакет
 * с опкодом HDLC_CMD_DATA (0x10). После этого мастер может
 * запросить их через RR (0x100000001).
 *
 * Ответ UI_RESP (0x13):
 *   [0] = 0x00 -- успех
 *   [0] = 0x01 -- данных нет (пустой payload)
 *   [0] = 0x02 -- буфер переполнен
 */
#include "../../../HDLC/hdlc.h"

static const uint8_t _tbuf_cmd[] = { 0xF5u };

static inline void UIHolder_TestBuffer_Handler(
    HDLC_Handle_t *h, const uint8_t *data, uint16_t len)
{
    uint8_t status;

    if (len == 0u) {
        status = 0x01u;
        HDLC_Transmit(h, HDLC_CMD_UI_RESP, &status, 1u);
        return;
    }

    HDLC_Status_t r = HDLC_Push(h, 0x10u, data, len);
    status = (r == HDLC_OK) ? 0x00u : 0x02u;
    HDLC_Transmit(h, HDLC_CMD_UI_RESP, &status, 1u);
}

static inline void UIHolder_TestBuffer_Register(HDLC_Handle_t *h)
{
    HDLC_UI_REGISTER(h, _tbuf_cmd, 1u, false, HDLC_BOOT_PRE,
                     UIHolder_TestBuffer_Handler);
}
