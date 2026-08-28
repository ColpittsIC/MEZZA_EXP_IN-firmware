/**
  ******************************************************************************
  * file           : main.c
  * brief          : Main program body
  *                  Calls target system initialization then loop in main.
  ******************************************************************************
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>
#include <string.h>

/* ===========================================================================
   BUILD MODE FLAGS - set before building, then reflash.
   =========================================================================== */

/* 0 = normal demo firmware (ADC/LED/USART3/SPI2 loop, this file's original
       behaviour).
   1 = run the ADC quality-test procedure selected by ADC_QUALITY_TEST_ID
       instead: the other tests (LED, USART3, SPI2) are not started, and
       main() never returns to the normal loop. */
#define TEST_ADC_QUALITY         1U

/* Which ADC quality-test procedure to run when TEST_ADC_QUALITY == 1.
   Only TEST_1 exists today; the value/dispatch is kept so more can be added
   later (e.g. TEST_2 for a different acquisition pattern) without reworking
   this switch. */
#define ADC_QUALITY_TEST_ID      1U

/* Private typedef -----------------------------------------------------------*/
typedef struct
{
  const char *label;       /* Pin / ADC channel description                       */
  int32_t     raw;         /* Raw ADC conversion result (12-bit)                  */
  int32_t     adc_mv;      /* Voltage measured at the ADC pin (mV)                */
  int32_t     input_mv;    /* Voltage at the board input, before attenuation (mV) */
} adc_channel_result_t;

typedef struct
{
  uint8_t     adc_number;   /* 1 or 2 (which physical ADC instance), for labels only */
  uint8_t     channel_idx;  /* 0-based index (= sequencer rank - 1) within that ADC's
                                existing regular sequence configured in mx_adc1.c/mx_adc2.c */
  const char *pin_label;    /* e.g. "PA0", for messages and for the PC script's filenames */
} adc_quality_channel_t;

typedef struct
{
  hal_gpio_t  port;
  uint32_t    pin;
  const char *label;       /* Pin / signal description */
} led_row_pin_t;

typedef struct
{
  uint8_t     anode_row;   /* Index into led_row_pins[] (0=ROW1 .. 3=ROW4): driven HIGH */
  uint8_t     cathode_row; /* Index into led_row_pins[] (0=ROW1 .. 3=ROW4): driven LOW  */
  const char *label;       /* LED reference designator */
} led_entry_t;

/* Private define ------------------------------------------------------------*/
#define ADC_VREF_MV             3300U  /* External VREF+ = 3.3 V                       */

/* Input-to-ADC attenuation factor = 0.2875 (10 V in -> 2.875 V at the ADC pin).
   To recover the input voltage from the ADC voltage without floating point:
   input_mV = adc_mV / 0.2875 = adc_mV * ATTEN_FACTOR_NUM / ATTEN_FACTOR_DEN */
#define ATTEN_FACTOR_NUM         10000U
#define ATTEN_FACTOR_DEN         2875U

#define ADC1_NB_CHANNELS         8U
#define ADC2_NB_CHANNELS         2U
#define ADC_TOTAL_CHANNELS       (ADC1_NB_CHANNELS + ADC2_NB_CHANNELS)

/* Informational only (sent to the PC in the ADC quality test's CONFIG block -
   see adc_quality_send_config()); keep these in sync by hand with the actual
   configuration in mx_rcc.c (PSIS 144 MHz / ADC_DAC prescaler 4) and
   mx_adc1.c / mx_adc2.c (HAL_ADC_SAMPLING_TIME_48CYCLES). */
#define ADC_RESOLUTION_BITS         12U
#define ADC_KERNEL_CLOCK_HZ         36000000UL  /* 144 MHz PSIS / 4 */
#define ADC_SAMPLING_TIME_CYCLES    48U
#define ADC_CONV_CYCLES_X10         125U        /* 12.5 SAR conversion cycles, x10 to avoid float */

#define ADC_CONV_TIMEOUT_MS      100U
#define UART_TX_TIMEOUT_MS       100U
#define TEST_PERIOD_MS           1000U

#define LED_ROW_COUNT            4U
#define LED_COUNT                10U  /* DL2..DL11 */

#define USART3_RX_BUF_SIZE       64U
#define USART3_TX_BUF_SIZE       32U
#define USART3_NO_MESSAGE_TEXT   "No-Message"

/* UART5 command channel (used by the ADC quality test to receive "go ahead"
   commands from the PC script; content is currently ignored, only the fact
   that *something* was received matters - see uart5_cmd_wait_for_command()). */
#define UART5_CMD_RX_BUF_SIZE    32U

/* ADC quality test (TEST_1): number of samples acquired per (channel, voltage)
   point, and the list of nominal voltages the operator is expected to apply
   at each step (labels only - the firmware has no way to verify the actual
   voltage, it just reports what it measures). */
#define ADC_QUALITY_SAMPLES_PER_POINT   10000U
#if TEST_ADC_QUALITY
static const uint32_t adc_quality_voltages_v[] = { 0U, 2U, 4U, 6U, 8U, 10U };
#define ADC_QUALITY_VOLTAGE_COUNT       (sizeof(adc_quality_voltages_v) / sizeof(adc_quality_voltages_v[0]))
#endif /* TEST_ADC_QUALITY */

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static adc_channel_result_t adc_results[ADC_TOTAL_CHANNELS] =
{
  { "PA0 / ADC1_IN0", 0, 0, 0 },
  { "PA1 / ADC1_IN1", 0, 0, 0 },
  { "PA2 / ADC1_IN2", 0, 0, 0 },
  { "PA3 / ADC1_IN3", 0, 0, 0 },
  { "PA4 / ADC1_IN4", 0, 0, 0 },
  { "PA5 / ADC1_IN5", 0, 0, 0 },
  { "PA6 / ADC1_IN6", 0, 0, 0 },
  { "PA7 / ADC1_IN7", 0, 0, 0 },
  { "PB0 / ADC2_IN6", 0, 0, 0 },
  { "PB1 / ADC2_IN7", 0, 0, 0 },
};

#if !TEST_ADC_QUALITY
/* Charlieplexing drive pins: index order defines the (high,low) pairs below. */
static const led_row_pin_t led_row_pins[LED_ROW_COUNT] =
{
  { HAL_GPIOB, HAL_GPIO_PIN_7,  "LED_ROW1 (PB7)"  },
  { HAL_GPIOC, HAL_GPIO_PIN_13, "LED_ROW2 (PC13)" },
  { HAL_GPIOA, HAL_GPIO_PIN_15, "LED_ROW3 (PA15)" },
  { HAL_GPIOC, HAL_GPIO_PIN_14, "LED_ROW4 (PC14)" },
};

/* Charlieplexed LED matrix netlist (from schematic), row indices 0=ROW1..3=ROW4.
   Ordered DL2..DL11 to match the board's top-to-bottom, left-to-right layout. */
static const led_entry_t leds[LED_COUNT] =
{
  { 1U, 0U, "DL2"  }, /* anode=ROW2, cathode=ROW1 */
  { 2U, 0U, "DL3"  }, /* anode=ROW3, cathode=ROW1 */
  { 3U, 0U, "DL4"  }, /* anode=ROW4, cathode=ROW1 */
  { 0U, 1U, "DL5"  }, /* anode=ROW1, cathode=ROW2 */
  { 2U, 1U, "DL6"  }, /* anode=ROW3, cathode=ROW2 */
  { 3U, 1U, "DL7"  }, /* anode=ROW4, cathode=ROW2 */
  { 0U, 2U, "DL8"  }, /* anode=ROW1, cathode=ROW3 */
  { 1U, 2U, "DL9"  }, /* anode=ROW2, cathode=ROW3 */
  { 3U, 2U, "DL10" }, /* anode=ROW4, cathode=ROW3 */
  { 0U, 3U, "DL11" }, /* anode=ROW1, cathode=ROW4 */
};
#endif /* !TEST_ADC_QUALITY */

/* USART3 (PB3/PB4) link to the other microcontroller: TX and RX are both fully
   interrupt-driven (no polling/timeout on this side). usart3_rx_buf is written
   directly by the HAL from IRQ context, so it must live for as long as a
   reception can be pending; usart3_last_message / usart3_message_ready form a
   single-producer (ISR) / single-consumer (main loop) mailbox, made safe by
   briefly masking the USART3 IRQ while it is consumed (see usart3_take_message). */
static uint8_t          usart3_rx_buf[USART3_RX_BUF_SIZE];
static char              usart3_last_message[USART3_RX_BUF_SIZE + 1U];
static volatile uint32_t usart3_message_ready = 0U;
static volatile uint32_t usart3_rx_need_rearm = 0U;
#if !TEST_ADC_QUALITY
static char              usart3_tx_buf[USART3_TX_BUF_SIZE];
#endif /* !TEST_ADC_QUALITY */

/* SPI2 (PB12..PB15) link to the other microcontroller: this board is SPI Master,
   the other board is expected to be SPI Slave. One full-duplex byte is exchanged
   per second, fully interrupt-driven (HAL_SPI_TransmitReceive_IT); the transfer
   is only ever (re-)started from the main loop, once per second, so - unlike
   USART3 - there is no need to re-arm anything from inside the completion
   callback: by the time the next transfer is kicked off, the previous one is
   long finished and the SPI state is back to idle.
   NOTE (full-duplex pipelining): the byte read back on any given transfer is
   whatever the slave had already loaded *before* that transfer started, i.e. it
   reflects the slave's reply to the *previous* byte we sent, not the one just
   sent - this is a normal property of synchronous full-duplex SPI, not a bug. */
static volatile uint32_t spi2_xfer_complete = 0U;
#if !TEST_ADC_QUALITY
static uint8_t           spi2_tx_byte = 0U;
static uint8_t           spi2_rx_byte = 0U;
#endif /* !TEST_ADC_QUALITY */

/* UART5 command channel: interrupt-driven reception of "go ahead" commands from
   the PC (ADC quality test script), same mailbox pattern as USART3 above. */
static uint8_t           uart5_cmd_rx_buf[UART5_CMD_RX_BUF_SIZE];
static volatile uint32_t uart5_cmd_ready = 0U;
static volatile uint32_t uart5_rx_need_rearm = 0U;

#if TEST_ADC_QUALITY
/* ADC quality test (TEST_1): the 10 channels under test, in the order they will
   be exercised - ADC1 rank1..8 (PA0..PA7), then ADC2 rank1..2 (PB0..PB1),
   matching the sequencer configuration in mx_adc1.c / mx_adc2.c. */
static const adc_quality_channel_t adc_quality_channels[] =
{
  { 1U, 0U, "PA0" }, { 1U, 1U, "PA1" }, { 1U, 2U, "PA2" }, { 1U, 3U, "PA3" },
  { 1U, 4U, "PA4" }, { 1U, 5U, "PA5" }, { 1U, 6U, "PA6" }, { 1U, 7U, "PA7" },
  { 2U, 0U, "PB0" }, { 2U, 1U, "PB1" },
};
#define ADC_QUALITY_CHANNEL_COUNT (sizeof(adc_quality_channels) / sizeof(adc_quality_channels[0]))
#endif /* TEST_ADC_QUALITY */

/* Private functions prototype -----------------------------------------------*/
static void uart_send_string(hal_uart_handle_t *huart, const char *p_str);
static void adc_test_error(hal_uart_handle_t *huart, const char *p_reason);
static const char *hal_status_to_str(hal_status_t status);
static uint32_t adc_read_group(hal_uart_handle_t *huart, const char *p_adc_name, hal_adc_handle_t *hadc,
                                uint8_t nb_channels, adc_channel_result_t *p_results);
#if !TEST_ADC_QUALITY
static void adc_send_results_uart(hal_uart_handle_t *huart);

static void led_charlie_init(void);
static void led_charlie_apply_step(uint32_t step);
static void led_charlie_report_step(hal_uart_handle_t *huart, uint32_t step, const char *p_usart3_msg);

static void usart3_arm_receive(hal_uart_handle_t *husart3);
static void usart3_send_ping(hal_uart_handle_t *husart3);
static uint32_t usart3_take_message(char *out_buf, uint32_t out_buf_size);

static void spi2_start_xfer(hal_spi_handle_t *hspi2);
static void spi2_report(hal_uart_handle_t *huart);
#endif /* !TEST_ADC_QUALITY */

#if TEST_ADC_QUALITY
static void uart5_cmd_arm_receive(hal_uart_handle_t *huart5);
static void uart5_cmd_wait_for_command(void);
static void adc_quality_send_config(hal_uart_handle_t *huart5);
static void adc_quality_run_test_1(hal_uart_handle_t *huart5, hal_adc_handle_t *hadc1, hal_adc_handle_t *hadc2);
#endif /* TEST_ADC_QUALITY */

/**
  * brief:  The application entry point.
  * retval: none but we specify int to comply with C99 standard
  */
int main(void)
{
  /** System Init: this code placed in targets folder initializes your system.
    * It calls the initialization (and sets the initial configuration) of the peripherals.
    * You can use STM32CubeMX to generate and call this code or not in this project.
    * It also contains the HAL initialization and the initial clock configuration.
    */
  if (mx_system_init() != SYSTEM_OK)
  {
    return (-1);
  }
  else
  {
    /*
      * You can start your application code here
      */
    hal_adc_handle_t  *hadc1   = mx_adc1_gethandle();
    hal_adc_handle_t  *hadc2   = mx_adc2_gethandle();
    hal_uart_handle_t *huart5  = mx_uart5_uart_gethandle();
#if !TEST_ADC_QUALITY
    hal_uart_handle_t *husart3 = mx_usart3_uart_gethandle();
    hal_spi_handle_t  *hspi2   = mx_spi2_gethandle();
#endif /* !TEST_ADC_QUALITY */

    /* Diagnostic message sent as early as possible: if this never shows up on the
       terminal, the issue is on the UART link itself (wiring, baud rate, ground),
       not in the ADC part of the test below. */
    uart_send_string(huart5, "\r\n>>> MEZZA_EXP_IN boot OK - UART5 alive <<<\r\n");

    /* Activate then calibrate both ADC instances (done once at startup) */
    if (HAL_ADC_Start(hadc1) != HAL_OK)
    {
      adc_test_error(huart5, "ADC1 Start failed");
    }
    if (HAL_ADC_Calibrate(hadc1) != HAL_OK)
    {
      adc_test_error(huart5, "ADC1 Calibrate failed");
    }
    if (HAL_ADC_Start(hadc2) != HAL_OK)
    {
      adc_test_error(huart5, "ADC2 Start failed");
    }
    if (HAL_ADC_Calibrate(hadc2) != HAL_OK)
    {
      adc_test_error(huart5, "ADC2 Calibrate failed");
    }

    uart_send_string(huart5, ">>> ADC1/ADC2 activated and calibrated <<<\r\n");

#if TEST_ADC_QUALITY

    /* ADC quality test mode: LED/USART3/SPI2 are intentionally NOT started here,
       to keep the board quiet (no extra GPIO/bus activity) while acquiring
       precision ADC data. UART5 is repurposed to also receive short commands
       from the PC script (any received data is treated as "go ahead"). */
    uart5_cmd_arm_receive(huart5);
    uart_send_string(huart5, ">>> TEST_ADC_QUALITY mode - running TEST_1 <<<\r\n");

    /* Sent once, before the first READY: lets the PC script save a record of
       the exact ADC settings in effect for this run, before any data file. */
    adc_quality_send_config(huart5);

#if (ADC_QUALITY_TEST_ID == 1U)
    adc_quality_run_test_1(huart5, hadc1, hadc2);
#else
#error "Unknown ADC_QUALITY_TEST_ID"
#endif

    uart_send_string(huart5, "\r\n>>> ADC quality test COMPLETE <<<\r\n");
    while (1)
    {
    }

#else /* !TEST_ADC_QUALITY : normal demo firmware */

    uart_send_string(huart5, ">>> starting measurement loop <<<\r\n");

    /* Charlieplexed LED test: all 4 drive pins start in Hi-Z (LEDs off). */
    led_charlie_init();
    uart_send_string(huart5, ">>> LED charlieplexing test ready (PB7/PC13/PA15/PC14) <<<\r\n");

    /* USART3 link to the other MCU: arm the (interrupt-driven) receiver once;
       from here on reception is entirely handled by HAL_UART_RxCpltCallback(). */
    usart3_arm_receive(husart3);
    uart_send_string(huart5, ">>> USART3 link ready (PB3 TX / PB4 RX) - sending PING every second <<<\r\n");

    uart_send_string(huart5, ">>> SPI2 link ready (Master, PB12 CS / PB13 SCK / PB14 MISO / PB15 MOSI) "
                             "- exchanging 1 byte every second <<<\r\n");

    uint32_t led_step = 0U;

    while (1)
    {
      /* Convert the 8 channels of ADC1 (PA0..PA7) and the 2 channels of ADC2 (PB0, PB1).
         On failure, adc_read_group() has already reported the exact rank/step/status
         over UART before returning. */
      if (adc_read_group(huart5, "ADC1", hadc1, ADC1_NB_CHANNELS, &adc_results[0]) == 0U)
      {
        adc_test_error(huart5, "ADC1 conversion failed");
      }
      if (adc_read_group(huart5, "ADC2", hadc2, ADC2_NB_CHANNELS, &adc_results[ADC1_NB_CHANNELS]) == 0U)
      {
        adc_test_error(huart5, "ADC2 conversion failed");
      }

      adc_send_results_uart(huart5);

      /* Fire-and-forget: non-blocking send, the other MCU is expected to reply
         at its own pace and its reply (if any) will show up on the LED line. */
      usart3_send_ping(husart3);

      /* Light one LED at a time, in board order DL2..DL11, one per loop iteration. */
      led_charlie_apply_step(led_step);

      char usart3_msg[USART3_RX_BUF_SIZE];
      const char *usart3_text = USART3_NO_MESSAGE_TEXT;
      if (usart3_take_message(usart3_msg, sizeof(usart3_msg)) != 0U)
      {
        usart3_text = usart3_msg;
      }
      led_charlie_report_step(huart5, led_step, usart3_text);
      led_step = (led_step + 1U) % LED_COUNT;

      /* Report the SPI2 transfer that completed during the previous iteration,
         then kick off a new one (non-blocking) for this iteration. */
      spi2_report(huart5);
      spi2_start_xfer(hspi2);

      HAL_Delay(TEST_PERIOD_MS);
    }

#endif /* TEST_ADC_QUALITY */
  }
} /* end main */

/**
  * brief:  Send a NUL-terminated string over UART, blocking.
  * retval: none
  */
static void uart_send_string(hal_uart_handle_t *huart, const char *p_str)
{
  (void)HAL_UART_Transmit(huart, p_str, (uint32_t)strlen(p_str), UART_TX_TIMEOUT_MS);
}

/**
  * brief:  Fatal test error handler: reports the failure reason over UART then halts.
  * retval: none
  */
static void adc_test_error(hal_uart_handle_t *huart, const char *p_reason)
{
  char line[64];
  int  len = snprintf(line, sizeof(line), "\r\n!!! FATAL: %s !!!\r\n", p_reason);
  (void)HAL_UART_Transmit(huart, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  while (1)
  {
  }
}

/**
  * brief:  Decode a hal_status_t value into a short human readable string.
  * retval: pointer to a static, NUL-terminated string
  */
static const char *hal_status_to_str(hal_status_t status)
{
  switch (status)
  {
    case HAL_OK:            return "OK";
    case HAL_ERROR:         return "ERROR";
    case HAL_BUSY:          return "BUSY";
    case HAL_TIMEOUT:       return "TIMEOUT";
    case HAL_INVALID_PARAM: return "INVALID_PARAM";
    default:                return "UNKNOWN";
  }
}

/**
  * brief:  Trigger a full regular sequence conversion on the given ADC and read back
  *         every channel's rank value. On failure, sends a diagnostic line over UART
  *         identifying the ADC, the rank, the failing step and the HAL status code.
  * retval: 1 if the whole sequence has been converted successfully, 0 otherwise
  */
static uint32_t adc_read_group(hal_uart_handle_t *huart, const char *p_adc_name, hal_adc_handle_t *hadc,
                                uint8_t nb_channels, adc_channel_result_t *p_results)
{
  uint8_t idx;

  for (idx = 0U; idx < nb_channels; idx++)
  {
    /* Discontinuous mode (1 rank per subgroup): the first rank is triggered by
       StartConv(), every following rank needs an explicit TrigNextConv() call. */
    hal_status_t trig_status = (idx == 0U) ? HAL_ADC_REG_StartConv(hadc) : HAL_ADC_REG_TrigNextConv(hadc);

    if (trig_status != HAL_OK)
    {
      char line[80];
      int  len = snprintf(line, sizeof(line), "%s rank %u: trigger failed (%s)\r\n",
                           p_adc_name, (unsigned int)(idx + 1U), hal_status_to_str(trig_status));
      (void)HAL_UART_Transmit(huart, line, (uint32_t)len, UART_TX_TIMEOUT_MS);
      return 0U;
    }

    hal_status_t poll_status = HAL_ADC_REG_PollForConv(hadc, ADC_CONV_TIMEOUT_MS);

    if (poll_status != HAL_OK)
    {
      char line[80];
      int  len = snprintf(line, sizeof(line), "%s rank %u: poll failed (%s)\r\n",
                           p_adc_name, (unsigned int)(idx + 1U), hal_status_to_str(poll_status));
      (void)HAL_UART_Transmit(huart, line, (uint32_t)len, UART_TX_TIMEOUT_MS);
      return 0U;
    }

    p_results[idx].raw = HAL_ADC_REG_ReadConversionData(hadc);
    p_results[idx].adc_mv = HAL_ADC_CALC_DATA_TO_VOLTAGE(ADC_VREF_MV, p_results[idx].raw, HAL_ADC_RESOLUTION_12_BIT);
    p_results[idx].input_mv = (int32_t)(((int64_t)p_results[idx].adc_mv * ATTEN_FACTOR_NUM) / ATTEN_FACTOR_DEN);
  }

  return 1U;
}

/* The following helpers only serve the normal demo loop (LED/USART3-ping/pretty-
   printed ADC dump); they are compiled out in the ADC quality test build so that
   build stays warning-free about unused static functions. */
#if !TEST_ADC_QUALITY

/**
  * brief:  Format and send the last conversion results as plain text over UART5.
  * retval: none
  */
static void adc_send_results_uart(hal_uart_handle_t *huart)
{
  char line[96];
  int  len;
  uint32_t idx;

  len = snprintf(line, sizeof(line), "\r\n--- ADC test (10 channels) ---\r\n");
  (void)HAL_UART_Transmit(huart, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  for (idx = 0U; idx < ADC_TOTAL_CHANNELS; idx++)
  {
    int32_t input_mv  = adc_results[idx].input_mv;
    int32_t input_int = input_mv / 1000;
    int32_t input_dec = (input_mv < 0 ? -input_mv : input_mv) % 1000;

    len = snprintf(line, sizeof(line), "%-16s raw=%4ld  Vadc=%4ld mV  Vin=%ld.%03ld V\r\n",
                   adc_results[idx].label,
                   (long)adc_results[idx].raw,
                   (long)adc_results[idx].adc_mv,
                   (long)input_int,
                   (long)input_dec);
    (void)HAL_UART_Transmit(huart, line, (uint32_t)len, UART_TX_TIMEOUT_MS);
  }
}

/**
  * brief:  Enable the GPIO clocks for the 4 charlieplexing drive pins and set them
  *         all to floating input (Hi-Z), i.e. all LEDs off.
  * retval: none
  */
static void led_charlie_init(void)
{
  HAL_RCC_GPIOA_EnableClock();
  HAL_RCC_GPIOB_EnableClock();
  HAL_RCC_GPIOC_EnableClock();

  hal_gpio_config_t hiz_config;
  hiz_config.mode = HAL_GPIO_MODE_INPUT;
  hiz_config.pull = HAL_GPIO_PULL_NO;

  for (uint32_t i = 0U; i < LED_ROW_COUNT; i++)
  {
    HAL_GPIO_Init(led_row_pins[i].port, led_row_pins[i].pin, &hiz_config);
  }
}

/**
  * brief:  Light a single LED: its anode row is driven push-pull HIGH, its cathode
  *         row push-pull LOW, and the other two rows are set to floating input
  *         (Hi-Z) so they cannot sink/source current for any other LED.
  * retval: none
  */
static void led_charlie_apply_step(uint32_t step)
{
  uint32_t high_idx = leds[step].anode_row;
  uint32_t low_idx  = leds[step].cathode_row;

  hal_gpio_config_t hiz_config;
  hiz_config.mode = HAL_GPIO_MODE_INPUT;
  hiz_config.pull = HAL_GPIO_PULL_NO;

  hal_gpio_config_t out_config;
  out_config.mode        = HAL_GPIO_MODE_OUTPUT;
  out_config.pull        = HAL_GPIO_PULL_NO;
  out_config.speed       = HAL_GPIO_SPEED_FREQ_LOW;
  out_config.output_type = HAL_GPIO_OUTPUT_PUSHPULL;

  for (uint32_t i = 0U; i < LED_ROW_COUNT; i++)
  {
    if (i == high_idx)
    {
      out_config.init_state = HAL_GPIO_PIN_SET;
      HAL_GPIO_Init(led_row_pins[i].port, led_row_pins[i].pin, &out_config);
    }
    else if (i == low_idx)
    {
      out_config.init_state = HAL_GPIO_PIN_RESET;
      HAL_GPIO_Init(led_row_pins[i].port, led_row_pins[i].pin, &out_config);
    }
    else
    {
      HAL_GPIO_Init(led_row_pins[i].port, led_row_pins[i].pin, &hiz_config);
    }
  }
}

/**
  * brief:  Report over UART which LED is currently lit for the given step, with the
  *         latest USART3 status (received message, or "No-Message") appended at the
  *         end of the same line.
  * retval: none
  */
static void led_charlie_report_step(hal_uart_handle_t *huart, uint32_t step, const char *p_usart3_msg)
{
  uint32_t high_idx = leds[step].anode_row;
  uint32_t low_idx  = leds[step].cathode_row;
  char     line[160];

  int len = snprintf(line, sizeof(line),
                      "LED test %2u/%u: %-5s ON  (%s=HIGH  %s=LOW  others Hi-Z)  USART3: %s\r\n",
                      (unsigned int)(step + 1U), (unsigned int)LED_COUNT, leds[step].label,
                      led_row_pins[high_idx].label, led_row_pins[low_idx].label, p_usart3_msg);
  (void)HAL_UART_Transmit(huart, line, (uint32_t)len, UART_TX_TIMEOUT_MS);
}

/**
  * brief:  Arm (or re-arm) an interrupt-driven, variable-length reception on USART3:
  *         HAL_UART_RxCpltCallback() fires as soon as either usart3_rx_buf is full or
  *         the line goes idle after some bytes were received - whichever comes first -
  *         with no busy-waiting and no software timeout involved.
  * retval: none
  */
static void usart3_arm_receive(hal_uart_handle_t *husart3)
{
  (void)HAL_UART_ReceiveToIdle_IT(husart3, usart3_rx_buf, USART3_RX_BUF_SIZE);
}

/**
  * brief:  Send one "PING <n>" message on USART3 without blocking (interrupt-driven
  *         transmission): the call returns immediately, actual bytes go out under
  *         USART3 IRQ. usart3_tx_buf is static so it stays valid for the whole
  *         transmission, well past this function's return.
  * retval: none
  */
static void usart3_send_ping(hal_uart_handle_t *husart3)
{
  static uint32_t ping_counter = 0U;

  int len = snprintf(usart3_tx_buf, sizeof(usart3_tx_buf), "PING %lu\r\n", (unsigned long)ping_counter);
  ping_counter++;

  (void)HAL_UART_Transmit_IT(husart3, usart3_tx_buf, (uint32_t)len);
}

/**
  * brief:  Consume the last message received on USART3, if any, since the previous
  *         call. The USART3 IRQ is briefly masked to make the read atomic with
  *         respect to HAL_UART_RxCpltCallback().
  * retval: 1 and *out_buf filled if a message was pending, 0 otherwise (out_buf
  *         left untouched)
  */
static uint32_t usart3_take_message(char *out_buf, uint32_t out_buf_size)
{
  uint32_t got = 0U;

  HAL_CORTEX_NVIC_DisableIRQ(USART3_IRQn);
  if (usart3_message_ready != 0U)
  {
    size_t msg_len = strlen(usart3_last_message);
    if (msg_len >= out_buf_size)
    {
      msg_len = out_buf_size - 1U;
    }
    memcpy(out_buf, usart3_last_message, msg_len);
    out_buf[msg_len] = '\0';
    usart3_message_ready = 0U;
    got = 1U;
  }
  HAL_CORTEX_NVIC_EnableIRQ(USART3_IRQn);

  return got;
}

#endif /* !TEST_ADC_QUALITY */

/**
  * brief:  HAL_UART callback: fires from inside HAL_UART_IRQHandler() when a
  *         reception armed by usart3_arm_receive() or uart5_cmd_arm_receive()
  *         completes, either because the RX buffer filled up or because the line
  *         went idle (rx_event == HAL_UART_RX_EVENT_IDLE) after a shorter message.
  *         This single callback is shared by every UART instance (that is how this
  *         HAL is designed), so it dispatches on which handle fired.
  *         NOTE: the HAL only moves huart->rx_state back to IDLE *after* this
  *         callback returns, so calling HAL_UART_ReceiveToIdle_IT() from here would
  *         always fail with HAL_BUSY. Just flag that a re-arm is needed; the actual
  *         re-arm happens in USART3_IRQHandler()/UART5_IRQHandler(), once
  *         HAL_UART_IRQHandler() (and therefore this callback) has returned and
  *         rx_state is IDLE again.
  * retval: none
  */
void HAL_UART_RxCpltCallback(hal_uart_handle_t *huart, uint32_t size_byte, hal_uart_rx_event_types_t rx_event)
{
  (void)rx_event;

  if (huart == mx_usart3_uart_gethandle())
  {
    uint32_t copy_len = size_byte;
    if (copy_len >= sizeof(usart3_last_message))
    {
      copy_len = (uint32_t)sizeof(usart3_last_message) - 1U;
    }
    memcpy(usart3_last_message, usart3_rx_buf, copy_len);

    /* Trim a trailing CR/LF, if any, so it does not break the single-line report. */
    while ((copy_len > 0U) && ((usart3_last_message[copy_len - 1U] == '\r') ||
                               (usart3_last_message[copy_len - 1U] == '\n')))
    {
      copy_len--;
    }
    usart3_last_message[copy_len] = '\0';

    usart3_message_ready = 1U;
    usart3_rx_need_rearm = 1U;
  }
  else if (huart == mx_uart5_uart_gethandle())
  {
    /* ADC quality test command channel: content is irrelevant, only the fact
       that something was received matters (see uart5_cmd_wait_for_command()). */
    uart5_cmd_ready = 1U;
    uart5_rx_need_rearm = 1U;
  }
  else
  {
    /* Unknown handle: nothing to do. */
  }
}

/**
  * brief:  HAL_UART callback: fires from inside HAL_UART_IRQHandler() on a line error
  *         (framing, noise, overrun, ...) on any UART instance. Only flags that
  *         reception needs to be re-armed (see HAL_UART_RxCpltCallback() note
  *         above); the actual re-arm happens in the matching IRQHandler() once
  *         rx_state is back to IDLE.
  * retval: none
  */
void HAL_UART_ErrorCallback(hal_uart_handle_t *huart)
{
  if (huart == mx_usart3_uart_gethandle())
  {
    usart3_rx_need_rearm = 1U;
  }
  else if (huart == mx_uart5_uart_gethandle())
  {
    uart5_rx_need_rearm = 1U;
  }
  else
  {
    /* Unknown handle: nothing to do. */
  }
}

/**
  * brief:  USART3 global interrupt handler: services the peripheral, then re-arms
  *         reception if HAL_UART_RxCpltCallback()/HAL_UART_ErrorCallback() flagged
  *         it as needed (see their comments for why this can't be done in-line).
  * retval: none
  */
void USART3_IRQHandler(void)
{
  hal_uart_handle_t *husart3 = mx_usart3_uart_gethandle();

  HAL_UART_IRQHandler(husart3);

  if (usart3_rx_need_rearm != 0U)
  {
    usart3_rx_need_rearm = 0U;
    (void)HAL_UART_ReceiveToIdle_IT(husart3, usart3_rx_buf, USART3_RX_BUF_SIZE);
  }
}

/**
  * brief:  UART5 global interrupt handler: services the peripheral (both the debug
  *         TX and, when TEST_ADC_QUALITY is active, the command RX), then re-arms
  *         reception if flagged as needed - same reasoning as USART3_IRQHandler().
  * retval: none
  */
void UART5_IRQHandler(void)
{
  hal_uart_handle_t *huart5 = mx_uart5_uart_gethandle();

  HAL_UART_IRQHandler(huart5);

  if (uart5_rx_need_rearm != 0U)
  {
    uart5_rx_need_rearm = 0U;
    (void)HAL_UART_ReceiveToIdle_IT(huart5, uart5_cmd_rx_buf, UART5_CMD_RX_BUF_SIZE);
  }
}

/* SPI2 test helpers: normal demo loop only, see the note above adc_send_results_uart(). */
#if !TEST_ADC_QUALITY

/**
  * brief:  Kick off one non-blocking full-duplex SPI2 byte exchange: sends the next
  *         counter value and, once complete, HAL_SPI_TxRxCpltCallback() will update
  *         spi2_rx_byte with whatever the slave shifted out during this transfer.
  * retval: none
  */
static void spi2_start_xfer(hal_spi_handle_t *hspi2)
{
  static uint8_t counter = 0U;

  spi2_tx_byte = counter;
  counter++;

  spi2_xfer_complete = 0U;
  (void)HAL_SPI_TransmitReceive_IT(hspi2, &spi2_tx_byte, &spi2_rx_byte, 1U);
}

/**
  * brief:  Report over UART the outcome of the SPI2 transfer started on the
  *         previous iteration (or "no data yet" before the first one completes).
  * retval: none
  */
static void spi2_report(hal_uart_handle_t *huart)
{
  char line[80];
  int  len;

  if (spi2_xfer_complete != 0U)
  {
    len = snprintf(line, sizeof(line), "SPI2 test: sent 0x%02X, received 0x%02X\r\n",
                   spi2_tx_byte, spi2_rx_byte);
  }
  else
  {
    len = snprintf(line, sizeof(line), "SPI2 test: sent 0x%02X, received: no data yet\r\n", spi2_tx_byte);
  }
  (void)HAL_UART_Transmit(huart, line, (uint32_t)len, UART_TX_TIMEOUT_MS);
}

#endif /* !TEST_ADC_QUALITY */

/**
  * brief:  HAL_SPI callback: fires from inside HAL_SPI_IRQHandler() when the transfer
  *         started by spi2_start_xfer() completes. Only updates spi2_xfer_complete;
  *         spi2_rx_byte itself has already been written directly by the HAL.
  * retval: none
  */
void HAL_SPI_TxRxCpltCallback(hal_spi_handle_t *hspi)
{
  (void)hspi;
  spi2_xfer_complete = 1U;
}

/**
  * brief:  HAL_SPI callback: fires from inside HAL_SPI_IRQHandler() on a transfer
  *         error (overrun, mode fault, CRC, ...). The next iteration's
  *         spi2_start_xfer() will simply start a fresh transfer.
  * retval: none
  */
void HAL_SPI_ErrorCallback(hal_spi_handle_t *hspi)
{
  (void)hspi;
}

/**
  * brief:  SPI2 global interrupt handler.
  * retval: none
  */
void SPI2_IRQHandler(void)
{
  HAL_SPI_IRQHandler(mx_spi2_gethandle());
}

#if TEST_ADC_QUALITY

/**
  * brief:  Arm (or re-arm) an interrupt-driven reception of a single command line
  *         from the PC on UART5. Content is ignored; see uart5_cmd_wait_for_command().
  * retval: none
  */
static void uart5_cmd_arm_receive(hal_uart_handle_t *huart5)
{
  (void)HAL_UART_ReceiveToIdle_IT(huart5, uart5_cmd_rx_buf, UART5_CMD_RX_BUF_SIZE);
}

/**
  * brief:  Block until the PC sends anything on UART5 (the "go ahead" for the next
  *         acquisition step), then consume it. Reception itself stays fully
  *         interrupt-driven (see HAL_UART_RxCpltCallback() / UART5_IRQHandler()); this
  *         function only busy-waits on the resulting flag, which is fine here since
  *         the ADC quality test has nothing else to do while waiting for the operator.
  * retval: none
  */
static void uart5_cmd_wait_for_command(void)
{
  while (uart5_cmd_ready == 0U)
  {
  }
  uart5_cmd_ready = 0U;
}

/**
  * brief:  Send a CONFIG_BEGIN/CONFIG_END block of "key=value" lines describing
  *         the exact ADC settings in effect for this run (resolution, VREF,
  *         clock, sampling time, attenuation factor, channel/voltage lists,
  *         ...), so the PC script can save them alongside the acquired data
  *         for later quality evaluation. Sent once, before the first READY.
  * retval: none
  */
static void adc_quality_send_config(hal_uart_handle_t *huart5)
{
  char     line[160];
  int      len;
  uint32_t i;

  uart_send_string(huart5, "CONFIG_BEGIN\r\n");

  len = snprintf(line, sizeof(line), "firmware_build=%s %s\r\n", __DATE__, __TIME__);
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  len = snprintf(line, sizeof(line), "test_id=%u\r\n", (unsigned int)ADC_QUALITY_TEST_ID);
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  len = snprintf(line, sizeof(line), "adc_vref_mV=%u\r\n", (unsigned int)ADC_VREF_MV);
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  len = snprintf(line, sizeof(line), "adc_resolution_bits=%u\r\n", (unsigned int)ADC_RESOLUTION_BITS);
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  len = snprintf(line, sizeof(line), "adc_kernel_clock_Hz=%lu\r\n", (unsigned long)ADC_KERNEL_CLOCK_HZ);
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  len = snprintf(line, sizeof(line), "adc_sampling_time_cycles=%u\r\n", (unsigned int)ADC_SAMPLING_TIME_CYCLES);
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  /* Sent as a x10 integer (no float printf support with nano.specs); the PC
     side divides by 10 to get the usual "12.5 cycles" figure. */
  len = snprintf(line, sizeof(line), "adc_conv_cycles_x10=%u\r\n", (unsigned int)ADC_CONV_CYCLES_X10);
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  /* atten_factor = atten_factor_num / atten_factor_den (e.g. 10000/2875 = 0.2875) */
  len = snprintf(line, sizeof(line), "atten_factor_num=%lu\r\n", (unsigned long)ATTEN_FACTOR_NUM);
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  len = snprintf(line, sizeof(line), "atten_factor_den=%lu\r\n", (unsigned long)ATTEN_FACTOR_DEN);
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  len = snprintf(line, sizeof(line), "samples_per_point=%lu\r\n", (unsigned long)ADC_QUALITY_SAMPLES_PER_POINT);
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  len = snprintf(line, sizeof(line), "uart_baud=115200\r\n");
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  /* voltages_v=0,2,4,6,8,10 */
  len = snprintf(line, sizeof(line), "voltages_v=");
  for (i = 0U; i < ADC_QUALITY_VOLTAGE_COUNT; i++)
  {
    len += snprintf(&line[len], sizeof(line) - (size_t)len, "%s%lu",
                     (i == 0U) ? "" : ",", (unsigned long)adc_quality_voltages_v[i]);
  }
  len += snprintf(&line[len], sizeof(line) - (size_t)len, "\r\n");
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  /* channels=ADC1:PA0,ADC1:PA1,...,ADC2:PB1 */
  len = snprintf(line, sizeof(line), "channels=");
  for (i = 0U; i < ADC_QUALITY_CHANNEL_COUNT; i++)
  {
    len += snprintf(&line[len], sizeof(line) - (size_t)len, "%sADC%u:%s",
                     (i == 0U) ? "" : ",",
                     (unsigned int)adc_quality_channels[i].adc_number,
                     adc_quality_channels[i].pin_label);
  }
  len += snprintf(&line[len], sizeof(line) - (size_t)len, "\r\n");
  (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

  uart_send_string(huart5, "CONFIG_END\r\n");
}

/**
  * brief:  TEST_1: for each channel in adc_quality_channels[] and each nominal
  *         voltage in adc_quality_voltages_v[], wait for the operator to set up the
  *         reference on that channel and confirm from the PC script (any data
  *         received on UART5), then acquire ADC_QUALITY_SAMPLES_PER_POINT samples of
  *         that single channel and stream them out as CSV lines.
  *         Wire protocol (see also the project README):
  *           READY,ADC<n>,<pin>,<v>V              -- sent, then this firmware blocks
  *           (operator sets up the reference and sends anything from the PC script)
  *           START,ADC<n>,<pin>,<v>V,<count>
  *           <index>,<raw>,<adc_mV>,<vin_mV>       -- repeated <count> times
  *           END,ADC<n>,<pin>,<v>V
  *         ... repeated for every (channel, voltage) combination, then:
  *           ALL_DONE
  * retval: none
  */
static void adc_quality_run_test_1(hal_uart_handle_t *huart5, hal_adc_handle_t *hadc1, hal_adc_handle_t *hadc2)
{
  char     line[64];
  int      len;
  uint32_t ch;
  uint32_t v;

  for (ch = 0U; ch < ADC_QUALITY_CHANNEL_COUNT; ch++)
  {
    const adc_quality_channel_t *p_ch = &adc_quality_channels[ch];
    hal_adc_handle_t *hadc = (p_ch->adc_number == 1U) ? hadc1 : hadc2;
    uint8_t nb_channels_in_seq = (p_ch->adc_number == 1U) ? ADC1_NB_CHANNELS : ADC2_NB_CHANNELS;
    const char *adc_label = (p_ch->adc_number == 1U) ? "ADC1" : "ADC2";

    for (v = 0U; v < ADC_QUALITY_VOLTAGE_COUNT; v++)
    {
      uint32_t voltage_v = adc_quality_voltages_v[v];
      uint32_t sample;

      len = snprintf(line, sizeof(line), "READY,ADC%u,%s,%luV\r\n",
                      (unsigned int)p_ch->adc_number, p_ch->pin_label, (unsigned long)voltage_v);
      (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

      /* Operator sets up the reference voltage on this channel, then confirms
         from the PC script; content is irrelevant, only its arrival matters. */
      uart5_cmd_wait_for_command();

      len = snprintf(line, sizeof(line), "START,ADC%u,%s,%luV,%lu\r\n",
                      (unsigned int)p_ch->adc_number, p_ch->pin_label, (unsigned long)voltage_v,
                      (unsigned long)ADC_QUALITY_SAMPLES_PER_POINT);
      (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);

      for (sample = 0U; sample < ADC_QUALITY_SAMPLES_PER_POINT; sample++)
      {
        /* Re-uses the same discontinuous-mode trigger/poll sequence already
           validated for the normal demo loop; only the target channel's rank
           result (channel_idx) is actually reported. */
        if (adc_read_group(huart5, adc_label, hadc, nb_channels_in_seq, adc_results) == 0U)
        {
          adc_test_error(huart5, "ADC quality test: conversion failed");
        }

        len = snprintf(line, sizeof(line), "%lu,%ld,%ld,%ld\r\n",
                        (unsigned long)sample,
                        (long)adc_results[p_ch->channel_idx].raw,
                        (long)adc_results[p_ch->channel_idx].adc_mv,
                        (long)adc_results[p_ch->channel_idx].input_mv);
        (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);
      }

      len = snprintf(line, sizeof(line), "END,ADC%u,%s,%luV\r\n",
                      (unsigned int)p_ch->adc_number, p_ch->pin_label, (unsigned long)voltage_v);
      (void)HAL_UART_Transmit(huart5, line, (uint32_t)len, UART_TX_TIMEOUT_MS);
    }
  }

  (void)HAL_UART_Transmit(huart5, "ALL_DONE\r\n", 10U, UART_TX_TIMEOUT_MS);
}

#endif /* TEST_ADC_QUALITY */
