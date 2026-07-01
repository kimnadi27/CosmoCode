#pragma once
/**
 * HDLC/Types.h -- общие типы, константы протокола.
 */

/*
* Применена настройка в 126 байт тк 2 байта резервируется для памяти адаптера
*/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "stm32l4xx_hal.h"

/* --- Debug LED ------------------------------------------------------ */
#ifndef HDLC_DEBUG_LED
#  define HDLC_DEBUG_LED 0
#endif
#define HDLC_LED_BLINK_MS   40u

/* --- Timing --------------------------------------------------------- */
#define HDLC_T_POLL_MS          200u
#define HDLC_T_ENABLE_MS         50u
#define HDLC_TX_TIMEOUT_MS      255u

/* --- Framing -------------------------------------------------------- */
#define HDLC_FLAG           0x7Eu
#define HDLC_ESC            0x7Du
#define HDLC_ESC_XOR        0x20u

/* --- Addresses ------------------------------------------------------ */
#define HDLC_ADDR_INVALID   0x00u
#define HDLC_ADDR_BROADCAST 0xFFu

/* --- Opcodes master->slave ------------------------------------------- */
#define HDLC_CMD_RR_REQ      0x01u
#define HDLC_CMD_TEST        0xF3u
#define HDLC_CMD_DISC        0x53u
#define HDLC_CMD_SNRM        0x83u
#define HDLC_CMD_SARM        0x0Fu
#define HDLC_CMD_UI_NORMAL   0x13u
#define HDLC_CMD_UI_ONE_WAY  0x03u

/* --- Opcodes slave->master ------------------------------------------- */
#define HDLC_CMD_RR_RESP     0x00u
#define HDLC_CMD_UA          0x73u
#define HDLC_CMD_DM          0x1Fu
#define HDLC_CMD_FRMR        0x97u
#define HDLC_CMD_UI_RESP     0x13u
#define HDLC_CMD_UI_ACK      0x23u
#define HDLC_CMD_PUSH        0x33u

/* --- Frame sizes ---------------------------------------------------- */
#define HDLC_CMD_MAX_SIZE       8u
#define HDLC_CRC_SIZE           2u
#define HDLC_FRAME_MIN_BODY     4u
#define HDLC_DMA_RX_BUF_SIZE  126u
#define HDLC_DMA_TX_BUF_SIZE  126u
#define HDLC_FRAME_MAX_SIZE   126u

/* --- UI subcommand -------------------------------------------------- */
#define HDLC_UI_CMD_MAX_BYTES  1u
#define HDLC_UI_HOLDERS_MAX   8u /* Для управления памятью; можно изменить кол-во элементов в таблице в которые будут скопированы ваши UI холдеры */

/* --- DataBuffer ----------------------------------------------------- */
#define HDLC_DATABUF_SIZE     504u

/**
 * HDLC_LINK_BUDGET_BYTES -- суммарная ёмкость внешней шины за одну транзакцию.
 *
 * Адаптер может принять не более этого числа байт за один RR_REQ цикл.
 */
#ifndef HDLC_LINK_BUDGET_BYTES
#  define HDLC_LINK_BUDGET_BYTES   126u
#endif

/** Служебные байты HDLC-фрейма (flag, addr, opcode, crc*2, flag = 6..8 с escaping). */
#define HDLC_LINK_SERVICE_BYTES    8u

/** Максимум полезных данных в одном PUSH-фрейме. */
#define HDLC_LINK_PAYLOAD_MAX  \
    ((HDLC_LINK_BUDGET_BYTES > HDLC_LINK_SERVICE_BYTES) \
     ? (HDLC_LINK_BUDGET_BYTES - HDLC_LINK_SERVICE_BYTES) \
     : 16u)

/**
 * HDLC_PAYLOAD_CHUNK_SIZE -- на сколько байт DataBuffer.h режет большие записи.
 *
 * Можно переопределить перед включением hdlc.h:
 *   #define HDLC_PAYLOAD_CHUNK_SIZE  60
 *
 * Допустимый диапазон: 16..245
 * По умолчанию = HDLC_LINK_PAYLOAD_MAX.
 */
#ifndef HDLC_PAYLOAD_CHUNK_SIZE
#  define HDLC_PAYLOAD_CHUNK_SIZE  HDLC_LINK_PAYLOAD_MAX
#endif

/* Жёсткий clamp (не будет компиляции без этого) */
#if HDLC_PAYLOAD_CHUNK_SIZE < 16
#  error "HDLC_PAYLOAD_CHUNK_SIZE must be >= 16"
#endif
#if HDLC_PAYLOAD_CHUNK_SIZE > 245
#  error "HDLC_PAYLOAD_CHUNK_SIZE must be <= 245"
#endif

/* --- Scheduler ------------------------------------------------------ */
#define HDLC_SCHED_SLOTS             8u
#define HDLC_SCHED_SOFT_DEADLINE_MS  300u
#define HDLC_SCHED_HARD_DEADLINE_MS  1000u
#define HDLC_SCHED_REQUEUE_DELAY_MS  50u
#define HDLC_SCHED_REQUEUE_MAX       5u

/* --- Boot phases ---------------------------------------------------- */
typedef enum {
    HDLC_BOOT_PRE   = 0u,
    HDLC_BOOT_READY = 1u,
} HDLC_BootPhase_t;

/* --- Parsed frame --------------------------------------------------- */
typedef struct {
    uint8_t  addr;
    uint8_t  cmd[HDLC_CMD_MAX_SIZE];
    uint8_t  cmd_len;
    uint8_t *data;
    uint16_t data_len;
} HDLC_Frame_t;

/* --- Status --------------------------------------------------------- */
typedef enum {
    HDLC_OK               =  0,
    HDLC_ERR_NULL         = -1,
    HDLC_ERR_OVERRUN      = -2,
    HDLC_ERR_BAD_LEN      = -3,
    HDLC_ERR_BAD_ADDR     = -4,
    HDLC_ERR_GATE_TIMEOUT = -5,
    HDLC_ERR_TX_TIMEOUT   = -6,
    HDLC_ERR_BUF_FULL     = -7,
    HDLC_ERR_DEADLINE     = -8,
} HDLC_Status_t;

/* --- FRMR flags ----------------------------------------------------- */
#define HDLC_FRMR_UNKNOWN_CMD   (1u << 0u)
#define HDLC_FRMR_BAD_FORMAT    (1u << 1u)
#define HDLC_FRMR_BAD_SEQ       (1u << 3u)

/* --- Forward declarations ------------------------------------------- */
typedef struct HDLC_Handle HDLC_Handle_t;
typedef void (*HDLC_TaskFn_t)(HDLC_Handle_t *h, void *arg);
