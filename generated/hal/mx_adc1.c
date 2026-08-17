/**
  ******************************************************************************
  * @file           : mx_adc1.c
  * @brief          : ADC1 Peripheral initialization
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
#include "mx_adc1.h"
/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private functions prototype------------------------------------------------*/
/* Exported variables by reference--------------------------------------------*/
static hal_adc_handle_t hADC1;

/******************************************************************************/
/* Exported functions for ADC1 in HAL layer (SW instance MyADC_1) */
/******************************************************************************/
hal_adc_handle_t *mx_adc1_init(void)
{
  HAL_RCC_ADC12_EnableClock();

  if (HAL_RCC_ADCDAC_SetKernelClkSource(HAL_RCC_ADCDAC_CLK_SRC_PSIS) != HAL_OK)
  {
    return NULL;
  }

  if (HAL_ADC_Init(&hADC1, HAL_ADC1) != HAL_OK)
  {
    return NULL;
  }

  hal_adc_config_t adc_config;

  adc_config.resolution          = HAL_ADC_RESOLUTION_12_BIT;
  adc_config.sampling_mode       = HAL_ADC_SAMPLING_MODE_NORMAL;
  HAL_ADC_SetConfig(&hADC1, &adc_config);

  /****************************************************************************/
  /* Configuration of basic features (mandatory)                              */
  /****************************************************************************/

/* ==================== Group Regular ====================*/
  hal_adc_reg_config_t reg_config;
  reg_config.trigger_src        = HAL_ADC_REG_TRIG_SOFTWARE;
  reg_config.sequencer_length   = 8;
  /* Discontinuous mode, 1 rank per subgroup: each SW trigger (StartConv/TrigNextConv)
     converts exactly one channel, then the ADC halts and waits for the next trigger
     before moving to the next rank. This is the standard pattern for polling a
     multi-channel regular sequence with a software trigger; letting the sequence
     run as one continuous burst (discont = DISABLE) got stuck after 6 conversions
     on this device. */
  reg_config.sequencer_discont  = HAL_ADC_REG_SEQ_DISCONT_1RANK;
  reg_config.continuous         = HAL_ADC_REG_CONV_SINGLE;
  reg_config.overrun            = HAL_ADC_REG_OVR_DATA_OVERWRITTEN;
  HAL_ADC_REG_SetConfig(&hADC1, &reg_config);

  hal_adc_channel_config_t adc_channel_config;

  adc_channel_config.group           = HAL_ADC_GROUP_REGULAR;
  adc_channel_config.sampling_time   = HAL_ADC_SAMPLING_TIME_48CYCLES;
  adc_channel_config.input_mode      = HAL_ADC_IN_SINGLE_ENDED;

  adc_channel_config.sequencer_rank  = 1;
  HAL_ADC_SetConfigChannel(&hADC1, HAL_ADC_CHANNEL_0, &adc_channel_config); /* PA0 - ADC1_IN0 */

  adc_channel_config.sequencer_rank  = 2;
  HAL_ADC_SetConfigChannel(&hADC1, HAL_ADC_CHANNEL_1, &adc_channel_config); /* PA1 - ADC1_IN1 */

  adc_channel_config.sequencer_rank  = 3;
  HAL_ADC_SetConfigChannel(&hADC1, HAL_ADC_CHANNEL_2, &adc_channel_config); /* PA2 - ADC1_IN2 */

  adc_channel_config.sequencer_rank  = 4;
  HAL_ADC_SetConfigChannel(&hADC1, HAL_ADC_CHANNEL_3, &adc_channel_config); /* PA3 - ADC1_IN3 */

  adc_channel_config.sequencer_rank  = 5;
  HAL_ADC_SetConfigChannel(&hADC1, HAL_ADC_CHANNEL_4, &adc_channel_config); /* PA4 - ADC1_IN4 */

  adc_channel_config.sequencer_rank  = 6;
  HAL_ADC_SetConfigChannel(&hADC1, HAL_ADC_CHANNEL_5, &adc_channel_config); /* PA5 - ADC1_IN5 */

  adc_channel_config.sequencer_rank  = 7;
  HAL_ADC_SetConfigChannel(&hADC1, HAL_ADC_CHANNEL_6, &adc_channel_config); /* PA6 - ADC1_IN6 */

  adc_channel_config.sequencer_rank  = 8;
  HAL_ADC_SetConfigChannel(&hADC1, HAL_ADC_CHANNEL_7, &adc_channel_config); /* PA7 - ADC1_IN7 */

  /****************************************************************************/
  /* Configuration of additional features (optional)                          */
  /****************************************************************************/

  HAL_RCC_GPIOA_EnableClock();

  hal_gpio_config_t  gpio_config;

  /**
    ADC1 GPIO Configuration

    [GPIO Pin] ------> [Signal Name]

       PA0     ------>   ADC1_IN0
       PA1     ------>   ADC1_IN1
       PA2     ------>   ADC1_IN2
       PA3     ------>   ADC1_IN3
       PA4     ------>   ADC1_IN4
       PA5     ------>   ADC1_IN5
       PA6     ------>   ADC1_IN6
       PA7     ------>   ADC1_IN7
    **/
  gpio_config.mode        = HAL_GPIO_MODE_ANALOG;
  gpio_config.pull        = HAL_GPIO_PULL_NO;
  HAL_GPIO_Init(HAL_GPIOA, HAL_GPIO_PIN_0 | HAL_GPIO_PIN_1 | HAL_GPIO_PIN_2 | HAL_GPIO_PIN_3 | HAL_GPIO_PIN_4 | HAL_GPIO_PIN_5 | HAL_GPIO_PIN_6 | HAL_GPIO_PIN_7, &gpio_config);

  return &hADC1;
}

void mx_adc1_deinit(void)
{
  (void)HAL_ADC_DeInit(&hADC1);

  /* De-initialize all GPIO pins associated with ADC1 */
  HAL_GPIO_DeInit(HAL_GPIOA, HAL_GPIO_PIN_0 | HAL_GPIO_PIN_1 | HAL_GPIO_PIN_2 | HAL_GPIO_PIN_3 | HAL_GPIO_PIN_4 | HAL_GPIO_PIN_5 | HAL_GPIO_PIN_6 | HAL_GPIO_PIN_7);
}

hal_adc_handle_t *mx_adc1_gethandle(void)
{
  return &hADC1;
}
