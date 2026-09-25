/**
  ******************************************************************************
  * @file    usb_cdc.c
  * @brief   Minimal USB CDC-ACM (Virtual COM Port) device class on top of
  *          stm32c5xx_hal_pcd.c.
  ******************************************************************************
  *
  * Unlike UART5/USART3/SPI2 (which only needed enabling an existing HAL
  * driver already present in this toolchain - see the project README), there
  * is no ready-made USB Device middleware pack for this MCU/toolchain
  * (ST's classic "STM32_USB_Device_Library" - usbd_core/usbd_cdc/usbd_desc -
  * simply isn't available for this CubeMX2/HAL2 project yet). This file is
  * therefore a hand-written, deliberately minimal CDC-ACM class implementation
  * directly on stm32c5xx_hal_pcd.c's PCD driver, built for exactly this
  * project's needs: a single bulk IN/OUT byte pipe the PC scripts can open as
  * an ordinary serial port (pyserial and friends don't care that it's USB
  * underneath, only that it behaves like a COM port), used as a drop-in
  * alternative to UART5 (see PC_COMM_USE_USB in main.c).
  *
  * Not implemented (deliberately, out of scope for this test tool): CDC
  * notifications on the interrupt IN endpoint (SERIAL_STATE etc. - declared
  * in the descriptor for spec compliance, but never actually sent), a real
  * reaction to SET_LINE_CODING (baud/parity/stop bits are stored but ignored -
  * there is no physical UART behind this port to reconfigure), remote wakeup,
  * suspend/resume handling, and multiple simultaneous configurations/alt
  * settings (there is exactly one of each).
  *
  * Architecture-wise this mirrors the well-established pattern used across
  * ST's own USB Device middleware for every STM32 family sharing this same
  * PCD driver shape (F0/F1/F3/F4/G0/G4/L4/U5/H5): a HAL_PCD_SetupStageCallback()
  * that decodes the raw 8-byte setup packet (read directly from
  * hpcd->setup - see stm32c5xx_hal_pcd.c's USB_DRD_ReadPMA() call into it) and
  * drives control transfers by directly arming EP0 IN/OUT via
  * HAL_PCD_SetEndpointTransmit()/SetEndpointReceive(), plus
  * HAL_PCD_DataInStageCallback()/DataOutStageCallback() for the bulk data
  * endpoints. Re-arming an endpoint directly from inside these callbacks is
  * safe on this driver (same reasoning already established for SPI2 in
  * main.c: the endpoint's transfer state is reset before the callback fires,
  * confirmed by reading stm32c5xx_hal_pcd.c's IRQ handling code).
  */

#include "usb_cdc.h"
#include "mx_usb.h"
#include <string.h>

/* USB standard/CDC constants used below (kept local: this is the only file
   that needs them). */
#define USB_REQ_TYPE_MASK               0x60U
#define USB_REQ_TYPE_STANDARD           0x00U
#define USB_REQ_TYPE_CLASS              0x20U

#define USB_REQ_GET_STATUS              0x00U
#define USB_REQ_CLEAR_FEATURE           0x01U
#define USB_REQ_SET_FEATURE             0x03U
#define USB_REQ_SET_ADDRESS             0x05U
#define USB_REQ_GET_DESCRIPTOR          0x06U
#define USB_REQ_GET_CONFIGURATION       0x08U
#define USB_REQ_SET_CONFIGURATION       0x09U
#define USB_REQ_GET_INTERFACE           0x0AU
#define USB_REQ_SET_INTERFACE           0x0BU

#define CDC_REQ_SET_LINE_CODING         0x20U
#define CDC_REQ_GET_LINE_CODING         0x21U
#define CDC_REQ_SET_CONTROL_LINE_STATE  0x22U

#define USB_DESC_TYPE_DEVICE            0x01U
#define USB_DESC_TYPE_CONFIGURATION     0x02U
#define USB_DESC_TYPE_STRING            0x03U

#define USB_EP0_MPS                     64U

/* Bounded busy-wait for one bulk IN packet to complete, so a host that stops
   reading mid-transfer (e.g. a PC script exiting abruptly) cannot hang the
   firmware forever - generous margin, not a tuned value, same spirit as
   SPI_CURRENT_XFER_WAIT_ATTEMPTS in main.c. */
#define USB_CDC_TX_WAIT_ATTEMPTS        1000000U

/* Raw 8-byte USB setup packet layout (little-endian, matches the wire
   format directly - safe to memcpy from hpcd->setup, which is 4-byte
   aligned). */
typedef struct __attribute__((packed))
{
  uint8_t  bm_request_type;
  uint8_t  b_request;
  uint16_t w_value;
  uint16_t w_index;
  uint16_t w_length;
} usb_setup_req_t;

typedef enum
{
  CDC_CTRL_IDLE = 0,
  CDC_CTRL_WAIT_STATUS_OUT,       /* control-read in progress: the next EP0 OUT is just the host's status ack */
  CDC_CTRL_WAIT_SET_LINE_CODING,  /* control-write in progress: the next EP0 OUT delivers the SET_LINE_CODING payload */
  CDC_CTRL_WAIT_ADDRESS_APPLY,    /* SET_ADDRESS status stage in flight: apply the address once EP0 IN completes */
} usb_cdc_ctrl_state_t;

/* ---- Descriptors (constant, built once at compile time) ------------------ */

static const uint8_t usb_cdc_device_descriptor[18] =
{
  0x12U,                    /* bLength */
  USB_DESC_TYPE_DEVICE,     /* bDescriptorType */
  0x00U, 0x02U,             /* bcdUSB = 2.00 */
  0x02U,                    /* bDeviceClass = CDC */
  0x00U,                    /* bDeviceSubClass */
  0x00U,                    /* bDeviceProtocol */
  USB_EP0_MPS,              /* bMaxPacketSize0 */
  0x83U, 0x04U,             /* idVendor  = 0x0483 (STMicroelectronics) */
  0x40U, 0x57U,             /* idProduct = 0x5740 (ST's well-known Virtual COM Port demo PID) */
  0x00U, 0x02U,             /* bcdDevice = 2.00 */
  0x01U,                    /* iManufacturer */
  0x02U,                    /* iProduct */
  0x00U,                    /* iSerialNumber: none (single test board, not worth the extra code) */
  0x01U                     /* bNumConfigurations */
};

/* Configuration descriptor: 1 configuration, 2 interfaces (CDC Control +
   CDC Data), 3 endpoints (interrupt IN for notifications - declared but
   unused, bulk OUT, bulk IN). wTotalLength = 67 bytes. */
static const uint8_t usb_cdc_config_descriptor[67] =
{
  /* Configuration Descriptor */
  0x09U, 0x02U, 0x43U, 0x00U, 0x02U, 0x01U, 0x00U, 0x80U, 0x32U,

  /* Interface 0: CDC Control */
  0x09U, 0x04U, 0x00U, 0x00U, 0x01U, 0x02U, 0x02U, 0x00U, 0x00U,
  /* CDC Header Functional Descriptor */
  0x05U, 0x24U, 0x00U, 0x10U, 0x01U,
  /* CDC Call Management Functional Descriptor */
  0x05U, 0x24U, 0x01U, 0x00U, 0x01U,
  /* CDC ACM Functional Descriptor (Set/Get Line Coding, Set Control Line State) */
  0x04U, 0x24U, 0x02U, 0x02U,
  /* CDC Union Functional Descriptor */
  0x05U, 0x24U, 0x06U, 0x00U, 0x01U,
  /* Endpoint: notification IN (interrupt) */
  0x07U, 0x05U, MX_USB_CDC_CMD_EP, 0x03U, MX_USB_CDC_CMD_EP_SIZE, 0x00U, 0x10U,

  /* Interface 1: CDC Data */
  0x09U, 0x04U, 0x01U, 0x00U, 0x02U, 0x0AU, 0x00U, 0x00U, 0x00U,
  /* Endpoint: data OUT (bulk) */
  0x07U, 0x05U, MX_USB_CDC_OUT_EP, 0x02U, MX_USB_CDC_DATA_EP_SIZE, 0x00U, 0x00U,
  /* Endpoint: data IN (bulk) */
  0x07U, 0x05U, MX_USB_CDC_IN_EP, 0x02U, MX_USB_CDC_DATA_EP_SIZE, 0x00U, 0x00U,
};

static const uint8_t usb_cdc_lang_id_descriptor[4] = { 0x04U, USB_DESC_TYPE_STRING, 0x09U, 0x04U /* English (US) */ };

/* ---- Module state ---------------------------------------------------------- */

static hal_pcd_handle_t     *g_hpcd = NULL;
static volatile uint32_t     g_configured = 0U;

/* Continuation state for an EP0 IN (control read) data stage longer than one
   max packet (64 bytes) - e.g. the 67-byte configuration descriptor. The
   driver only ever sends ONE packet per HAL_PCD_SetEndpointTransmit() call on
   EP0 and then fires HAL_PCD_DataInStageCallback(hpcd, 0): for a multi-packet
   transfer it is the caller's job to queue the next chunk from inside that
   callback (confirmed by reading stm32c5xx_hal_pcd.c's EP0 IN interrupt
   handling - it advances p_xfer_buffer/xfer_count per packet and calls back
   without ever restarting the transfer itself), mirroring the classic
   USBD_CtlContinueSendData() pattern used across ST's own USB Device stack. */
static const uint8_t *g_ctrl_tx_ptr = NULL;
static uint32_t       g_ctrl_tx_remaining = 0U;
static volatile usb_cdc_ctrl_state_t g_ctrl_state = CDC_CTRL_IDLE;
static volatile uint8_t      g_pending_address = 0U;

/* SET_LINE_CODING/GET_LINE_CODING payload (dwDTERate, bCharFormat,
   bParityType, bDataBits) - stored so GET_LINE_CODING can echo back whatever
   was last set, but otherwise not acted upon (no physical UART to
   reconfigure behind this port). Initialized to a plausible default
   (115200 8N1) purely so a host that queries it before ever setting it sees
   something sane. */
static uint8_t g_line_coding_buf[7] = { 0x00U, 0xC2U, 0x01U, 0x00U, 0x00U, 0x00U, 0x08U };

/* Bulk OUT reception: one packet at a time, single-buffered (no ring buffer -
   see usb_cdc_wait_for_line()'s docstring in usb_cdc.h for why a fixed-size,
   single-consumer design is enough for this project's line-based protocol). */
static volatile uint8_t  cdc_rx_pkt_buf[MX_USB_CDC_DATA_EP_SIZE];
static volatile uint32_t cdc_rx_pkt_len = 0U;
static volatile uint32_t cdc_rx_pkt_ready = 0U;

/* Bulk IN transmission: cleared by HAL_PCD_DataInStageCallback() once the
   packet queued by usb_cdc_transmit() has actually gone out. */
static volatile uint32_t cdc_tx_busy = 0U;

/* ---- Helpers ---------------------------------------------------------------- */

/* Builds a USB string descriptor (bLength, bDescriptorType=STRING, then the
   string as UTF-16LE) from a plain ASCII C string into out_buf, which must be
   at least 2 + 2*strlen(ascii) bytes. Returns the descriptor's bLength. Only
   ASCII input is expected here (fixed strings below), so the upper byte of
   every UTF-16 code unit is always 0x00. */
static uint16_t usb_cdc_build_string_descriptor(const char *ascii, uint8_t *out_buf)
{
  uint16_t len = (uint16_t)strlen(ascii);
  uint16_t i;

  out_buf[0] = (uint8_t)(2U + (2U * len));
  out_buf[1] = USB_DESC_TYPE_STRING;
  for (i = 0U; i < len; i++)
  {
    out_buf[2U + (2U * i)] = (uint8_t)ascii[i];
    out_buf[3U + (2U * i)] = 0x00U;
  }
  return out_buf[0];
}

/* Start (or continue) an EP0 IN data stage of arbitrary length, one max
   packet (64 bytes) at a time - see the g_ctrl_tx_ptr/g_ctrl_tx_remaining
   comment above. Called once from HAL_PCD_SetupStageCallback() for a
   control-read request, then again from HAL_PCD_DataInStageCallback()
   for every subsequent packet until the whole thing has gone out. A
   final packet shorter than 64 bytes (guaranteed here since none of this
   device's responses are an exact multiple of 64 bytes) naturally
   terminates the data stage per the USB spec, no ZLP needed. */
static void usb_ctrl_send_data(hal_pcd_handle_t *hpcd, const uint8_t *data, uint32_t len)
{
  uint32_t chunk = (len > USB_EP0_MPS) ? USB_EP0_MPS : len;

  g_ctrl_tx_ptr = data + chunk;
  g_ctrl_tx_remaining = len - chunk;

  (void)HAL_PCD_SetEndpointTransmit(hpcd, 0x80U, (uint8_t *)data, chunk);
}

static void usb_cdc_open_endpoints(hal_pcd_handle_t *hpcd)
{
  (void)HAL_PCD_OpenEndpoint(hpcd, MX_USB_CDC_CMD_EP, MX_USB_CDC_CMD_EP_SIZE, HAL_PCD_EP_TYPE_INTR);
  (void)HAL_PCD_OpenEndpoint(hpcd, MX_USB_CDC_OUT_EP, MX_USB_CDC_DATA_EP_SIZE, HAL_PCD_EP_TYPE_BULK);
  (void)HAL_PCD_OpenEndpoint(hpcd, MX_USB_CDC_IN_EP, MX_USB_CDC_DATA_EP_SIZE, HAL_PCD_EP_TYPE_BULK);

  cdc_rx_pkt_len = 0U;
  cdc_rx_pkt_ready = 0U;
  (void)HAL_PCD_SetEndpointReceive(hpcd, MX_USB_CDC_OUT_EP, (uint8_t *)cdc_rx_pkt_buf, MX_USB_CDC_DATA_EP_SIZE);
}

/* ---- Public API ------------------------------------------------------------- */

void usb_cdc_start(hal_pcd_handle_t *hpcd)
{
  g_hpcd = hpcd;
  g_configured = 0U;
  g_ctrl_state = CDC_CTRL_IDLE;
  cdc_rx_pkt_ready = 0U;
  cdc_tx_busy = 0U;

  (void)HAL_PCD_Start(hpcd);
}

uint32_t usb_cdc_is_configured(void)
{
  return g_configured;
}

void usb_cdc_transmit(const uint8_t *data, uint32_t len)
{
  uint32_t offset;

  if ((g_hpcd == NULL) || (usb_cdc_is_configured() == 0U))
  {
    return; /* nobody listening yet (or ever) - drop, like a UART with nothing wired up */
  }

  offset = 0U;
  while (offset < len)
  {
    uint32_t chunk = len - offset;
    uint32_t attempts;

    if (chunk > MX_USB_CDC_DATA_EP_SIZE)
    {
      chunk = MX_USB_CDC_DATA_EP_SIZE;
    }

    cdc_tx_busy = 1U;
    if (HAL_PCD_SetEndpointTransmit(g_hpcd, MX_USB_CDC_IN_EP, (uint8_t *)&data[offset], chunk) != HAL_OK)
    {
      return;
    }

    attempts = 0U;
    while ((cdc_tx_busy != 0U) && (attempts < USB_CDC_TX_WAIT_ATTEMPTS))
    {
      attempts++;
    }
    if (cdc_tx_busy != 0U)
    {
      return; /* host stopped reading: give up rather than hang forever */
    }

    offset += chunk;
  }
}

void usb_cdc_wait_for_line(char *out_buf, uint32_t out_buf_size)
{
  static char     line_buf[128];
  static uint32_t line_len = 0U;
  static uint32_t pkt_pos = 0U;   /* consumer-only: read position within cdc_rx_pkt_buf */

  for (;;)
  {
    if (cdc_rx_pkt_ready == 0U)
    {
      continue; /* wait for HAL_PCD_DataOutStageCallback() to deliver a packet */
    }

    while (pkt_pos < cdc_rx_pkt_len)
    {
      uint8_t c = cdc_rx_pkt_buf[pkt_pos];
      pkt_pos++;

      if (c == (uint8_t)'\n')
      {
        uint32_t copy_len;

        while ((line_len > 0U) && (line_buf[line_len - 1U] == '\r'))
        {
          line_len--;
        }
        copy_len = line_len;
        if (copy_len > (out_buf_size - 1U))
        {
          copy_len = out_buf_size - 1U;
        }
        memcpy(out_buf, line_buf, copy_len);
        out_buf[copy_len] = '\0';
        line_len = 0U;
        return; /* any bytes left in this packet stay buffered (pkt_pos) for the next call */
      }
      else if (line_len < (sizeof(line_buf) - 1U))
      {
        line_buf[line_len] = (char)c;
        line_len++;
      }
      else
      {
        /* overlong line: silently drop extra bytes, same spirit as the
           fixed-size UART command buffers elsewhere in this project */
      }
    }

    /* fully consumed this packet: re-arm the endpoint for the next one */
    pkt_pos = 0U;
    cdc_rx_pkt_ready = 0U;
    (void)HAL_PCD_SetEndpointReceive(g_hpcd, MX_USB_CDC_OUT_EP, (uint8_t *)cdc_rx_pkt_buf, MX_USB_CDC_DATA_EP_SIZE);
  }
}

/* ---- HAL_PCD callbacks ------------------------------------------------------ */

void HAL_PCD_ResetCallback(hal_pcd_handle_t *hpcd)
{
  g_configured = 0U;
  g_ctrl_state = CDC_CTRL_IDLE;

  (void)HAL_PCD_SetDeviceAddress(hpcd, 0U);
  (void)HAL_PCD_OpenEndpoint(hpcd, 0x00U, USB_EP0_MPS, HAL_PCD_EP_TYPE_CTRL);
  (void)HAL_PCD_OpenEndpoint(hpcd, 0x80U, USB_EP0_MPS, HAL_PCD_EP_TYPE_CTRL);
}

void HAL_PCD_SetupStageCallback(hal_pcd_handle_t *hpcd)
{
  usb_setup_req_t req;
  uint8_t         req_type;

  memcpy(&req, (const void *)hpcd->setup, sizeof(req));
  req_type = req.bm_request_type & USB_REQ_TYPE_MASK;

  if (req_type == USB_REQ_TYPE_STANDARD)
  {
    switch (req.b_request)
    {
      case USB_REQ_GET_STATUS:
      {
        static const uint8_t status[2] = { 0x00U, 0x00U };
        usb_ctrl_send_data(hpcd, status, 2U);
        (void)HAL_PCD_SetEndpointReceive(hpcd, 0x00U, NULL, 0U);
        g_ctrl_state = CDC_CTRL_WAIT_STATUS_OUT;
        break;
      }

      case USB_REQ_SET_ADDRESS:
        /* Applied only after the status stage completes (see
           HAL_PCD_DataInStageCallback()) - the device must still answer as
           address 0 for that stage, per the USB spec. */
        g_pending_address = (uint8_t)(req.w_value & 0x7FU);
        (void)HAL_PCD_SetEndpointTransmit(hpcd, 0x80U, NULL, 0U);
        g_ctrl_state = CDC_CTRL_WAIT_ADDRESS_APPLY;
        break;

      case USB_REQ_GET_DESCRIPTOR:
      {
        uint8_t        desc_type = (uint8_t)(req.w_value >> 8);
        uint8_t        desc_index = (uint8_t)(req.w_value & 0xFFU);
        const uint8_t *desc = NULL;
        uint16_t       desc_len = 0U;
        static uint8_t string_buf[64];

        if (desc_type == USB_DESC_TYPE_DEVICE)
        {
          desc = usb_cdc_device_descriptor;
          desc_len = (uint16_t)sizeof(usb_cdc_device_descriptor);
        }
        else if (desc_type == USB_DESC_TYPE_CONFIGURATION)
        {
          desc = usb_cdc_config_descriptor;
          desc_len = (uint16_t)sizeof(usb_cdc_config_descriptor);
        }
        else if (desc_type == USB_DESC_TYPE_STRING)
        {
          if (desc_index == 0U)
          {
            desc = usb_cdc_lang_id_descriptor;
            desc_len = (uint16_t)sizeof(usb_cdc_lang_id_descriptor);
          }
          else if (desc_index == 1U)
          {
            desc_len = usb_cdc_build_string_descriptor("MEZZA_EXP_IN", string_buf);
            desc = string_buf;
          }
          else if (desc_index == 2U)
          {
            desc_len = usb_cdc_build_string_descriptor("MEZZA_EXP_IN Virtual COM Port", string_buf);
            desc = string_buf;
          }
          else
          {
            /* unknown string index: fall through to the stall below */
          }
        }
        else
        {
          /* unsupported descriptor type: fall through to the stall below */
        }

        if (desc != NULL)
        {
          uint16_t send_len = (desc_len < req.w_length) ? desc_len : req.w_length;
          usb_ctrl_send_data(hpcd, desc, send_len);
          (void)HAL_PCD_SetEndpointReceive(hpcd, 0x00U, NULL, 0U);
          g_ctrl_state = CDC_CTRL_WAIT_STATUS_OUT;
        }
        else
        {
          (void)HAL_PCD_SetEndpointStall(hpcd, 0x00U);
          (void)HAL_PCD_SetEndpointStall(hpcd, 0x80U);
        }
        break;
      }

      case USB_REQ_GET_CONFIGURATION:
      {
        static uint8_t cfg;
        cfg = (g_configured != 0U) ? 1U : 0U;
        usb_ctrl_send_data(hpcd, &cfg, 1U);
        (void)HAL_PCD_SetEndpointReceive(hpcd, 0x00U, NULL, 0U);
        g_ctrl_state = CDC_CTRL_WAIT_STATUS_OUT;
        break;
      }

      case USB_REQ_SET_CONFIGURATION:
        if ((req.w_value & 0xFFU) != 0U)
        {
          usb_cdc_open_endpoints(hpcd);
          g_configured = 1U;
        }
        else
        {
          g_configured = 0U;
        }
        (void)HAL_PCD_SetEndpointTransmit(hpcd, 0x80U, NULL, 0U);
        break;

      case USB_REQ_GET_INTERFACE:
      {
        static const uint8_t alt_setting = 0U;
        usb_ctrl_send_data(hpcd, &alt_setting, 1U);
        (void)HAL_PCD_SetEndpointReceive(hpcd, 0x00U, NULL, 0U);
        g_ctrl_state = CDC_CTRL_WAIT_STATUS_OUT;
        break;
      }

      case USB_REQ_SET_INTERFACE:
      case USB_REQ_CLEAR_FEATURE:
      case USB_REQ_SET_FEATURE:
        /* Single altsetting/no optional features to track: just ack. */
        (void)HAL_PCD_SetEndpointTransmit(hpcd, 0x80U, NULL, 0U);
        break;

      default:
        (void)HAL_PCD_SetEndpointStall(hpcd, 0x00U);
        (void)HAL_PCD_SetEndpointStall(hpcd, 0x80U);
        break;
    }
  }
  else if (req_type == USB_REQ_TYPE_CLASS)
  {
    switch (req.b_request)
    {
      case CDC_REQ_SET_LINE_CODING:
        (void)HAL_PCD_SetEndpointReceive(hpcd, 0x00U, g_line_coding_buf, (uint32_t)sizeof(g_line_coding_buf));
        g_ctrl_state = CDC_CTRL_WAIT_SET_LINE_CODING;
        break;

      case CDC_REQ_GET_LINE_CODING:
        usb_ctrl_send_data(hpcd, g_line_coding_buf, (uint32_t)sizeof(g_line_coding_buf));
        (void)HAL_PCD_SetEndpointReceive(hpcd, 0x00U, NULL, 0U);
        g_ctrl_state = CDC_CTRL_WAIT_STATUS_OUT;
        break;

      case CDC_REQ_SET_CONTROL_LINE_STATE:
        /* DTR/RTS state - nothing physical behind this port to drive, just ack. */
        (void)HAL_PCD_SetEndpointTransmit(hpcd, 0x80U, NULL, 0U);
        break;

      default:
        (void)HAL_PCD_SetEndpointStall(hpcd, 0x00U);
        (void)HAL_PCD_SetEndpointStall(hpcd, 0x80U);
        break;
    }
  }
  else
  {
    /* Vendor requests: none defined. */
    (void)HAL_PCD_SetEndpointStall(hpcd, 0x00U);
    (void)HAL_PCD_SetEndpointStall(hpcd, 0x80U);
  }
}

void HAL_PCD_DataOutStageCallback(hal_pcd_handle_t *hpcd, uint8_t ep_num)
{
  if (ep_num == 0U)
  {
    if (g_ctrl_state == CDC_CTRL_WAIT_SET_LINE_CODING)
    {
      /* g_line_coding_buf now holds the host's 7-byte payload - stored, not
         otherwise acted upon (see the file-level comment). */
      (void)HAL_PCD_SetEndpointTransmit(hpcd, 0x80U, NULL, 0U); /* status stage */
    }
    /* else: this is just the host's status-stage ack for a control-read
       (GET_DESCRIPTOR/GET_STATUS/GET_LINE_CODING/...) - nothing to do. */
    g_ctrl_state = CDC_CTRL_IDLE;
    return;
  }

  if (ep_num == (uint8_t)(MX_USB_CDC_OUT_EP & 0x7FU))
  {
    uint32_t rx_count = HAL_PCD_EP_GetRxCount(hpcd, MX_USB_CDC_OUT_EP);
    if (rx_count > MX_USB_CDC_DATA_EP_SIZE)
    {
      rx_count = MX_USB_CDC_DATA_EP_SIZE; /* defensive: cannot actually happen */
    }
    cdc_rx_pkt_len = rx_count;
    cdc_rx_pkt_ready = 1U;
    /* Deliberately NOT re-armed here: usb_cdc_wait_for_line() re-arms once it
       has fully consumed cdc_rx_pkt_buf, so a second packet can never
       overwrite a still-unread one. */
  }
}

void HAL_PCD_DataInStageCallback(hal_pcd_handle_t *hpcd, uint8_t ep_num)
{
  if (ep_num == 0U)
  {
    if (g_ctrl_tx_remaining > 0U)
    {
      /* More of a multi-packet control-read data stage to send (see
         usb_ctrl_send_data()'s comment) - queue the next packet and stop
         here: this is NOT the end of the data stage yet, so none of the
         status-stage/address-apply follow-ups below apply. */
      const uint8_t *ptr = g_ctrl_tx_ptr;
      uint32_t       chunk = (g_ctrl_tx_remaining > USB_EP0_MPS) ? USB_EP0_MPS : g_ctrl_tx_remaining;

      g_ctrl_tx_ptr += chunk;
      g_ctrl_tx_remaining -= chunk;
      (void)HAL_PCD_SetEndpointTransmit(hpcd, 0x80U, (uint8_t *)ptr, chunk);
      return;
    }

    if (g_ctrl_state == CDC_CTRL_WAIT_ADDRESS_APPLY)
    {
      (void)HAL_PCD_SetDeviceAddress(hpcd, g_pending_address);
      g_ctrl_state = CDC_CTRL_IDLE;
    }
    return;
  }

  if (ep_num == (uint8_t)(MX_USB_CDC_IN_EP & 0x7FU))
  {
    cdc_tx_busy = 0U;
  }
}
