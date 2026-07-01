#pragma once
/**
 * Poryadok include v main.c:
 *   #include "hdlc.h"
 *   #include "UIHolder_Stats.h"
 *   #include "Holders.h"   // POSLEDNIM
 *
 * Kolbeki:
 *   on_periph_enable -- vyzivaetsya ODIN RAZ posle pervogo uspeshnogo TEST.
 *   on_user_loop     -- vyzivaetsya KAZHDIY tik HDLC_Poll poka initialized.
 *                       Mozhno zadat cherez HDLC_SetUserLoop() posle Init.
 *
 * Optsionalnie define PERED pervim include:
 *   #define HDLC_DEBUG_LED          1
 *   #define HDLC_LINK_BUDGET_BYTES  96
 *   #define HDLC_PAYLOAD_CHUNK_SIZE 88
 */

#include "Types.h"
#include "CRC_CCITT.h"
#include "Transmitter.h"
#include "DataBuffer.h"
#include "Tasker.h"
#include <string.h>

typedef struct {
    uint8_t  cmd[HDLC_UI_CMD_MAX_BYTES];
    uint8_t  cmd_len;
    bool     one_way;
    HDLC_BootPhase_t min_phase;
    void (*handler)(HDLC_Handle_t *h, const uint8_t *data, uint16_t len);
} HDLC_UIHolder_t;

struct HDLC_Handle {
    UART_HandleTypeDef *huart;

    /* RX DMA ring */
    uint8_t  dma_buf[HDLC_DMA_RX_BUF_SIZE];
    uint16_t rx_tail;

    /* RX assembler */
    uint8_t  frame_buf[HDLC_FRAME_MAX_SIZE];
    uint16_t frame_len;
    bool     in_frame;
    bool     escaped;
    uint8_t  frame_expect;

    /* TX */
    uint8_t  tx_buf[HDLC_DMA_TX_BUF_SIZE];
    uint16_t tx_len;
    volatile bool tx_busy;
    bool     tx_pending;
    uint32_t tx_pending_ts;

    /* DataBuffer */
    HDLC_DataBuffer_t data_buf;
    uint16_t          push_chunk_size;

    /* Planirovshik */
    HDLC_Scheduler_t  scheduler;

    /* Config */
    uint8_t          slave_addr;
    HDLC_BootPhase_t boot_phase;
    bool             initialized;

    /** Vyzivaetsya odin raz posle pervogo TEST. Initializatsiya periferii. */
    void (*on_periph_enable)(struct HDLC_Handle *h);

    /** Vyzivaetsya kazhdiy tik HDLC_Poll poka initialized. Polzovatelskiy kod. */
    void (*on_user_loop)(struct HDLC_Handle *h);

    /* UI registry */
    HDLC_UIHolder_t  ui_table[HDLC_UI_HOLDERS_MAX];
    uint8_t          ui_count;

    /* Stats RX */
    uint32_t rx_ok;
    uint32_t rx_bad_addr;
    uint32_t rx_bad_len;
    uint32_t rx_bad_crc;
    uint32_t rx_overrun;

    /* Stats TX */
    uint32_t tx_ok_count;
    uint32_t tx_timeout_count;

    /* RR sequence counters (ispolzuyutsya Holder_RR i Holder_Error) */
    uint32_t rr_vr_s;   /* VR(S): nomer sleduyushego otpravlyaemogo paketa */
    uint32_t rr_vr_r;   /* VR(R): nomer ozhidaemogo ot mastera */
    uint8_t  rr_first;  /* flag pervogo RR-zaprosa */

#if HDLC_DEBUG_LED
    uint32_t led_off_tick;
#endif
};

/*
 * _tx_fire -- zagruzhaet h->tx_buf/tx_len v DMA ili stavit pending.
 * Vyzivaetsya iz HDLC_Transmit, HDLC_Transmit8, HDLC_Transmitter_Poll.
 * Centralizuet logiku: ne dubling v treх mestakh.
 */
static inline void _tx_fire(HDLC_Handle_t *h)
{
    if (_tx_gate_ready() && !h->tx_busy) {
        h->tx_busy       = true;
        h->tx_pending    = false;
        HAL_UART_Transmit_DMA(h->huart, h->tx_buf, h->tx_len);
    } else {
        h->tx_pending    = true;
        h->tx_pending_ts = HAL_GetTick();
    }
}

static inline void HDLC_Transmit(
    HDLC_Handle_t *h, uint8_t opcode,
    const uint8_t *payload, uint16_t plen)
{
    uint16_t len = _tx_build(h->tx_buf, h->slave_addr, opcode, payload, plen);
    if (!len) return;
    h->tx_len = len;
    _tx_fire(h);
}

static inline void HDLC_Transmit8(
    HDLC_Handle_t *h, const uint8_t cmd8[8],
    const uint8_t *payload, uint16_t plen)
{
    if (plen + 16u > HDLC_DMA_TX_BUF_SIZE) return;
    uint8_t  raw[HDLC_DMA_TX_BUF_SIZE];
    uint16_t r = 0u;
    raw[r++] = (uint8_t)(1u + 1u + 8u + plen + 2u);
    raw[r++] = h->slave_addr;
    for (uint8_t i = 0u; i < 8u; i++) raw[r++] = cmd8[i];
    if (payload && plen) { memcpy(&raw[r], payload, plen); r += plen; }
    uint16_t crc = CRC_CCITT(raw, r);
    raw[r++] = (uint8_t)(crc & 0xFFu);
    raw[r++] = (uint8_t)(crc >> 8u);
    uint16_t f = 0u;
    h->tx_buf[f++] = HDLC_FLAG;
    for (uint16_t i = 0u; i < r; i++) _tx_push(h->tx_buf, &f, raw[i]);
    h->tx_buf[f++] = HDLC_FLAG;
    h->tx_len = f;
    _tx_fire(h);
}

static inline void HDLC_Transmitter_Poll(HDLC_Handle_t *h)
{
    if (!h->tx_pending) return;
    if ((HAL_GetTick() - h->tx_pending_ts) >= HDLC_TX_TIMEOUT_MS) {
        h->tx_pending = false;
        h->tx_timeout_count++;
        return;
    }
    if (_tx_gate_ready() && !h->tx_busy) {
        h->tx_busy    = true;
        h->tx_pending = false;
        HAL_UART_Transmit_DMA(h->huart, h->tx_buf, h->tx_len);
    }
}

static inline void HDLC_Transmitter_TxDone(HDLC_Handle_t *h)
    { h->tx_busy = false; h->tx_ok_count++; }

static inline HDLC_Status_t HDLC_Push(
    HDLC_Handle_t *h, uint8_t opcode,
    const uint8_t *data, uint16_t len)
    { return HDLC_DB_Push(&h->data_buf, opcode, data, len); }

static inline void HDLC_SetPushChunkSize(HDLC_Handle_t *h, uint16_t size)
{
    if (size < 16u)  size = 16u;
    if (size > 245u) size = 245u;
    h->push_chunk_size = size;
}

/*
 * HDLC_FlushOne / HDLC_FlushOne_WithCmd
 *
 * tmp -- stekovy bufer (ne static!).
 * static uint8_t tmp[] byl by odin globalny bufer na vse vyzovy --
 * pri reentrantnom vyzove (iz zadachi plannirovshhika + osnovnogo tsikla)
 * bufer molcha perepisyvalsya by.
 */
static inline bool HDLC_FlushOne(HDLC_Handle_t *h)
{
    uint8_t  opcode;
    uint8_t  tmp[HDLC_DMA_TX_BUF_SIZE - 8u];
    uint16_t len;
    uint16_t chunk = h->push_chunk_size
                     ? h->push_chunk_size
                     : (uint16_t)HDLC_PAYLOAD_CHUNK_SIZE;
    uint16_t take  = (chunk < (uint16_t)sizeof(tmp))
                     ? chunk
                     : (uint16_t)sizeof(tmp);
    if (HDLC_DB_Pop(&h->data_buf, &opcode, tmp, &len, take) != HDLC_OK)
        return false;
    HDLC_Transmit(h, opcode, tmp, len);
    return true;
}

static inline bool HDLC_FlushOne_WithCmd(
    HDLC_Handle_t *h, const uint8_t cmd8[8])
{
    uint8_t  opcode;
    uint8_t  tmp[HDLC_DMA_TX_BUF_SIZE - 16u];
    uint16_t len;
    uint16_t chunk = h->push_chunk_size
                     ? h->push_chunk_size
                     : (uint16_t)HDLC_PAYLOAD_CHUNK_SIZE;
    uint16_t take  = (chunk < (uint16_t)sizeof(tmp))
                     ? chunk
                     : (uint16_t)sizeof(tmp);
    if (HDLC_DB_Pop(&h->data_buf, &opcode, tmp, &len, take) != HDLC_OK)
        return false;
    HDLC_Transmit8(h, cmd8, tmp, len);
    return true;
}

static inline void HDLC_UI_Register(
    HDLC_Handle_t *h, const uint8_t *cmd, uint8_t cmd_len,
    bool one_way, HDLC_BootPhase_t min_phase,
    void (*handler)(HDLC_Handle_t *, const uint8_t *, uint16_t))
{
    if (h->ui_count >= HDLC_UI_HOLDERS_MAX ||
        !cmd_len || cmd_len > HDLC_UI_CMD_MAX_BYTES) return;
    HDLC_UIHolder_t *e = &h->ui_table[h->ui_count++];
    for (uint8_t i = 0u; i < cmd_len; i++) e->cmd[i] = cmd[i];
    e->cmd_len   = cmd_len;
    e->one_way   = one_way;
    e->min_phase = min_phase;
    e->handler   = handler;
}

#define HDLC_UI_REGISTER(h, cmd_arr, cmd_len_, one_way_, phase_, fn_) \
    HDLC_UI_Register((h),(cmd_arr),(cmd_len_),(one_way_),(phase_),(fn_))

static inline void HDLC_SetUserLoop(
    HDLC_Handle_t *h, void (*fn)(HDLC_Handle_t *))
    { h->on_user_loop = fn; }

static inline bool Holders_Dispatch(HDLC_Handle_t *h, const HDLC_Frame_t *f);

#if HDLC_DEBUG_LED
static inline void _hdlc_led_on(HDLC_Handle_t *h) {
    HAL_GPIO_WritePin(Led_GPIO_Port, Led_Pin, GPIO_PIN_SET);
    h->led_off_tick = HAL_GetTick() + HDLC_LED_BLINK_MS;
}
static inline void _hdlc_led_tick(HDLC_Handle_t *h) {
    if (h->led_off_tick && HAL_GetTick() >= h->led_off_tick) {
        HAL_GPIO_WritePin(Led_GPIO_Port, Led_Pin, GPIO_PIN_RESET);
        h->led_off_tick = 0u;
    }
}
#else
#  define _hdlc_led_on(h)   ((void)(h))
#  define _hdlc_led_tick(h) ((void)(h))
#endif

static inline bool _hdlc_ui_dispatch(
    HDLC_Handle_t *h, const uint8_t *data, uint16_t dlen)
{
    for (uint8_t i = 0u; i < h->ui_count; i++) {
        HDLC_UIHolder_t *e = &h->ui_table[i];
        if (dlen < e->cmd_len) continue;
        if (e->min_phase > h->boot_phase) continue;
        bool match = true;
        for (uint8_t j = 0u; j < e->cmd_len; j++)
            if (data[j] != e->cmd[j]) { match = false; break; }
        if (!match) continue;
        e->handler(h, data + e->cmd_len, (uint16_t)(dlen - e->cmd_len));
        return true;
    }
    return false;
}

static inline void _hdlc_dispatch(HDLC_Handle_t *h)
{
    uint8_t *fb = h->frame_buf;
    uint16_t n  = h->frame_len;
    h->frame_len = 0u; h->frame_expect = 0u;
    h->in_frame  = false; h->escaped = false;

    if (n < (HDLC_FRAME_MIN_BODY + 1u)) { h->rx_bad_len++; return; }
    if (fb[0] != (uint8_t)n)            { h->rx_bad_len++; return; }

    uint8_t addr = fb[1];
    if (addr != h->slave_addr && addr != HDLC_ADDR_BROADCAST)
        { h->rx_bad_addr++; return; }
    if (!CRC_CCITT_Check(fb, n)) { h->rx_bad_crc++; return; }

    uint8_t cmd_len = ((fb[2] & 0x01u) && (fb[0] >= 12u)) ? 8u : 1u;
    if (fb[0] < (uint8_t)(1u + 1u + cmd_len + 2u)) { h->rx_bad_len++; return; }

    HDLC_Frame_t f;
    f.addr    = addr;
    f.cmd_len = cmd_len;
    for (uint8_t i = 0u; i < cmd_len; i++) f.cmd[i] = fb[2u + i];

    uint8_t overhead = (uint8_t)(1u + 1u + cmd_len + 2u);
    uint8_t dlen = (fb[0] > overhead) ? (uint8_t)(fb[0] - overhead) : 0u;
    f.data_len = dlen;
    f.data     = dlen ? &fb[2u + cmd_len] : NULL;

    h->rx_ok++;
    _hdlc_led_on(h);

    Holders_Dispatch(h, &f);
}

static inline void _hdlc_feed(HDLC_Handle_t *h, uint8_t b)
{
    if (!h->in_frame) {
        if (b == HDLC_FLAG)
            { h->in_frame = true; h->frame_len = 0u; h->frame_expect = 0u; h->escaped = false; }
        return;
    }
    if (b == HDLC_FLAG && !h->escaped) {
        if (!h->frame_len) return;
        if (h->frame_len == h->frame_expect) _hdlc_dispatch(h);
        else { h->rx_bad_len++; h->frame_len=0u; h->frame_expect=0u; h->escaped=false; }
        return;
    }
    if (h->escaped)         { b ^= HDLC_ESC_XOR; h->escaped = false; }
    else if (b == HDLC_ESC) { h->escaped = true;  return; }
    if (h->frame_len >= HDLC_FRAME_MAX_SIZE) {
        h->rx_overrun++; h->in_frame=false;
        h->frame_len=0u; h->frame_expect=0u; h->escaped=false;
        return;
    }
    if (h->frame_len == 0u) {
        if (b < HDLC_FRAME_MIN_BODY)
            { h->rx_bad_len++; h->in_frame=false; h->escaped=false; return; }
        h->frame_expect = b;
    }
    h->frame_buf[h->frame_len++] = b;
}

/**
 * HDLC_Init
 * VAZHNO: vizivat DO lyubyx UIHolder_*_Register().
 * on_periph_enable -- Otvechaet za initializatsiyu ostalnoj periferii
 * on_user_loop -- polzovatelskiy kod chto krutitsya v osnovnom while bez nablyudeniya so storony tasker
 */
static inline HAL_StatusTypeDef HDLC_Init(
    HDLC_Handle_t      *h,
    UART_HandleTypeDef *huart,
    uint8_t             slave_addr,
    void (*on_periph_enable)(HDLC_Handle_t *),
    void (*on_user_loop)(HDLC_Handle_t *))
{
    memset(h, 0, sizeof(*h));
    h->huart            = huart;
    h->slave_addr       = slave_addr;
    h->boot_phase       = HDLC_BOOT_PRE;
    h->on_periph_enable = on_periph_enable;
    h->on_user_loop     = on_user_loop;
    h->rr_first         = 1u;
    return HAL_UART_Receive_DMA(huart, h->dma_buf, HDLC_DMA_RX_BUF_SIZE);
}

static inline void HDLC_Poll(HDLC_Handle_t *h)
{
    _hdlc_led_tick(h);
    HDLC_Transmitter_Poll(h);
    HDLC_Sched_Tick(&h->scheduler, h);

    if (h->initialized && h->on_user_loop)
        h->on_user_loop(h);

    uint16_t head = (uint16_t)(
        HDLC_DMA_RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(h->huart->hdmarx));
    while (h->rx_tail != head) {
        _hdlc_feed(h, h->dma_buf[h->rx_tail]);
        if (++h->rx_tail >= HDLC_DMA_RX_BUF_SIZE) h->rx_tail = 0u;
        head = (uint16_t)(
            HDLC_DMA_RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(h->huart->hdmarx));
    }
}
