/**
  ******************************************************************************
  * @file    stm32_external_env.h
  * @brief   External environments values (external oscillators values).
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
#ifndef STM32_EXTERNAL_ENV_H
#define STM32_EXTERNAL_ENV_H

#ifdef __cplusplus
extern "C" {
#endif

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

/* ########################## VDD Value #######################################*/
/**
  * @brief VDD Value.
  */
#if !defined  (VDD_VALUE)
#define  VDD_VALUE             3300UL /*!< Value of VDD in mV */
#endif /* VDD_VALUE */

/* ########################## Oscillator Values adaptation ####################*/

/* Tip: To avoid modifying this file each time you need to use different HSE,
   ===  you can define the HSE value in your toolchain compiler preprocessor. */

/**
  * @brief HSE Value: external crystal on PH0(OSC_IN)/PH1(OSC_OUT), used as the
  *        USB Full-Speed CK48 reference (see mx_rcc.c) - not needed for the
  *        main HSI-based SYSCLK, which stays as configured in mx_rcc.c.
  */
#if !defined (HSE_VALUE)
#define HSE_VALUE               48000000UL /*!< Value of the External oscillator in Hz */
#endif /* HSE_VALUE */

#if !defined (HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT     100UL      /*!< Time out for HSE start up, in ms */
#endif /* HSE_STARTUP_TIMEOUT */

#ifdef __cplusplus
}
#endif

#endif /* STM32_EXTERNAL_ENV_H */
