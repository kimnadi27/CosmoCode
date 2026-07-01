#pragma once
/**
 * ответ на TEST (0xF3) / Запрос адреса.
 *
 * CMD: запроса адреса:
 *   Ответ: тот же опкод 0xF3 + echo payload без изменений.
 *
 * Инициализация шины:
 *   Первый успешный TEST -> initialized = true -> boot_phase READY.
 *   on_periph_enable ставится в планировщик через shim-враппер:
 *   HDLC_LegacyFn_t требует void fn(h, void *arg), а
 *   on_periph_enable имеет void fn(h) -- shim просто игнорирует arg.
 *
 * Работает в ЛЮБОЙ boot-фазе.
 */
#include "../hdlc.h"

/**
 * Shim: адаптер void(h) -> HDLC_LegacyFn_t void(h, void*).
 * Использует on_periph_enable из самого хэндла (arg игнорируется).
 */
static inline void _periph_enable_shim(HDLC_Handle_t *h, void *arg)
{
    (void)arg;
    if (h->on_periph_enable)
        h->on_periph_enable(h);
}

static inline bool Holder_Test(HDLC_Handle_t *h, const HDLC_Frame_t *f)
{
    if (f->cmd[0] != HDLC_CMD_TEST) return false;

    /* Echo */
    HDLC_Transmit(h, HDLC_CMD_TEST, f->data, f->data_len);

    /* Первый TEST -> инициализация шины */
    if (!h->initialized) {
        h->initialized = true;

        if (h->boot_phase == HDLC_BOOT_PRE) {
            h->boot_phase = HDLC_BOOT_READY;
            /*
             * on_periph_enable запускаем через shim:
             * ответ TEST уйдёт по DMA раньше чем сработает периферия.
             */
            if (h->on_periph_enable)
                HDLC_Sched_PostLegacy(&h->scheduler,
                                      _periph_enable_shim,
                                      NULL, 0u,
                                      HDLC_SCHED_POLICY_LEGACY);
        }
    }

    return true;
}
