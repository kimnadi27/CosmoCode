#include <Motor.h>

Motor::Motor(GPIO_TypeDef *pm0_port, GPIO_TypeDef *pm1_port, uint16_t pm0_pin, uint16_t pm1_pin, long long &pcounter)
: M0_port(pm0_port), M1_port(pm1_port), M0_pin(pm0_pin), M1_pin(pm1_pin), counter(&pcounter)
{
	*counter = 0;
}

void Motor::Start(bool direction)
{
	if(direction)
	{
		HAL_GPIO_WritePin(M0_port, M0_pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(M1_port, M1_pin, GPIO_PIN_RESET);
	}
	else
	{
		HAL_GPIO_WritePin(M0_port, M0_pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(M1_port, M1_pin, GPIO_PIN_SET);
	}
}

void Motor::Stop()
{
	HAL_GPIO_WritePin(M1_port, M1_pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(M0_port, M0_pin, GPIO_PIN_RESET);
}

void Motor::ActiveStop(bool pre_direction)
{
	if(pre_direction)
	{
		HAL_GPIO_WritePin(M0_port, M0_pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(M1_port, M1_pin, GPIO_PIN_SET);
		HAL_Delay(5);

		HAL_GPIO_WritePin(M1_port, M1_pin, GPIO_PIN_RESET);
	}
	else
	{
		HAL_GPIO_WritePin(M0_port, M0_pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(M1_port, M1_pin, GPIO_PIN_RESET);
		HAL_Delay(5);

		HAL_GPIO_WritePin(M0_port, M0_pin, GPIO_PIN_RESET);
	}
}

void Motor::ManualMotorStep(uint32_t steps, bool direction)
{
	*counter = 0;
	if(direction)
	{
		HAL_GPIO_WritePin(M0_port, M0_pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(M1_port, M1_pin, GPIO_PIN_RESET);

		while(*counter < steps)    // Wait until all steps will completed
		{

		}

		HAL_GPIO_WritePin(M0_port, M0_pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(M1_port, M1_pin, GPIO_PIN_SET);
		HAL_Delay(5);

		HAL_GPIO_WritePin(M1_port, M1_pin, GPIO_PIN_RESET);
	}
	else
	{
		HAL_GPIO_WritePin(M0_port, M0_pin, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(M1_port, M1_pin, GPIO_PIN_SET);

		while(*counter < steps)    // Wait until all steps will completed
		{

		}

		HAL_GPIO_WritePin(M0_port, M0_pin, GPIO_PIN_SET);
		HAL_GPIO_WritePin(M1_port, M1_pin, GPIO_PIN_RESET);
		HAL_Delay(5);

		HAL_GPIO_WritePin(M0_port, M0_pin, GPIO_PIN_RESET);
	}

	*counter = 0;

	// *counter = *counter - steps;     IF WE WANT ABSOLUTE MOVING
}


