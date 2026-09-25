/**
  ******************************************************************************
  * @file           : mx_usb.h
  * @brief          : Header for mx_usb.c file.
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
#ifndef MX_USB_H
#define MX_USB_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/
#include "stm32_hal.h"

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/

/* Endpoint addresses used by usb_cdc.c - shared here since mx_usb.c is where
   the PMA layout for each endpoint is configured (HAL_PCD_PMAConfig()), and
   usb_cdc.c needs the same addresses to open/use those endpoints. */
#define MX_USB_CDC_CMD_EP           0x81U   /* interrupt IN: CDC notifications (unused by this test, but part of the class) */
#define MX_USB_CDC_CMD_EP_SIZE      8U
#define MX_USB_CDC_OUT_EP           0x02U   /* bulk OUT: host -> device data */
/* Bulk IN deliberately uses a DIFFERENT physical endpoint number (3) than
   bulk OUT (2), rather than sharing one number for both directions (a
   common, normally-valid pattern): on this specific driver, sharing endpoint
   2 for both directions caused HAL_PCD_DataInStageCallback() to never fire
   for the IN side (confirmed by direct testing - the TX completion interrupt
   simply never arrived, regardless of endpoint state management), so IN
   traffic was either lost or delivered stale/corrupted. Moving IN to its own
   physical endpoint number fixed it outright. */
#define MX_USB_CDC_IN_EP            0x83U   /* bulk IN: device -> host data */
#define MX_USB_CDC_DATA_EP_SIZE     64U     /* full-speed bulk max packet size */

/* Exported macros -----------------------------------------------------------*/
/* Exported variables --------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */

/******************************************************************************/
/* Exported functions for USB (PCD, device mode) in HAL layer */
/******************************************************************************/
/**
  * @brief mx_usb init function
  * This function configures the hardware resources used in this example.
  * Only sets up the USB peripheral (clock, PCD config, endpoint PMA layout,
  * NVIC) - it does NOT start the device (no pull-up, not visible on the bus
  * yet): that is done separately by usb_cdc_start(), from main.c, only when
  * PC_COMM_USE_USB selects the USB link over UART5.
  * @retval pointer to handle or NULL in case of failure
  */
hal_pcd_handle_t *mx_usb_init(void);

/**
  * @brief  De-initialize mx_usb instance and return it.
  * @retval None
  */
void mx_usb_deinit(void);

/**
  * @brief  Get the mx_usb object.
  * @retval Pointer on the mx_usb Handle
  */
hal_pcd_handle_t *mx_usb_gethandle(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MX_USB_H */
