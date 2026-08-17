/**
  ******************************************************************************
  * @file           : mx_uart5.c
  * @brief          : UART5 Peripheral initialization
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
#include "mx_uart5.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private functions prototype------------------------------------------------*/
/* Exported variables by reference--------------------------------------------*/

/* Handle for UART */
static hal_uart_handle_t hUART5;

/* Exported function definition ----------------------------------------------*/

/******************************************************************************/
/* Exported functions for UART in HAL layer */
/******************************************************************************/

hal_uart_handle_t *mx_uart5_uart_init(void)
{
  hal_uart_config_t uart_config;

  /* Basic configuration */
  if (HAL_UART_Init(&hUART5, HAL_UART5) != HAL_OK)
  {
    return NULL;
  }

  HAL_RCC_UART5_EnableClock();

  if (HAL_RCC_UART5_SetKernelClkSource(HAL_RCC_UART5_CLK_SRC_PCLK1) != HAL_OK)
  {
    return NULL;
  }

  uart_config.baud_rate = 115200;
  uart_config.clock_prescaler = HAL_UART_PRESCALER_DIV1;
  uart_config.word_length = HAL_UART_WORD_LENGTH_8_BIT;
  uart_config.stop_bits = HAL_UART_STOP_BIT_1;
  uart_config.parity = HAL_UART_PARITY_NONE;
  uart_config.direction = HAL_UART_DIRECTION_TX_RX;
  uart_config.hw_flow_ctl = HAL_UART_HW_CONTROL_NONE;
  uart_config.oversampling = HAL_UART_OVERSAMPLING_16;
  uart_config.one_bit_sampling = HAL_UART_ONE_BIT_SAMPLE_DISABLE;

  if (HAL_UART_SetConfig(&hUART5, &uart_config) != HAL_OK)
  {
    return NULL;
  }

  HAL_RCC_GPIOB_EnableClock();

  hal_gpio_config_t  gpio_config;

  /**
    UART5 GPIO Configuration

    [GPIO Pin] ------> [Signal Name]

       PB5     ------>   UART5_RX
       PB6     ------>   UART5_TX
    **/
  gpio_config.mode        = HAL_GPIO_MODE_ALTERNATE;
  gpio_config.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
  gpio_config.pull        = HAL_GPIO_PULL_NO;
  gpio_config.speed       = HAL_GPIO_SPEED_FREQ_LOW;
  gpio_config.alternate   = HAL_GPIO_AF_14;
  HAL_GPIO_Init(HAL_GPIOB, HAL_GPIO_PIN_5 | HAL_GPIO_PIN_6, &gpio_config);

  return &hUART5;
}

void mx_uart5_uart_deinit(void)
{
(void)HAL_UART_DeInit(&hUART5);

  HAL_RCC_UART5_Reset();

  HAL_RCC_UART5_DisableClock();

  /* De-initialize all GPIO pins associated with UART5 */
  HAL_GPIO_DeInit(HAL_GPIOB, HAL_GPIO_PIN_5 | HAL_GPIO_PIN_6);
}
hal_uart_handle_t *mx_uart5_uart_gethandle(void)
{
  return &hUART5;
}
