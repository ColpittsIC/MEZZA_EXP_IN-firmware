/**
  ******************************************************************************
  * @file           : mx_spi2.c
  * @brief          : SPI2 Peripheral initialization
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
#include "mx_spi2.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private functions prototype------------------------------------------------*/
/* Exported variables by reference--------------------------------------------*/

/* Handle for SPI2 */
static hal_spi_handle_t hSPI2;

/* Exported function definition ----------------------------------------------*/

/******************************************************************************/
/* Exported functions for SPI2 in HAL layer */
/******************************************************************************/

hal_spi_handle_t *mx_spi2_init(void)
{
  hal_spi_config_t spi_config;

  /* Basic configuration */
  if (HAL_SPI_Init(&hSPI2, HAL_SPI2) != HAL_OK)
  {
    return NULL;
  }

  HAL_RCC_SPI2_EnableClock();

  if (HAL_RCC_SPI2_SetKernelClkSource(HAL_RCC_SPI2_CLK_SRC_PCLK1) != HAL_OK)
  {
    return NULL;
  }

  /* This board acts as SPI Master: it initiates every transaction on the bus.
     The other board is expected to be configured as SPI Slave. */
  spi_config.mode                = HAL_SPI_MODE_MASTER;
  spi_config.direction           = HAL_SPI_DIRECTION_FULL_DUPLEX;
  spi_config.data_width          = HAL_SPI_DATA_WIDTH_8_BIT;
  spi_config.clock_polarity      = HAL_SPI_CLOCK_POLARITY_LOW;   /* SPI mode 0: CPOL=0 */
  spi_config.clock_phase         = HAL_SPI_CLOCK_PHASE_1_EDGE;   /* SPI mode 0: CPHA=0 */
  spi_config.baud_rate_prescaler = HAL_SPI_BAUD_RATE_PRESCALER_64; /* PCLK1/64 = 2.25 MHz @ 144 MHz PCLK1 */
  spi_config.first_bit           = HAL_SPI_MSB_FIRST;
  spi_config.nss_pin_management  = HAL_SPI_NSS_PIN_MGMT_OUTPUT;  /* Master drives NSS automatically per transfer */

  if (HAL_SPI_SetConfig(&hSPI2, &spi_config) != HAL_OK)
  {
    return NULL;
  }

  HAL_RCC_GPIOB_EnableClock();

  hal_gpio_config_t  gpio_config;

  /**
    SPI2 GPIO Configuration

    [GPIO Pin] ------> [Signal Name]

       PB12    ------>   SPI2_NSS  (CS)
       PB13    ------>   SPI2_SCK
       PB14    ------>   SPI2_MISO
       PB15    ------>   SPI2_MOSI

    Alternate function: AF5 (confirmed).
  **/
  gpio_config.mode        = HAL_GPIO_MODE_ALTERNATE;
  gpio_config.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
  gpio_config.pull        = HAL_GPIO_PULL_NO;
  gpio_config.speed       = HAL_GPIO_SPEED_FREQ_LOW;
  gpio_config.alternate   = HAL_GPIO_AF5_SPI2;
  HAL_GPIO_Init(HAL_GPIOB, HAL_GPIO_PIN_12 | HAL_GPIO_PIN_13 | HAL_GPIO_PIN_14 | HAL_GPIO_PIN_15, &gpio_config);

  /* SPI2 interrupt: enabled here so transfers can be driven fully asynchronously
     (HAL_SPI_TransmitReceive_IT) without blocking the rest of the application. */
  HAL_CORTEX_NVIC_SetPriority(SPI2_IRQn, HAL_CORTEX_NVIC_PREEMP_PRIORITY_5, HAL_CORTEX_NVIC_SUB_PRIORITY_0);
  HAL_CORTEX_NVIC_EnableIRQ(SPI2_IRQn);

  return &hSPI2;
}

void mx_spi2_deinit(void)
{
  HAL_CORTEX_NVIC_DisableIRQ(SPI2_IRQn);

  HAL_SPI_DeInit(&hSPI2);

  HAL_RCC_SPI2_Reset();

  HAL_RCC_SPI2_DisableClock();

  /* De-initialize all GPIO pins associated with SPI2 */
  HAL_GPIO_DeInit(HAL_GPIOB, HAL_GPIO_PIN_12 | HAL_GPIO_PIN_13 | HAL_GPIO_PIN_14 | HAL_GPIO_PIN_15);
}

hal_spi_handle_t *mx_spi2_gethandle(void)
{
  return &hSPI2;
}

/* NOTE: SPI2_IRQHandler() is implemented in main.c, not here, so it can update
   the application's transfer-complete flag right next to the buffers it protects
   (mirrors the USART3 handler for the same reason - see main.c). */
