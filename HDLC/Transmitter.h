#pragma once
/**
 * Определяет только функции-стабы, которые раскрываются
 * ПОСЛЕ определения struct HDLC_Handle в hdlc.h.
 *
 * Не включать напрямую -- включается автоматически из hdlc.h.
 */

#include "Types.h"
#include "CRC_CCITT.h"
#include <string.h>

/* --- Internal: byte-stuffing ----------------------------------------- */
static inline void _tx_push(uint8_t *buf, uint16_t *pos, uint8_t b)
{
    if (b == HDLC_FLAG || b == HDLC_ESC) {
        buf[(*pos)++] = HDLC_ESC;
        buf[(*pos)++] = b ^ HDLC_ESC_XOR;
    } else {
        buf[(*pos)++] = b;
    }
}

/* --- Internal: сборка stuffed-фрейма в буфер ------------------------ */
static inline uint16_t _tx_build(
    uint8_t       *out,
    uint8_t        slave_addr,
    uint8_t        opcode,
    const uint8_t *payload,
    uint16_t       plen)
{
    if (plen + 8u > HDLC_DMA_TX_BUF_SIZE) return 0u;

    uint8_t  raw[HDLC_DMA_TX_BUF_SIZE];
    uint16_t r = 0u;
    /*
     * length = полный размер frame_buf включая само поле length.
     * Формула: 1(length) + 1(addr) + 1(cmd) + plen + 2(crc) = plen + 5.
     * Симметрично тому что принимает _hdlc_feed / _hdlc_dispatch.
     */
    raw[r++] = (uint8_t)(1u + 1u + 1u + plen + 2u);  /* = plen + 5 */
    raw[r++] = slave_addr;
    raw[r++] = opcode;
    if (payload && plen) { memcpy(&raw[r], payload, plen); r += plen; }
    uint16_t crc = CRC_CCITT(raw, r);
    raw[r++] = (uint8_t)(crc & 0xFFu);
    raw[r++] = (uint8_t)(crc >> 8u);

    uint16_t f = 0u;
    out[f++] = HDLC_FLAG;
    for (uint16_t i = 0u; i < r; i++) _tx_push(out, &f, raw[i]);
    out[f++] = HDLC_FLAG;
    return f;
}

/* --- Internal: DE gate ready? --------------------------------------- */
static inline bool _tx_gate_ready(void)
{
    return HAL_GPIO_ReadPin(RESP_GATE_GPIO_Port, RESP_GATE_Pin) == GPIO_PIN_SET;
}

/*
 * Ниже -- API-функции, использующие struct HDLC_Handle.
 * Определены в hdlc.h ПОСЛЕ struct HDLC_Handle, чтобы
 * избежать incomplete type.
 *
 * Форвард-декларации здесь:
 */
static inline void HDLC_Transmit(
    HDLC_Handle_t *h, uint8_t opcode,
    const uint8_t *payload, uint16_t plen);

static inline void HDLC_Transmitter_Poll(HDLC_Handle_t *h);
static inline void HDLC_Transmitter_TxDone(HDLC_Handle_t *h);
