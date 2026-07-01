#pragma once
/**
 * статистика слейва
 * Субкоманда: {0xF4}, фаза: BOOT_PRE (всегда доступна)
 *
 * Ответ UI_RESP, 32 байта LE:
 *  [0..3]  rx_ok          [4..7]  rx_bad_crc
 *  [8..11] rx_bad_len    [12..15] rx_bad_addr
 * [16..19] rx_overrun    [20..23] tx_ok
 * [24..27] tx_timeout    [28..29] ui_count (u16)
 * [30]     sched_used    [31]     tick_ms/100 mod 256 (грубый индикатор аптайма)
 */
#include "../../../HDLC/hdlc.h"

static const uint8_t _stats_cmd[] = { 0xF4u };

static inline void UIHolder_Stats_Handler(
    HDLC_Handle_t *h, const uint8_t *data, uint16_t len)
{
    (void)data; (void)len;

    /* Считаем занятые слоты планировщика */
    uint8_t sched_used = 0u;
    for (uint8_t i = 0u; i < HDLC_SCHED_SLOTS; i++)
        if (h->scheduler.slots[i].used) sched_used++;

    uint32_t fields[7] = {
        h->rx_ok,
        h->rx_bad_crc,
        h->rx_bad_len,
        h->rx_bad_addr,
        h->rx_overrun,
        h->tx_ok_count,
        h->tx_timeout_count,
    };

    uint8_t resp[32];
    /* [0..27] -- 7x u32 LE */
    for (uint8_t i = 0u; i < 7u; i++) {
        resp[i*4u+0u] = (uint8_t)( fields[i]        & 0xFFu);
        resp[i*4u+1u] = (uint8_t)((fields[i] >>  8) & 0xFFu);
        resp[i*4u+2u] = (uint8_t)((fields[i] >> 16) & 0xFFu);
        resp[i*4u+3u] = (uint8_t)((fields[i] >> 24) & 0xFFu);
    }
    /* [28..29] ui_count u16 LE */
    resp[28] = (uint8_t)( h->ui_count       & 0xFFu);
    resp[29] = (uint8_t)((h->ui_count >> 8) & 0xFFu);
    /* [30] sched_used */
    resp[30] = sched_used;
    /* [31] tick / 100 mod 256 (грубый индикатор аптайма, ~25.6 сек цикл) */
    resp[31] = (uint8_t)((HAL_GetTick() / 100u) & 0xFFu);

    HDLC_Transmit(h, HDLC_CMD_UI_RESP, resp, sizeof(resp));
}

static inline void UIHolder_Stats_Register(HDLC_Handle_t *h)
{
    HDLC_UI_REGISTER(h, _stats_cmd, 1u, false, HDLC_BOOT_PRE,
                     UIHolder_Stats_Handler);
}
