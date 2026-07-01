#pragma once
/**
 * общие утилиты, не зависят от протокола.
 *
 * Правило: этот файл НЕ знает ни про HDLC_Handle_t, ни про протокол.
 * Подключать можно в любом месте прошивки.
 */

#include <stdint.h>
#include <stdbool.h>
#include "stm32l4xx_hal.h"

/* =======================================================================
 * HDLC_Timer_t -- неблокирующий таймер
 *
 * Пример:
 *   HDLC_Timer_t t;
 *   HDLC_Timer_Start(&t, 500, true);   // каждые 500 мс, повторять
 *   // в цикле:
 *   if (HDLC_Timer_Tick(&t)) { ... }   // тело срабатывает раз в 500 мс
 * ======================================================================= */
typedef struct {
    uint32_t start_ms;
    uint32_t period_ms;
    bool     running;
    bool     repeat;
    bool     fired;
} HDLC_Timer_t;

static inline void HDLC_Timer_Start(HDLC_Timer_t *t, uint32_t ms, bool repeat)
{
    t->start_ms  = HAL_GetTick();
    t->period_ms = ms;
    t->running   = true;
    t->repeat    = repeat;
    t->fired     = false;
}

static inline void HDLC_Timer_Stop(HDLC_Timer_t *t)
{
    t->running = false;
    t->fired   = false;
}

static inline void HDLC_Timer_Reset(HDLC_Timer_t *t)
{
    t->start_ms = HAL_GetTick();
    t->fired    = false;
}

/**
 * Вернёт true один раз при срабатывании (или каждый период если repeat).
 * Не блокирует.
 */
static inline bool HDLC_Timer_Tick(HDLC_Timer_t *t)
{
    if (!t->running) return false;
    if ((HAL_GetTick() - t->start_ms) < t->period_ms) return false;
    t->fired = true;
    if (t->repeat) t->start_ms = HAL_GetTick();
    else           t->running  = false;
    return true;
}

static inline bool HDLC_Timer_Elapsed(const HDLC_Timer_t *t) { return t->fired; }
static inline bool HDLC_Timer_Running(const HDLC_Timer_t *t) { return t->running; }

/** Сколько мс прошло с момента старта. */
static inline uint32_t HDLC_Timer_ElapsedMs(const HDLC_Timer_t *t)
{
    return t->running ? (HAL_GetTick() - t->start_ms) : 0u;
}

/* =======================================================================
 * Мелкие хелперы
 * ======================================================================= */

#define UTIL_CLAMP(x, lo, hi)  ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define UTIL_MIN(a, b)         ((a) < (b) ? (a) : (b))
#define UTIL_MAX(a, b)         ((a) > (b) ? (a) : (b))
#define UTIL_ARRAY_LEN(arr)    (sizeof(arr) / sizeof((arr)[0]))
