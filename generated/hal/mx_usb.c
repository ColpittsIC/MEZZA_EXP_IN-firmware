/**
  ******************************************************************************
  * @file           : mx_usb.c
  * @brief          : USB (PCD, device mode) Peripheral initialization
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
#include "mx_usb.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* PMA (Packet Memory Area) byte offsets for each endpoint direction - single
   buffered (no double-buffering), well within the 2 Kbyte PMA available on
   this device. EP0 is control (64 bytes each way); the CDC endpoints use the
   addresses/sizes shared with usb_cdc.c via mx_usb.h. */
#define MX_USB_EP0_OUT_PMA_ADDR      0x0008U
#define MX_USB_EP0_IN_PMA_ADDR       0x0048U
#define MX_USB_CDC_CMD_EP_PMA_ADDR   0x0088U
#define MX_USB_CDC_OUT_EP_PMA_ADDR   0x0090U
#define MX_USB_CDC_IN_EP_PMA_ADDR    0x00D0U

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private functions prototype------------------------------------------------*/
/* Exported variables by reference--------------------------------------------*/

/* Handle for USB (PCD) */
static hal_pcd_handle_t hUSB_PCD;

/* Exported function definition ----------------------------------------------*/

/******************************************************************************/
/* Exported functions for USB in HAL layer */
/******************************************************************************/

hal_pcd_handle_t *mx_usb_init(void)
{
  hal_pcd_config_t pcd_config;

  /* Configure hUSB_PCD */
  if (HAL_PCD_Init(&hUSB_PCD, HAL_PCD_DRD_FS) != HAL_OK)
  {
    return NULL;
  }

  HAL_RCC_USB_EnableClock();

  pcd_config.dma_enable = HAL_PCD_DMA_DISABLED;
  pcd_config.pcd_speed = HAL_PCD_SPEED_FS;
  pcd_config.phy_interface = HAL_PCD_PHY_EMBEDDED_FS;
  pcd_config.sof_enable = HAL_PCD_SOF_DISABLED;
  pcd_config.lpm_enable = HAL_PCD_LPM_DISABLED;
  pcd_config.battery_charging_enable = HAL_PCD_BCD_DISABLED;
  /* No dedicated VBUS-sensing pin wired on this board: the device just
     assumes it is powered/attached whenever the MCU itself is running. */
  pcd_config.vbus_sensing_enable = HAL_PCD_VBUS_SENSE_DISABLED;
  pcd_config.bulk_doublebuffer_enable = HAL_PCD_BULK_DB_DISABLED;

  if (HAL_PCD_SetConfig(&hUSB_PCD, &pcd_config) != HAL_OK)
  {
    return NULL;
  }

  /* PMA layout: one entry per endpoint direction actually used (EP0 control,
     plus the 3 CDC endpoints - see mx_usb.h). Endpoint OPEN (type/size) is
     done later by usb_cdc.c, once the class logic is ready to run; this only
     reserves where in PMA each one lives. */
  HAL_PCD_PMAConfig(&hUSB_PCD, 0x00U, HAL_PCD_SNG_BUF, MX_USB_EP0_OUT_PMA_ADDR);
  HAL_PCD_PMAConfig(&hUSB_PCD, 0x80U, HAL_PCD_SNG_BUF, MX_USB_EP0_IN_PMA_ADDR);
  HAL_PCD_PMAConfig(&hUSB_PCD, MX_USB_CDC_CMD_EP, HAL_PCD_SNG_BUF, MX_USB_CDC_CMD_EP_PMA_ADDR);
  HAL_PCD_PMAConfig(&hUSB_PCD, MX_USB_CDC_OUT_EP, HAL_PCD_SNG_BUF, MX_USB_CDC_OUT_EP_PMA_ADDR);
  HAL_PCD_PMAConfig(&hUSB_PCD, MX_USB_CDC_IN_EP, HAL_PCD_SNG_BUF, MX_USB_CDC_IN_EP_PMA_ADDR);

  /* USB interrupt: enabled so enumeration/transfers are handled fully
     asynchronously (HAL_PCD_IRQHandler), without polling. Harmless to leave
     enabled even if PC_COMM_USE_USB selects UART5 instead at the application
     level (see main.c) - the device simply never gets started/connected in
     that case (see usb_cdc_start()), so this ISR stays idle. */
  HAL_CORTEX_NVIC_SetPriority(USB_DRD_FS_IRQn, HAL_CORTEX_NVIC_PREEMP_PRIORITY_6, HAL_CORTEX_NVIC_SUB_PRIORITY_0);
  HAL_CORTEX_NVIC_EnableIRQ(USB_DRD_FS_IRQn);

  return &hUSB_PCD;
}

void mx_usb_deinit(void)
{
  HAL_CORTEX_NVIC_DisableIRQ(USB_DRD_FS_IRQn);

  HAL_PCD_DeInit(&hUSB_PCD);

  HAL_RCC_USB_Reset();

  HAL_RCC_USB_DisableClock();
}

hal_pcd_handle_t *mx_usb_gethandle(void)
{
  return &hUSB_PCD;
}

/******************************************************************************/
/*                     Interruption and Exception Handlers                    */
/******************************************************************************/
void USB_DRD_FS_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&hUSB_PCD);
}
