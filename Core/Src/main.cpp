/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   PolyTest — адаптация под UniversalModuleFirmware / HDLC-слейв
  ******************************************************************************
  *
  * Порядок include (обязателен):
  *   1. hdlc.h               -- транспорт
  *   2. Utils.h              -- HDLC_Timer_t и хелперы
  *   3. PolyTestFSM.h        -- FSM poly_test + публичный API
  *   4. UIHolder_*.h         -- UI-субхолдеры (команды с RS-шины)
  *   5. Holders.h            -- ПОСЛЕДНИМ (определяет Holders_Dispatch)
  *
  * UI-команды (UI_NORMAL 0x13, фаза READY):
  *   0xA1          -- принудительный запуск теста
  *   0xA2          -- принудительный запрос веса (без мотора)
  *   0xA3 + u32LE  -- изменить интервал эксперимента (мс)
  *
  * Данные теста (RR DataBuffer, опкод 0x10), 20 байт LE:
  *   [0..3]   float  -- итоговый вес после теста (мг)
  *   [4..7]   u32    -- шаги мотора до порога 100 мг
  *   [8..11]  u32    -- шаги мотора до порога 200 мг
  *   [12..15] u32    -- шаги мотора до порога 300 мг
  *   [16..19] u32    -- шаги мотора до порога 400 мг (полный ход)
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "adc.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* USER CODE BEGIN Includes */
#include "../../HDLC/hdlc.h"
#include "../../Core/Inc/Utils.h"
#include "Hx711.h"
#include "../../CosmosLib/Motor.h"

/*
 * PolyTestFSM.h должен быть включён ДО UIHolder'ов,
 * так как те обращаются к его API.
 */
#include "UIHolders/PolyTestFSM.h"

#include "UIHolders/UIHolder_Stats.h"
#include "UIHolders/UIHolder_TestBuffer.h"

/* Три новых UI-команды */
#include "UIHolders/UIHolder_ForceRun.h"      /* 0xA1 -- принудительный запуск    */
#include "UIHolders/UIHolder_GetData.h"        /* 0xA2 -- принудительный запрос данных */
#include "UIHolders/UIHolder_SetInterval.h"    /* 0xA3 -- изменить интервал        */

#include "../../HDLC/Holders/Holders.h"        /* ПОСЛЕДНИМ */
/* USER CODE END Includes */

/* -----------------------------------------------------------------------
 * HAL-хэндлы (USART1 + DMA — используются библиотекой)
 * --------------------------------------------------------------------- */
UART_HandleTypeDef huart1;
DMA_HandleTypeDef  hdma_usart1_rx;
DMA_HandleTypeDef  hdma_usart1_tx;

/* USER CODE BEGIN PV */

/* Счётчик шагов мотора — инкрементируется в HAL_TIM_PeriodElapsedCallback */
long long g_motor_counter = 0;

/* HAL-хэндлы периферии (объявлены здесь, используются в on_periph_enable) */
//static TIM_HandleTypeDef  htim1;   /* таймер для HX711 (1 МГц, микросекунды) */
//static TIM_HandleTypeDef  htim3;   /* таймер шагов мотора                    */
//static TIM_HandleTypeDef  htim2;   /* вспомогательный (ADC-тригер, если нужен) */

/* Объекты периферии уровня приложения */
static Hx711  g_hx711;
static Motor  g_motor;

/* HDLC-хэндл */
static HDLC_Handle_t g_hdlc;

/* USER CODE END PV */

/* -----------------------------------------------------------------------
 * Прототипы
 * --------------------------------------------------------------------- */
void SystemClock_Config(void);
//static void MX_GPIO_Init(void);
//static void MX_DMA_Init(void);
//static void MX_USART1_UART_Init(void);
//static void MX_TIM1_Init(void);
//static void MX_TIM2_Init(void);
//static void MX_TIM3_Init(void);
//static void MX_GPIO_Extra_Init(void);   /* GPIO мотора, HX711, питание 5В */

/* -----------------------------------------------------------------------
 * Колбэки HDLC-слейва
 * --------------------------------------------------------------------- */

/**
 * on_periph_enable -- вызывается ОДИН РАЗ после первого успешного TEST.
 * Инициализируем периферию, которая нужна только в рабочей фазе.
 */
static void on_periph_enable(HDLC_Handle_t *h)
{
    (void)h;

    /* Таймеры */
    MX_TIM1_Init();   /* HX711 — счётчик микросекунд */
    MX_TIM2_Init();   /* вспомогательный              */
    MX_TIM3_Init();   /* шаги мотора                  */

    HAL_TIM_Base_Start(&htim1);
    HAL_TIM_Base_Start_IT(&htim3);   /* прерывание каждый шаг */

    /* GPIO мотора, HX711, питание */
    MX_GPIO_Init();

    /* Включаем питание 5 В */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);

    /*
     * Инициализируем объекты приложения.
     * Hx711 и Motor используют C++ конструкторы — здесь вызываем
     * их C-обёртки (Hx711_Init / Motor_Init), либо размещаем объекты
     * через placement-new если файл скомпилирован как .cpp.
     *
     * Пример для C (если Motor.h / Hx711.h имеют C-API):
     */
    Hx711_Init(&g_hx711,
               GPIOA, GPIOB,          /* DT_port, SCK_port */
               GPIO_PIN_12,            /* DT_pin  */
               GPIO_PIN_8,             /* SCK_pin */
               &htim1,
               8600020u,               /* tare (заводское значение) */
               0.24783146f);           /* coefficient               */

    Motor_Init(&g_motor,
               GPIOC, GPIOC,
               GPIO_PIN_0, GPIO_PIN_1,
               &g_motor_counter);

    /* Запускаем FSM poly_test */
    PolyTest_Init(&g_hx711, &g_motor);
}

/**
 * on_user_loop -- вызывается каждый тик HDLC_Poll пока initialized.
 * Здесь крутится FSM poly_test — никаких HAL_Delay.
 */
static void on_user_loop(HDLC_Handle_t *h)
{
    PolyTest_Tick(h);
}

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* Базовая GPIO (LED, RESP_GATE) и DMA+USART1 — нужны ДО HDLC_Init */
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART1_UART_Init();

    /* USER CODE BEGIN 2 */

    /*
     * ВАЖНО: HDLC_Init делает memset(h, 0) — вызывать ДО любых Register.
     * Адрес слейва 0x01 (совпадает с примером из библиотеки).
     */
    HDLC_Init(&g_hdlc, &huart1, 0x01u, on_periph_enable, on_user_loop);

    /* Стандартные холдеры из библиотеки */
    UIHolder_Stats_Register(&g_hdlc);
    UIHolder_TestBuffer_Register(&g_hdlc);

    /* Пользовательские UI-команды */
    UIHolder_ForceRun_Register(&g_hdlc);      /* 0xA1 */
    UIHolder_GetData_Register(&g_hdlc);        /* 0xA2 */
    UIHolder_SetInterval_Register(&g_hdlc);    /* 0xA3 */

    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN WHILE */
        HDLC_Poll(&g_hdlc);
        /* USER CODE END WHILE */
    }
}

/* -----------------------------------------------------------------------
 * Колбэки HAL
 * --------------------------------------------------------------------- */

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == g_hdlc.huart)
        HDLC_Transmitter_TxDone(&g_hdlc);
}

/**
 * Прерывание таймера TIM3 — инкрементируем счётчик шагов мотора.
 * Этот счётчик использует Motor и FSM poly_test.
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3)
        g_motor_counter++;
}

/* -----------------------------------------------------------------------
 * Инициализация тактирования (сгенерировано CubeMX)
 * --------------------------------------------------------------------- */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) Error_Handler();
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 1;
    RCC_OscInitStruct.PLL.PLLN       = 8;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR       = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK) Error_Handler();
}

///* -----------------------------------------------------------------------
// * Инициализация периферии
// * --------------------------------------------------------------------- */
//
//static void MX_USART1_UART_Init(void)
//{
//    huart1.Instance                    = USART1;
//    huart1.Init.BaudRate               = 115200;
//    huart1.Init.WordLength             = UART_WORDLENGTH_8B;
//    huart1.Init.StopBits               = UART_STOPBITS_1;
//    huart1.Init.Parity                 = UART_PARITY_NONE;
//    huart1.Init.Mode                   = UART_MODE_TX_RX;
//    huart1.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
//    huart1.Init.OverSampling           = UART_OVERSAMPLING_16;
//    huart1.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
//    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_RXOVERRUNDISABLE_INIT;
//    huart1.AdvancedInit.OverrunDisable = UART_ADVFEATURE_OVERRUN_DISABLE;
//    if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
//}
//
//static void MX_DMA_Init(void)
//{
//    __HAL_RCC_DMA1_CLK_ENABLE();
//    HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 3, 0);
//    HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
//    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 2, 0);
//    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
//}
//
//static void MX_GPIO_Init(void)
//{
//    GPIO_InitTypeDef GPIO_InitStruct = {0};
//    __HAL_RCC_GPIOC_CLK_ENABLE();
//    __HAL_RCC_GPIOH_CLK_ENABLE();
//    __HAL_RCC_GPIOA_CLK_ENABLE();
//
//    /* LED */
//    HAL_GPIO_WritePin(Led_GPIO_Port, Led_Pin, GPIO_PIN_RESET);
//    GPIO_InitStruct.Pin   = Led_Pin;
//    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
//    GPIO_InitStruct.Pull  = GPIO_NOPULL;
//    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
//    HAL_GPIO_Init(Led_GPIO_Port, &GPIO_InitStruct);
//
//    /* RESP_GATE (вход управления шиной RS485) */
//    GPIO_InitStruct.Pin  = RESP_GATE_Pin;
//    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
//    GPIO_InitStruct.Pull = GPIO_NOPULL;
//    HAL_GPIO_Init(RESP_GATE_GPIO_Port, &GPIO_InitStruct);
//    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 1, 0);
//    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
//}
//
///** GPIO мотора, HX711, питание 5В — инициализируются в on_periph_enable */
//static void MX_GPIO_Extra_Init(void)
//{
//    GPIO_InitTypeDef GPIO_InitStruct = {0};
//
//    __HAL_RCC_GPIOB_CLK_ENABLE();
//
//    /* GPIO питание 5В */
//    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
//    GPIO_InitStruct.Pin   = GPIO_PIN_0;
//    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
//    GPIO_InitStruct.Pull  = GPIO_NOPULL;
//    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
//    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
//
//    /* Мотор: PC0, PC1 */
//    GPIO_InitStruct.Pin   = GPIO_PIN_0 | GPIO_PIN_1;
//    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
//    GPIO_InitStruct.Pull  = GPIO_NOPULL;
//    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
//    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
//
//    /* HX711 SCK: PB8 */
//    GPIO_InitStruct.Pin   = GPIO_PIN_8;
//    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
//    GPIO_InitStruct.Pull  = GPIO_NOPULL;
//    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
//    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
//
//    /* HX711 DT: PA12 (вход) */
//    GPIO_InitStruct.Pin  = GPIO_PIN_12;
//    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
//    GPIO_InitStruct.Pull = GPIO_NOPULL;
//    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
//}
//
///**
// * TIM1 — счётчик микросекунд для HX711.
// * Настраиваем на 1 МГц (1 тик = 1 мкс) при системной частоте 16 МГц.
// * Скорректировать Prescaler под реальную PCLK если тактирование другое.
// */
//static void MX_TIM1_Init(void)
//{
//    __HAL_RCC_TIM1_CLK_ENABLE();
//    htim1.Instance               = TIM1;
//    htim1.Init.Prescaler         = (uint32_t)(HAL_RCC_GetPCLK2Freq() / 1000000u) - 1u;
//    htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
//    htim1.Init.Period            = 0xFFFF;
//    htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
//    htim1.Init.RepetitionCounter = 0;
//    if (HAL_TIM_Base_Init(&htim1) != HAL_OK) Error_Handler();
//}
//
///** TIM2 — вспомогательный (ADC-тригер / запас). */
//static void MX_TIM2_Init(void)
//{
//    __HAL_RCC_TIM2_CLK_ENABLE();
//    htim2.Instance           = TIM2;
//    htim2.Init.Prescaler     = (uint32_t)(HAL_RCC_GetPCLK1Freq() / 1000000u) - 1u;
//    htim2.Init.CounterMode   = TIM_COUNTERMODE_UP;
//    htim2.Init.Period        = 0xFFFF;
//    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
//    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) Error_Handler();
//}
//
///**
// * TIM3 — счётчик шагов мотора (прерывание).
// * Период подобрать под реальное число импульсов энкодера.
// * Здесь: 1 прерывание = 1 шаг (период = 1, prescaler под задачу).
// */
//static void MX_TIM3_Init(void)
//{
//    __HAL_RCC_TIM3_CLK_ENABLE();
//    htim3.Instance           = TIM3;
//    htim3.Init.Prescaler     = 0u;
//    htim3.Init.CounterMode   = TIM_COUNTERMODE_UP;
//    htim3.Init.Period        = 1u;   /* каждые 2 тика APB1 = 1 шаг */
//    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
//    if (HAL_TIM_Base_Init(&htim3) != HAL_OK) Error_Handler();
//
//    HAL_NVIC_SetPriority(TIM3_IRQn, 5, 0);
//    HAL_NVIC_EnableIRQ(TIM3_IRQn);
//}

/* -----------------------------------------------------------------------
 * Обработчик ошибок
 * --------------------------------------------------------------------- */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
}
#endif
