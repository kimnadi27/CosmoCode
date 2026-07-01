#pragma once
/**
 * HDLC/Tasker.h -- кооперативный планировщик задач для HDLC-слейва.
 *
 * +================================================================================+
 * |  ВАЖНО: задачи КООПЕРАТИВНЫЕ -- MCU без RTOS не прервёт зависший               |
 * |  while(1) или HAL_Delay() внутри callback.                                     |
 * |                                                                                |
 * |  Защита планировщика = диагностика и drop, а не прерывание.                    |
 * |  Если твой код блокирует -- счётчик overrun_count вырастет,                    |
 * |  а UIHolder_Stats это покажет адаптеру. Но протокол всё равно                  |
 * |  зависнет пока callback не вернёт управление.                                  |
 * |                                                                                |
 * |  Правило для пишущих задачи:                                                   |
 * |    1. HAL_Delay() внутри задачи -- ЗАПРЕЩЕНО.                                  |
 * |    2. Длинные операции -- делить на шаги (HDLC_TaskStepFn_t).                  |
 * |    3. Задача не должна вызывать HDLC_Sched_Post рекурсивно                     |
 * |       без ограничения глубины.                                                 |
 * +================================================================================+
 *
 * Типы задач:
 *  LEGACY   -- старый стиль: void fn(handle, arg). Быстро, но без защиты.
 *  STEP     -- кооперативная задача возвращает DONE/YIELD/RETRY/FAIL.
 *             Планировщик выполняет max_steps_per_tick шагов за тик,
 *             затем откладывает на следующий вызов HDLC_Poll.
 *
 * Политики при просрочке soft_deadline:
 *  HARD_KILL    -- задача дропается немедленно. overrun_count++.
 *  SOFT_DEGRADE -- max_steps_per_tick снижается до 1, добавляется backoff.
 *                 Задача продолжает работать, но медленнее.
 *  REQUEUE      -- задача откладывается на HDLC_SCHED_REQUEUE_DELAY_MS.
 *                 Повторяется до hard_deadline, потом KILL.
 *  LEGACY       -- политика не применяется, callback вызывается как есть.
 */

#include "Types.h"

/* -- Результат шага кооперативной задачи -- */
typedef enum {
    HDLC_TASK_DONE  = 0,  /* задача завершена успешно, слот освобождается  */
    HDLC_TASK_YIELD = 1,  /* нужен ещё один тик, продолжим позже           */
    HDLC_TASK_RETRY = 2,  /* ошибка, повторить с задержкой REQUEUE_DELAY   */
    HDLC_TASK_FAIL  = 3,  /* ошибка, слот освобождается, fail_count++      */
} HDLC_TaskResult_t;

/* -- Тип callback -- */
typedef void               (*HDLC_LegacyFn_t)(HDLC_Handle_t *h, void *arg);
typedef HDLC_TaskResult_t  (*HDLC_TaskStepFn_t)(HDLC_Handle_t *h, void *ctx);

/* -- Политика при просрочке soft_deadline -- */
typedef enum {
    HDLC_SCHED_POLICY_HARD_KILL    = 0, /* просрочка = немедленный drop         */
    HDLC_SCHED_POLICY_SOFT_DEGRADE = 1, /* просрочка = замедлить (backoff)      */
    HDLC_SCHED_POLICY_REQUEUE      = 2, /* просрочка = отложить и повторить     */
    HDLC_SCHED_POLICY_LEGACY       = 3, /* legacy callback, политика N/A        */
} HDLC_SchedPolicy_t;

/* -- Тип задачи -- */
typedef enum {
    HDLC_TASK_TYPE_LEGACY = 0,
    HDLC_TASK_TYPE_STEP   = 1,
} HDLC_TaskType_t;

/* -- Слот задачи -- */
typedef struct {
    HDLC_TaskType_t   type;
    HDLC_SchedPolicy_t policy;
    union {
        HDLC_LegacyFn_t  legacy;
        HDLC_TaskStepFn_t step;
    } fn;
    void        *arg;
    uint32_t     run_after;        /* abs tick: не запускать раньше            */
    uint32_t     soft_deadline;    /* abs tick: после -- применить политику     */
    uint32_t     hard_deadline;    /* abs tick: после -- всегда KILL            */
    uint8_t      max_steps;        /* шагов за один тик (только STEP)         */
    uint8_t      degraded_steps;   /* текущий лимит шагов (уменьшается)       */
    uint8_t      requeue_count;    /* сколько раз уже ставили заново           */
    bool         used;
} HDLC_SchedSlot_t;

/* -- Статистика планировщика -- */
typedef struct {
    uint32_t overrun_count;  /* задач убито по hard_deadline                */
    uint32_t drop_count;     /* задач дропнуто (нет слота)                  */
    uint32_t fail_count;     /* задач завершились с TASK_FAIL               */
    uint32_t degrade_count;  /* раз применялась политика SOFT_DEGRADE       */
    uint32_t requeue_count;  /* раз задачи ставились заново (REQUEUE)       */
} HDLC_SchedStats_t;

typedef struct {
    HDLC_SchedSlot_t  slots[HDLC_SCHED_SLOTS];
    HDLC_SchedStats_t stats;
} HDLC_Scheduler_t;

/* =======================================================================
 * API
 * ======================================================================= */

/**
 * Поставить LEGACY задачу (старый стиль, void fn(h, arg)).
 *
 * Плюсы:  просто, знакомо.
 * Минусы: если callback зависнет -- протокол зависнет вместе с ним.
 *         политика soft/requeue не работает -- callback не возвращает статус.
 *
 * delay_ms  = 0 -> выполнить в следующий HDLC_Poll.
 * policy рекомендуется HARD_KILL или LEGACY.
 */
static inline HDLC_Status_t HDLC_Sched_PostLegacy(
    HDLC_Scheduler_t  *s,
    HDLC_LegacyFn_t    fn,
    void              *arg,
    uint32_t           delay_ms,
    HDLC_SchedPolicy_t policy)
{
    if (!fn) return HDLC_ERR_NULL;
    uint32_t now = HAL_GetTick();
    for (uint8_t i = 0u; i < HDLC_SCHED_SLOTS; i++) {
        if (s->slots[i].used) continue;
        HDLC_SchedSlot_t *sl = &s->slots[i];
        sl->type            = HDLC_TASK_TYPE_LEGACY;
        sl->policy          = policy;
        sl->fn.legacy       = fn;
        sl->arg             = arg;
        sl->run_after       = now + delay_ms;
        sl->soft_deadline   = now + delay_ms + HDLC_SCHED_SOFT_DEADLINE_MS;
        sl->hard_deadline   = now + delay_ms + HDLC_SCHED_HARD_DEADLINE_MS;
        sl->max_steps       = 1u;
        sl->degraded_steps  = 1u;
        sl->requeue_count   = 0u;
        sl->used            = true;
        return HDLC_OK;
    }
    s->stats.drop_count++;
    return HDLC_ERR_BUF_FULL;
}

/**
 * Поставить STEP задачу (кооперативная, возвращает статус).
 *
 * Плюсы:  безопасна: планировщик контролирует число шагов.
 *         при SOFT_DEGRADE деградирует плавно, не убивает протокол.
 *         поддерживает все политики.
 *
 * Минусы: нужно писать step-машину вместо линейного кода.
 *
 * max_steps -- сколько раз вызвать fn за один тик HDLC_Poll.
 *             Рекомендация: 1 для длинных операций, до 4 для быстрых.
 */
static inline HDLC_Status_t HDLC_Sched_PostStep(
    HDLC_Scheduler_t  *s,
    HDLC_TaskStepFn_t  fn,
    void              *arg,
    uint32_t           delay_ms,
    uint8_t            max_steps,
    HDLC_SchedPolicy_t policy)
{
    if (!fn) return HDLC_ERR_NULL;
    if (!max_steps) max_steps = 1u;
    uint32_t now = HAL_GetTick();
    for (uint8_t i = 0u; i < HDLC_SCHED_SLOTS; i++) {
        if (s->slots[i].used) continue;
        HDLC_SchedSlot_t *sl = &s->slots[i];
        sl->type            = HDLC_TASK_TYPE_STEP;
        sl->policy          = policy;
        sl->fn.step         = fn;
        sl->arg             = arg;
        sl->run_after       = now + delay_ms;
        sl->soft_deadline   = now + delay_ms + HDLC_SCHED_SOFT_DEADLINE_MS;
        sl->hard_deadline   = now + delay_ms + HDLC_SCHED_HARD_DEADLINE_MS;
        sl->max_steps       = max_steps;
        sl->degraded_steps  = max_steps;
        sl->requeue_count   = 0u;
        sl->used            = true;
        return HDLC_OK;
    }
    s->stats.drop_count++;
    return HDLC_ERR_BUF_FULL;
}

/** Shortcut: legacy + HARD_KILL, delay=0 */
#define HDLC_Sched_Post(s, fn, arg, delay_ms) \
    HDLC_Sched_PostLegacy((s), (fn), (arg), (delay_ms), HDLC_SCHED_POLICY_HARD_KILL)

/**
 * Тик планировщика -- вызывать из HDLC_Poll каждый проход.
 */
static inline void HDLC_Sched_Tick(HDLC_Scheduler_t *s, HDLC_Handle_t *h)
{
    uint32_t now = HAL_GetTick();
    for (uint8_t i = 0u; i < HDLC_SCHED_SLOTS; i++) {
        HDLC_SchedSlot_t *sl = &s->slots[i];
        if (!sl->used) continue;

        /* Hard deadline -- убиваем без исполнения */
        if (now >= sl->hard_deadline) {
            sl->used = false;
            s->stats.overrun_count++;
            continue;
        }

        /* Ещё не время */
        if (now < sl->run_after) continue;

        /* Soft deadline -- применяем политику */
        if (now >= sl->soft_deadline) {
            switch (sl->policy) {
            case HDLC_SCHED_POLICY_HARD_KILL:
                sl->used = false;
                s->stats.overrun_count++;
                continue;

            case HDLC_SCHED_POLICY_SOFT_DEGRADE:
                if (sl->degraded_steps > 1u) sl->degraded_steps = 1u;
                s->stats.degrade_count++;
                break;

            case HDLC_SCHED_POLICY_REQUEUE:
                if (sl->requeue_count < HDLC_SCHED_REQUEUE_MAX) {
                    sl->requeue_count++;
                    sl->run_after     = now + HDLC_SCHED_REQUEUE_DELAY_MS;
                    sl->soft_deadline = now + HDLC_SCHED_REQUEUE_DELAY_MS
                                            + HDLC_SCHED_SOFT_DEADLINE_MS;
                    s->stats.requeue_count++;
                    continue;
                }
                /* Исчерпали requeue -- kill */
                sl->used = false;
                s->stats.overrun_count++;
                continue;

            case HDLC_SCHED_POLICY_LEGACY:
            default:
                break;
            }
        }

        /* Выполняем */
        if (sl->type == HDLC_TASK_TYPE_LEGACY) {
            sl->used = false;          /* освобождаем ДО вызова */
            sl->fn.legacy(h, sl->arg);
        } else {
            /* STEP: выполняем degraded_steps шагов */
            uint8_t steps = sl->degraded_steps;
            bool done = false;
            for (uint8_t s_ = 0u; s_ < steps && !done; s_++) {
                HDLC_TaskResult_t r = sl->fn.step(h, sl->arg);
                switch (r) {
                case HDLC_TASK_DONE:
                    sl->used = false;
                    done = true;
                    break;
                case HDLC_TASK_FAIL:
                    sl->used = false;
                    s->stats.fail_count++;
                    done = true;
                    break;
                case HDLC_TASK_RETRY:
                    sl->run_after = now + HDLC_SCHED_REQUEUE_DELAY_MS;
                    done = true;
                    break;
                case HDLC_TASK_YIELD:
                default:
                    break; /* продолжим в следующий тик */
                }
            }
            /* sl->used остаётся true если YIELD/RETRY */
        }
    }
}

static inline const HDLC_SchedStats_t *HDLC_Sched_GetStats(
    const HDLC_Scheduler_t *s) { return &s->stats; }
