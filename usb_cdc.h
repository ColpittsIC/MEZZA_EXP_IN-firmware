/**
  ******************************************************************************
  * @file    usb_cdc.h
  * @brief   Minimal USB CDC-ACM (Virtual COM Port) device class on top of
  *          stm32c5xx_hal_pcd.c - see usb_cdc.c for the full explanation of
  *          why this is hand-written instead of using ST's USB Device
  *          middleware (not available for this MCU/toolchain yet).
  ******************************************************************************
  */

#ifndef USB_CDC_H
#define USB_CDC_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "stm32_hal.h"

/**
  * @brief  Make the USB CDC Virtual COM Port actually visible/usable on the
  *         bus: wires the given (already mx_usb_init()-configured) PCD
  *         handle into this module and starts the device (asserts the D+
  *         pull-up, so the host begins enumeration). Call this only when the
  *         application actually wants to use USB instead of UART5 for PC
  *         communication (see PC_COMM_USE_USB in main.c) - calling
  *         mx_usb_init() alone does not make the device appear on the bus.
  * @param  hpcd Pointer to the PCD handle from mx_usb_gethandle()
  * @retval none
  */
void usb_cdc_start(hal_pcd_handle_t *hpcd);

/**
  * @brief  Whether the host has finished enumerating the device
  *         (SET_CONFIGURATION received) - i.e. the OS should already show a
  *         COM port for it, independently of whether any application has
  *         actually opened it yet.
  * @retval 1 if configured, 0 otherwise
  */
uint32_t usb_cdc_is_configured(void);

/**
  * @brief  Blocking-ish send over the CDC bulk IN endpoint, chunked into
  *         max-packet-size packets, waiting (bounded busy-wait) for each
  *         packet's completion before queueing the next - mirrors this
  *         project's existing HAL_UART_Transmit(..., UART_TX_TIMEOUT_MS)
  *         blocking-send convention. If the device isn't configured yet (host
  *         hasn't finished enumerating - e.g. right at boot, before Windows/
  *         Linux notices the new device), the data is silently dropped,
  *         exactly like printing to a UART with nothing connected on the
  *         other end.
  * @param  data Buffer to send
  * @param  len  Number of bytes to send
  * @retval none
  */
void usb_cdc_transmit(const uint8_t *data, uint32_t len);

/**
  * @brief  Blocking line read from the CDC bulk OUT endpoint: waits for and
  *         copies one line (up to the next '\n', with a trailing '\r'
  *         stripped if present) received from the host into out_buf
  *         (truncated to out_buf_size - 1, NUL-terminated) - mirrors
  *         uart5_cmd_wait_for_line()'s semantics in main.c exactly, so it can
  *         be used as a drop-in alternative there.
  * @param  out_buf      Destination buffer
  * @param  out_buf_size Size of out_buf, including the NUL terminator
  * @retval none
  */
void usb_cdc_wait_for_line(char *out_buf, uint32_t out_buf_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* USB_CDC_H */
