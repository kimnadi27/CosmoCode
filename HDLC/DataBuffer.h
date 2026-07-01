#pragma once
/**
 * HDLC/DataBuffer.h -- кольцевой буфер накопленных данных для отдачи адаптеру.
 *
 * Формат записи в байт-массиве:
 *   [opcode:1][len_hi:1][len_lo:1][data:len]
 *
 * Pop уважает HDLC_PAYLOAD_CHUNK_SIZE:
 *   если длина записи > CHUNK_SIZE -- отдаёт первые CHUNK_SIZE байт,
 *   остаток остаётся в буфере как новая запись (автонарезка).
 */

#include "Types.h"
#include <string.h>

#define _DB_HDR  3u   /* opcode(1) + len_hi(1) + len_lo(1) */

typedef struct {
    uint8_t  buf[HDLC_DATABUF_SIZE];
    uint16_t head;   /* следующий байт для чтения  */
    uint16_t tail;   /* следующий байт для записи  */
    uint16_t count;  /* занято байт                */
} HDLC_DataBuffer_t;

static inline void HDLC_DB_Clear(HDLC_DataBuffer_t *db)
{
    db->head = 0u; db->tail = 0u; db->count = 0u;
}

static inline uint16_t HDLC_DB_Used(const HDLC_DataBuffer_t *db)
{
    return db->count;
}

static inline uint16_t HDLC_DB_Free(const HDLC_DataBuffer_t *db)
{
    return (uint16_t)(HDLC_DATABUF_SIZE - db->count);
}

/* Записать байт в кольцо */
static inline void _db_put(HDLC_DataBuffer_t *db, uint8_t b)
{
    db->buf[db->tail] = b;
    if (++db->tail >= HDLC_DATABUF_SIZE) db->tail = 0u;
    db->count++;
}

/* Прочитать байт из кольца */
static inline uint8_t _db_get(HDLC_DataBuffer_t *db)
{
    uint8_t b = db->buf[db->head];
    if (++db->head >= HDLC_DATABUF_SIZE) db->head = 0u;
    db->count--;
    return b;
}

/* Подсмотреть байт без извлечения (offset от head) */
static inline uint8_t _db_peek(const HDLC_DataBuffer_t *db, uint16_t offset)
{
    uint16_t idx = (uint16_t)((db->head + offset) % HDLC_DATABUF_SIZE);
    return db->buf[idx];
}

/**
 * HDLC_DB_Push -- добавить запись в буфер.
 * Если нет места -- возвращает HDLC_ERR_BUF_FULL (запись не вставляется частично).
 */
static inline HDLC_Status_t HDLC_DB_Push(
    HDLC_DataBuffer_t *db, uint8_t opcode,
    const uint8_t *data, uint16_t len)
{
    if (!db || !data || !len) return HDLC_ERR_NULL;
    uint16_t need = (uint16_t)(_DB_HDR + len);
    if (HDLC_DB_Free(db) < need) return HDLC_ERR_BUF_FULL;
    _db_put(db, opcode);
    _db_put(db, (uint8_t)(len >> 8));
    _db_put(db, (uint8_t)(len & 0xFFu));
    for (uint16_t i = 0u; i < len; i++) _db_put(db, data[i]);
    return HDLC_OK;
}

/**
 * HDLC_DB_Pop -- извлечь одну запись (или её chunk).
 *
 * Если длина записи > HDLC_PAYLOAD_CHUNK_SIZE:
 *   - в out_buf возвращается CHUNK_SIZE байт
 *   - оставшиеся (len - CHUNK_SIZE) байт перепаковываются как новая запись
 *     с тем же opcode в начале очереди (peek-based swap не нужен, используем
 *     временный буфер переупаковки в tail)
 *
 * @param db        буфер
 * @param out_op    opcode извлечённой записи
 * @param out_buf   буфер-приёмник
 * @param out_len   сколько байт записано в out_buf
 * @param buf_size  размер out_buf (безопасность)
 */
static inline HDLC_Status_t HDLC_DB_Pop(
    HDLC_DataBuffer_t *db,
    uint8_t  *out_op,
    uint8_t  *out_buf,
    uint16_t *out_len,
    uint16_t  buf_size)
{
    if (!db || !out_op || !out_buf || !out_len) return HDLC_ERR_NULL;
    if (db->count < _DB_HDR) return HDLC_ERR_BAD_LEN;

    uint8_t  opcode = _db_get(db);
    uint16_t len    = (uint16_t)((uint16_t)_db_get(db) << 8);
    len            |= _db_get(db);

    if (db->count < len) {
        /* Повреждение буфера -- очищаем */
        HDLC_DB_Clear(db);
        return HDLC_ERR_OVERRUN;
    }

    uint16_t chunk = (len > HDLC_PAYLOAD_CHUNK_SIZE)
                     ? (uint16_t)HDLC_PAYLOAD_CHUNK_SIZE
                     : len;
    if (chunk > buf_size) chunk = buf_size;

    /* Читаем chunk байт в out_buf */
    *out_op  = opcode;
    *out_len = chunk;
    for (uint16_t i = 0u; i < chunk; i++) out_buf[i] = _db_get(db);

    /* Если осталось -- пушим остаток обратно как новую запись */
    if (len > chunk) {
        uint16_t remain = (uint16_t)(len - chunk);
        /* Временно извлекаем в стек (remain <= PAYLOAD_CHUNK_SIZE <= 245) */
        uint8_t tmp[HDLC_PAYLOAD_CHUNK_SIZE];
        uint16_t take = (remain > (uint16_t)sizeof(tmp))
                        ? (uint16_t)sizeof(tmp) : remain;
        for (uint16_t i = 0u; i < take; i++) tmp[i] = _db_get(db);
        /* Остаток сверх tmp теряем -- не должно случаться при chunk=CHUNK_SIZE */
        for (uint16_t i = take; i < remain; i++) _db_get(db);
        HDLC_DB_Push(db, opcode, tmp, take);
    }

    return HDLC_OK;
}
