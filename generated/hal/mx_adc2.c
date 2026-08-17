/**
  ******************************************************************************
  * @file           : mx_adc2.c
  * @brief          : ADC2 Peripheral initialization
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the mx_stm32c5xx_hal_drivers_license.md file
  * in the same directory as the generated code.
  * If no mx_stm32c5xx_hal_drivers_license.md file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "mx_adc2.h"
/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private functions prototype------------------------------------------------*/
/* Exported variables by reference--------------------------------------------*/
static hal_adc_handle_t hADC2;

/******************************************************************************/
/* Exported functions for ADC2 in HAL layer (SW instance MyADC_2) */
/******************************************************************************/
hal_adc_handle_t *mx_adc2_init(void)
{
  HAL_RCC_ADC12_EnableClock();

  if (HAL_RCC_ADCDAC_SetKernelClkSource(HAL_RCC_ADCDAC_CLK_SRC_PSIS) != HAL_OK)
  {
    return NULL;
  }

  if (HAL_ADC_Init(&hADC2, HAL_ADC2) != HAL_OK)
  {
    return NULL;
  }

  hal_adc_config_t adc_config;

  adc_config.resolution          = HAL_ADC_RESOLUTION_12_BIT;
  adc_config.sampling_mode       = HAL_ADC_SAMPLING_MODE_NORMAL;
  HAL_ADC_SetConfig(&hADC2, &adc_config);

  /****************************************************************************/
  /* Configuration of basic features (mandatory)                              */
  /****************************************************************************/

/* ==================== Group Regular ====================*/
  hal_adc_reg_config_t reg_config;
  reg_config.trigger_src        = HAL_ADC_REG_TRIG_SOFTWARE;
  reg_config.sequencer_length   = 2;
  /* Discontinuous mode, 1 rank per subgroup: see mx_adc1.c for the reason. */
  reg_config.sequencer_discont  = HAL_ADC_REG_SEQ_DISCONT_1RANK;
  reg_config.continuous         = HAL_ADC_REG_CONV_SINGLE;
  reg_config.overrun            = HAL_ADC_REG_OVR_DATA_OVERWRITTEN;
  HAL_ADC_REG_SetConfig(&hADC2, &reg_config);

  hal_adc_channel_config_t adc_channel_config;

  adc_channel_config.group           = HAL_ADC_GROUP_REGULAR;
  adc_channel_config.sampling_time   = HAL_ADC_SAMPLING_TIME_48CYCLES;
  adc_channel_config.input_mode      = HAL_ADC_IN_SINGLE_ENDED;

  adc_channel_config.sequencer_rank  = 1;
  HAL_ADC_SetConfigChannel(&hADC2, HAL_ADC_CHANNEL_6, &adc_channel_config); /* PB0 - ADC2_IN6 */

  adc_channel_config.sequencer_rank  = 2;
  HAL_ADC_SetConfigChannel(&hADC2, HAL_ADC_CHANNEL_7, &adc_channel_config); /* PB1 - ADC2_IN7 */

  /****************************************************************************/
  /* Configuration of additional features (optional)                          */
  /****************************************************************************/

  HAL_RCC_GPIOB_EnableClock();

  hal_gpio_config_t  gpio_config;

  /**
    ADC2 GPIO Configuration

    [GPIO Pin] ------> [Signal Name]

       PB0     ------>   ADC2_IN6
       PB1     ------>   ADC2_IN7
    **/
  gpio_config.mode        = HAL_GPIO_MODE_ANALOG;
  gpio_config.pull        = HAL_GPIO_PULL_NO;
  HAL_GPIO_Init(HAL_GPIOB, HAL_GPIO_PIN_0 | HAL_GPIO_PIN_1, &gpio_config);

  return &hADC2;
}

void mx_adc2_deinit(void)
{
  (void)HAL_ADC_DeInit(&hADC2);

  /* De-initialize all GPIO pins associated with ADC2 */
  HAL_GPIO_DeInit(HAL_GPIOB, HAL_GPIO_PIN_0 | HAL_GPIO_PIN_1);
}

hal_adc_handle_t *mx_adc2_gethandle(void)
{
  return &hADC2;
}
