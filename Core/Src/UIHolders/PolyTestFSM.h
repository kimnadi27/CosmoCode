#pragma once
/**
 * PolyTestFSM.h  --  FSM poly_test + публичный API для UIHolder'ов.
 *
 * Включать в main.cpp ПЕРЕД UIHolder'ами.
 * Файл рассчитан на компиляцию как C++ (.cpp).
 *
 * API:
 *   PolyTest_Init(hx711*, motor*)  -- вызвать в on_periph_enable
 *   PolyTest_Tick(hdlc*)           -- вызывать в on_user_loop
 *   PolyTest_IsIdle()
 *   PolyTest_ForceStart()
 *   PolyTest_ReadWeightNow()
 *   PolyTest_GetInterval()
 *   PolyTest_SetInterval(ms)
 */

#include "../../HDLC/hdlc.h"  /* HDLC_Handle_t, HDLC_Push */
#include "Motor.h"
#include "Hx711.h"

/* -----------------------------------------------------------------------
 * Константы
 * --------------------------------------------------------------------- */
#define PT_WEIGHT_THRESH_1    100    /* мг */
#define PT_WEIGHT_THRESH_2    200
#define PT_WEIGHT_THRESH_3    300
#define PT_WEIGHT_THRESH_MAX  400

#define PT_MOTOR_BRAKE_MS     5u
#define PT_INTERVAL_DEFAULT   1000u
#define PT_OPCODE_RESULT      0x10u

#define DIR_TO_TENZO    false
#define DIR_FROM_TENZO  true

/* -----------------------------------------------------------------------
 * Состояния FSM
 * --------------------------------------------------------------------- */
typedef enum {
    PT_IDLE = 0,
    PT_TARE,
    PT_MOVE_FWD,
    PT_BRAKE,
    PT_MOVE_BACK,
    PT_DONE
} PT_State_t;

/* -----------------------------------------------------------------------
 * Счётчик шагов мотора -- инкрементируется в HAL_TIM_PeriodElapsedCallback
 * Объявлен в main.cpp как: long long g_motor_counter = 0;
 * --------------------------------------------------------------------- */
extern long long g_motor_counter;

/* -----------------------------------------------------------------------
 * Состояние FSM (singleton, static внутри .h — один экземпляр на единицу трансляции)
 * --------------------------------------------------------------------- */
static struct {
    PT_State_t   state;
    Hx711       *hx711;
    Motor       *motor;
    uint32_t     interval_ms;
    uint32_t     last_run_ms;
    bool         force;
    uint32_t     brake_start_ms;
    long long    cnt_at_start;
    long long    cnt_at_stop;
    int          pt_stat;
    int          pt_data[4];
    float        pt_result;
} _pt;

/* -----------------------------------------------------------------------
 * Публичный API
 * --------------------------------------------------------------------- */

static inline void PolyTest_Init(Hx711 *hx711, Motor *motor)
{
    _pt.hx711       = hx711;
    _pt.motor       = motor;
    _pt.interval_ms = PT_INTERVAL_DEFAULT;
    _pt.state       = PT_IDLE;
    _pt.force       = false;
    _pt.last_run_ms = HAL_GetTick();
}

static inline bool     PolyTest_IsIdle(void)          { return _pt.state == PT_IDLE; }
static inline uint32_t PolyTest_GetInterval(void)     { return _pt.interval_ms; }
static inline void     PolyTest_SetInterval(uint32_t ms) {
    _pt.interval_ms = ms;
    _pt.last_run_ms = HAL_GetTick(); /* сбросить таймер */
}
static inline void PolyTest_ForceStart(void) {
    if (_pt.state == PT_IDLE) _pt.force = true;
}
static inline float PolyTest_ReadWeightNow(void) {
    if (!_pt.hx711) return 0.0f;
    return (float)_pt.hx711->getWeight();
}

/* -----------------------------------------------------------------------
 * Тик FSM -- вызывать из on_user_loop каждый проход HDLC_Poll
 * --------------------------------------------------------------------- */
static inline void PolyTest_Tick(HDLC_Handle_t *h)
{
    uint32_t now = HAL_GetTick();

    switch (_pt.state)
    {
    /* -------------------------------------------------------------- */
    case PT_IDLE:
        if ((now - _pt.last_run_ms) >= _pt.interval_ms || _pt.force)
        {
            _pt.force       = false;
            _pt.last_run_ms = now;
            _pt.state       = PT_TARE;
        }
        break;

    /* -------------------------------------------------------------- */
    case PT_TARE:
        /*
         * setTare() блокирует ~2.5 с (5 × HAL_Delay(500) внутри).
         * Это ограничение Hx711.cpp; DMA ring buffer HDLC не теряет
         * байты, но ответы на запросы в это время не отправляются.
         */
        if (!_pt.hx711->setTare())
        {
            _pt.state = PT_IDLE; /* тарировка не удалась */
            break;
        }
        for (int i = 0; i < 4; i++) _pt.pt_data[i] = 0;
        _pt.pt_stat      = 0;
        g_motor_counter  = 0;
        _pt.motor->Start(DIR_TO_TENZO);
        _pt.cnt_at_start = g_motor_counter;
        _pt.state        = PT_MOVE_FWD;
        break;

    /* -------------------------------------------------------------- */
    case PT_MOVE_FWD:
    {
        int w = _pt.hx711->getWeight();

        if (_pt.pt_stat == 0 && w > PT_WEIGHT_THRESH_1)
            { _pt.pt_data[0] = (int)(g_motor_counter - _pt.cnt_at_start); _pt.pt_stat = 1; }
        if (_pt.pt_stat == 1 && w > PT_WEIGHT_THRESH_2)
            { _pt.pt_data[1] = (int)(g_motor_counter - _pt.cnt_at_start); _pt.pt_stat = 2; }
        if (_pt.pt_stat == 2 && w > PT_WEIGHT_THRESH_3)
            { _pt.pt_data[2] = (int)(g_motor_counter - _pt.cnt_at_start); _pt.pt_stat = 3; }

        if (w >= PT_WEIGHT_THRESH_MAX)
        {
            _pt.pt_data[3]    = (int)(g_motor_counter - _pt.cnt_at_start);
            _pt.cnt_at_stop   = g_motor_counter;
            _pt.motor->Start(DIR_FROM_TENZO); /* начало неблокирующего тормоза */
            _pt.brake_start_ms = now;
            _pt.state          = PT_BRAKE;
        }
        break;
    }

    /* -------------------------------------------------------------- */
    case PT_BRAKE:
        if ((now - _pt.brake_start_ms) >= PT_MOTOR_BRAKE_MS)
        {
            _pt.motor->Stop();
            _pt.state = PT_MOVE_BACK;
        }
        break;

    /* -------------------------------------------------------------- */
    case PT_MOVE_BACK:
        /* ManualMotorStep блокирует на время хода (без HAL_Delay кроме 5 мс тормоза) */
        _pt.motor->ManualMotorStep((uint32_t)_pt.cnt_at_stop, DIR_FROM_TENZO);
        _pt.pt_result = (float)_pt.hx711->getWeight();
        _pt.state     = PT_DONE;
        break;

    /* -------------------------------------------------------------- */
    case PT_DONE:
    {
        /* Пакет: [float вес, 4б LE] + [4 × uint32_t шаги по порогам, LE] = 20 байт */
        uint8_t payload[4u + 4u * 4u];
        __builtin_memcpy(&payload[0], &_pt.pt_result, 4u);
        for (int i = 0; i < 4; i++) {
            uint32_t v = (uint32_t)_pt.pt_data[i];
            payload[4u + i*4u + 0u] = (uint8_t)( v        & 0xFFu);
            payload[4u + i*4u + 1u] = (uint8_t)((v >>  8) & 0xFFu);
            payload[4u + i*4u + 2u] = (uint8_t)((v >> 16) & 0xFFu);
            payload[4u + i*4u + 3u] = (uint8_t)((v >> 24) & 0xFFu);
        }
        HDLC_Push(h, PT_OPCODE_RESULT, payload, (uint16_t)sizeof(payload));
        _pt.last_run_ms = HAL_GetTick();
        _pt.state       = PT_IDLE;
        break;
    }

    default:
        _pt.state = PT_IDLE;
        break;
    }
}
