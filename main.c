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

#define ADC_CONV_TIMEOUT_MS      100U
#define UART_TX_TIMEOUT_MS       100U
#define TEST_PERIOD_MS           1000U

#define LED_ROW_COUNT            4U
#define LED_COUNT                10U  /* DL2..DL11 */

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

/* Private functions prototype -----------------------------------------------*/
static void uart_send_string(hal_uart_handle_t *huart, const char *p_str);
static void adc_test_error(hal_uart_handle_t *huart, const char *p_reason);
static const char *hal_status_to_str(hal_status_t status);
static uint32_t adc_read_group(hal_uart_handle_t *huart, const char *p_adc_name, hal_adc_handle_t *hadc,
                                uint8_t nb_channels, adc_channel_result_t *p_results);
static void adc_send_results_uart(hal_uart_handle_t *huart);

static void led_charlie_init(void);
static void led_charlie_apply_step(uint32_t step);
static void led_charlie_report_step(hal_uart_handle_t *huart, uint32_t step);

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
    hal_adc_handle_t  *hadc1  = mx_adc1_gethandle();
    hal_adc_handle_t  *hadc2  = mx_adc2_gethandle();
    hal_uart_handle_t *huart5 = mx_uart5_uart_gethandle();

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

    uart_send_string(huart5, ">>> ADC1/ADC2 activated and calibrated - starting measurement loop <<<\r\n");

    /* Charlieplexed LED test: all 4 drive pins start in Hi-Z (LEDs off). */
    led_charlie_init();
    uart_send_string(huart5, ">>> LED charlieplexing test ready (PB7/PC13/PA15/PC14) <<<\r\n");

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

      /* Light one LED at a time, in board order DL2..DL11, one per loop iteration. */
      led_charlie_apply_step(led_step);
      led_charlie_report_step(huart5, led_step);
      led_step = (led_step + 1U) % LED_COUNT;

      HAL_Delay(TEST_PERIOD_MS);
    }
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
  * brief:  Report over UART which LED is currently lit for the given step.
  * retval: none
  */
static void led_charlie_report_step(hal_uart_handle_t *huart, uint32_t step)
{
  uint32_t high_idx = leds[step].anode_row;
  uint32_t low_idx  = leds[step].cathode_row;
  char     line[112];

  int len = snprintf(line, sizeof(line), "LED test %2u/%u: %-5s ON  (%s=HIGH  %s=LOW  others Hi-Z)\r\n",
                      (unsigned int)(step + 1U), (unsigned int)LED_COUNT, leds[step].label,
                      led_row_pins[high_idx].label, led_row_pins[low_idx].label);
  (void)HAL_UART_Transmit(huart, line, (uint32_t)len, UART_TX_TIMEOUT_MS);
}
