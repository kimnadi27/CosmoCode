#pragma once

extern "C" {
#include <stm32l4xx_hal.h>
}

#include <iostream>
#include <algorithm>

class Hx711
{
private:
	GPIO_TypeDef *DT_port;
	GPIO_TypeDef *SCK_port;
	uint16_t DT_pin;
	uint16_t SCK_pin;
	TIM_HandleTypeDef *htim;
	uint32_t tare;
	float coefficient;
//	float knownOriginal;
//	float knownHX711;

public:
	Hx711(GPIO_TypeDef *DT_port, GPIO_TypeDef *SCK_port, uint16_t DT_pin, uint16_t SCK_pin, TIM_HandleTypeDef *htim, uint32_t tare = 8600020, float coefficient = 0.24783146f);

	void microDelay(uint16_t delay);
	int32_t getHX711();
	int getWeight();
	bool setTare();


};
