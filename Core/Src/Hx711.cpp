#include <Hx711.h>


Hx711::Hx711(GPIO_TypeDef *DT_port, GPIO_TypeDef *SCK_port, uint16_t DT_pin, uint16_t SCK_pin, TIM_HandleTypeDef *htim, uint32_t tare, float coefficient)
: DT_port(DT_port), SCK_port(SCK_port), DT_pin(DT_pin), SCK_pin(SCK_pin), htim(htim), tare(tare), coefficient(coefficient)
{

}

void Hx711::microDelay(uint16_t delay)
{
	__HAL_TIM_SET_COUNTER(htim, 0);
	while (__HAL_TIM_GET_COUNTER(htim) < delay);
}

//int32_t Hx711::getHX711()
//{
//	uint32_t data = 0;
//	uint32_t startTime = HAL_GetTick();
//	while(HAL_GPIO_ReadPin(DT_port, DT_pin) == GPIO_PIN_SET)
//	{
//		if(HAL_GetTick() - startTime > 200)
//		  return 0;
//	}
//	for(int8_t len=0; len<24 ; len++)
//	{
//		HAL_GPIO_WritePin(SCK_port, SCK_pin, GPIO_PIN_SET);
//		microDelay(1);
//		data = data << 1;
//		HAL_GPIO_WritePin(SCK_port, SCK_pin, GPIO_PIN_RESET);
//		microDelay(1);
//		if(HAL_GPIO_ReadPin(DT_port, DT_pin) == GPIO_PIN_SET)
//		  data ++;
//		}
//
//	data = data ^ 0x800000;
//	HAL_GPIO_WritePin(SCK_port, SCK_pin, GPIO_PIN_SET);
//	microDelay(1);
//	HAL_GPIO_WritePin(SCK_port, SCK_pin, GPIO_PIN_RESET);
//	microDelay(1);
//	return data;
//}

int32_t Hx711::getHX711(void) {
    long value = 0;

    while(HAL_GPIO_ReadPin(DT_port, DT_pin) == GPIO_PIN_SET);

    for (int i = 0; i < 24; i++) {
        HAL_GPIO_WritePin(SCK_port, SCK_pin, GPIO_PIN_SET);
        value = (value << 1) | HAL_GPIO_ReadPin(DT_port, DT_pin);
        HAL_GPIO_WritePin(SCK_port, SCK_pin, GPIO_PIN_RESET);
    }

    HAL_GPIO_WritePin(SCK_port, SCK_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SCK_port, SCK_pin, GPIO_PIN_RESET);

    if (value & 0x800000) value |= 0xFF000000;

    return value;
}

int Hx711::getWeight(void)
{
	int32_t  total = 0;
	int32_t  samples = 10;
	int milligram;

	for(uint16_t i=0 ; i<samples ; i++)
	{
	  total += getHX711();
	}
	int32_t average = (int32_t)(total / samples);

//	coefficient = knownOriginal / knownHX711;
	milligram = (int)(average-tare)*coefficient;

	return milligram;
}

bool Hx711::setTare()
{
	int weight[5];
	int av_weight = 0;

	for(int i = 0; i < 5; i++)
	{
		weight[i] = getWeight();
		av_weight = av_weight + weight[i];
		HAL_Delay(500);
	}

	auto minmax = std::minmax_element(weight, weight + 5);

	if(*minmax.second - *minmax.first >= 1000)
		return 0;
	else
	{
		av_weight = av_weight / 5;
		tare = tare + av_weight / coefficient;
		return 1;
	}
}





