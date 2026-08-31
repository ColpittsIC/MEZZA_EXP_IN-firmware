/**
  ******************************************************************************
  * @file           : mx_uart5.h
  * @brief          : Header for mx_uart5.c file.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef MX_UART5_H
#define MX_UART5_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/
#include "stm32_hal.h"

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

/* Single source of truth for UART5's baud rate: used both by mx_uart5.c (to
   configure the peripheral) and by main.c (to report it in the ADC quality
   test CONFIG block, and to know what to tell the operator to set their
   terminal / the PC scripts' --baud to).
   921600 was picked as a large, near-universally supported speed-up over the
   classic 115200 (the ADC quality test's 10000-sample-per-point CSV streaming
   is UART-transmission-bound, not ADC-conversion-bound - see
   adc_quality_send_config() in main.c). If your USB-serial adapter supports
   an even higher standard rate, this is the only place to change. */
#define MX_UART5_BAUD_RATE   921600UL

/* Exported macros -----------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */

/******************************************************************************/
/* Exported functions for UART in HAL layer */
/******************************************************************************/
/**
  * @brief mx_uart5_uart init function
  * This function configures the hardware resources used in this example
  * @retval pointer to handle or NULL in case of failure
  */
hal_uart_handle_t *mx_uart5_uart_init(void);

/**
  * @brief  De-initialize mx_uart5_uart instance and return it.
  * @retval None
  */
void mx_uart5_uart_deinit(void);

/**
  * @brief  Get the mx_uart5_uart object.
  * @retval Pointer on the mx_uart5_uartHandle
  */
hal_uart_handle_t *mx_uart5_uart_gethandle(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MX_UART5_H */
