#pragma once

#include <stm32l4xx_hal.h>

class Motor
{
private:
	GPIO_TypeDef *M0_port;
	GPIO_TypeDef *M1_port;
	uint16_t M0_pin;
	uint16_t M1_pin;
	long long *counter;

public:
	Motor(GPIO_TypeDef *pm0_port, GPIO_TypeDef *pm1_port, uint16_t pm0_pin, uint16_t pm1_pin, long long &pcounter);

	void Start(bool direction);
	void Stop();
	void ActiveStop(bool pre_direction);
	void ManualMotorStep(uint32_t steps, bool direction);
};
