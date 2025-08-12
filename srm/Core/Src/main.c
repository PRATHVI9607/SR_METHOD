/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
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
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "usbd_cdc_if.h"
#include "NanoEdgeAI.h"
#include "knowledge.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct { int16_t x, y, z; } vector3_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ADXL345_ADDR            (0x53 << 1)
#define ADXL345_REG_POWER_CTL   0x2D
#define ADXL345_REG_DATAX0      0x32
#define ACC_BUFFER_SIZE         300

#define PUMP_GPIO_PORT          GPIOA
#define PUMP_PIN                GPIO_PIN_5
#define WATER_SENSOR_CHANNEL    ADC_CHANNEL_6  /* PA6 */
#define WATER_LOW_THRESHOLD_PERCENT 20

#define DS18B20_PORT            GPIOA
#define DS18B20_PIN             GPIO_PIN_4     /* PA4 (DQ) */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
float acc_buffer[ACC_BUFFER_SIZE];
uint16_t water_level_full = 1; /* initialized >0 to avoid div-by-zero */
uint8_t neai_similarity = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ---------- USB print helper (CDC) ---------- */
static void usb_printf(const char *fmt, ...)
{
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  /* wait if busy */
  while (CDC_Transmit_FS((uint8_t *)buf, (uint16_t)strlen(buf)) == USBD_BUSY)
  {
    HAL_Delay(1);
  }
  HAL_Delay(2); /* small pause to allow host to receive (safe) */
}

/* ---------- simple short delay function (rough micro delays without timers) ----------
   This uses a small empty loop tuned to be small. It's not cycle-accurate across
   all optimization levels and compilers, but avoids hardware timers as requested.
   If you need accurate timing, request a DWT/us-timer version. */
static void short_delay_cycles(volatile uint32_t cycles)
{
  while (cycles--) { __NOP(); }
}

/* ---------- DS18B20 1-Wire (bit-banged) on PA4 ----------
   Note: uses open-drain like behavior by switching pin mode between output (drive low)
   and input (release line so pull-up can pull high). You MUST have a 4.7k pull-up on DQ.
*/
static void ds18b20_pin_output(void)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = DS18B20_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP; /* we'll drive low when needed, set high by releasing */
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DS18B20_PORT, &gpio);
}

static void ds18b20_pin_input(void)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = DS18B20_PIN;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DS18B20_PORT, &gpio);
}

/* Reset pulse: master pulls low for ~480us, then releases and reads presence */
static uint8_t ds18b20_reset(void)
{
  uint8_t presence = 0;

  ds18b20_pin_output();
  HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);
  /* ~480us low: approximate with HAL_Delay(1) = 1 ms is longer but safe; use short loops for ~600 cycles */
  /* We use 8000 cycles of NOP which is a rough ~?us depending on clock - not exact.
     To be safe and portable, use HAL_Delay(1) here (1 ms) which is longer than spec but safe. */
  HAL_Delay(1);

  ds18b20_pin_input(); /* release the bus */
  /* Wait 60-70us for presence window; approximate with small cycles */
  short_delay_cycles(4000); /* small busy loop */
  if (HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN) == GPIO_PIN_RESET) presence = 1;
  /* Wait for end of presence (recovery) */
  short_delay_cycles(4000);
  return presence;
}

/* Write single bit */
static void ds18b20_write_bit(uint8_t bit)
{
  ds18b20_pin_output();
  HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);

  if (bit) {
    /* write 1: release quickly (<15us) */
    short_delay_cycles(1500);
    ds18b20_pin_input(); /* release bus to let pull-up raise line */
    /* slot rest */
    short_delay_cycles(7000);
  } else {
    /* write 0: hold low for ~60us */
    short_delay_cycles(10000);
    ds18b20_pin_input();
    short_delay_cycles(2000);
  }
}

/* Read single bit */
static uint8_t ds18b20_read_bit(void)
{
  uint8_t bit = 0;
  ds18b20_pin_output();
  HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);
  /* short low */
  short_delay_cycles(1200);
  ds18b20_pin_input(); /* master releases and reads */
  short_delay_cycles(2500);
  if (HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN) == GPIO_PIN_SET) bit = 1;
  /* finish slot */
  short_delay_cycles(7000);
  return bit;
}

/* Write byte (LSB first) */
static void ds18b20_write_byte(uint8_t val)
{
  for (uint8_t i = 0; i < 8; ++i) {
    ds18b20_write_bit(val & 0x01);
    val >>= 1;
  }
}

/* Read byte (LSB first) */
static uint8_t ds18b20_read_byte(void)
{
  uint8_t val = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    val |= (ds18b20_read_bit() << i);
  }
  return val;
}

/* Get temperature from DS18B20: returns Celsius (float) or very negative on error */
static float ds18b20_get_temperature(void)
{
  uint8_t lsb, msb;
  int16_t raw;

  if (!ds18b20_reset()) {
    /* no presence — return clearly invalid temperature */
    return -1000.0f;
  }

  ds18b20_write_byte(0xCC); /* SKIP ROM (only one device) */
  ds18b20_write_byte(0x44); /* CONVERT T */

  /* Conversion time: up to 750 ms for 12-bit. We'll wait 750ms (safe) */
  HAL_Delay(750);

  if (!ds18b20_reset()) return -1000.0f;
  ds18b20_write_byte(0xCC); /* SKIP ROM */
  ds18b20_write_byte(0xBE); /* READ SCRATCHPAD */

  lsb = ds18b20_read_byte();
  msb = ds18b20_read_byte();

  raw = (int16_t)((msb << 8) | lsb);

  /* Temperature resolution is 1/16 C (12-bit default) */
  return ((float)raw) / 16.0f;
}

/* ---------- ADXL345 functions (I2C1) ---------- */
static HAL_StatusTypeDef read_adxl345(vector3_t *acc)
{
  uint8_t buf[6];
  if (HAL_I2C_Mem_Read(&hi2c1, ADXL345_ADDR, ADXL345_REG_DATAX0, 1, buf, 6, 100) != HAL_OK)
    return HAL_ERROR;

  acc->x = (int16_t)((buf[1] << 8) | buf[0]);
  acc->y = (int16_t)((buf[3] << 8) | buf[2]);
  acc->z = (int16_t)((buf[5] << 8) | buf[4]);
  return HAL_OK;
}

static void fill_acc_buffer(void)
{
  vector3_t acc;
  for (int i = 0; i < (ACC_BUFFER_SIZE / 3); ++i) {
    while (read_adxl345(&acc) != HAL_OK) { HAL_Delay(1); }
    acc_buffer[i*3 + 0] = acc.x * 0.0039f; /* assuming +/-2g -> 3.9mg/LSB */
    acc_buffer[i*3 + 1] = acc.y * 0.0039f;
    acc_buffer[i*3 + 2] = acc.z * 0.0039f;
    HAL_Delay(1);
  }
}

/* ---------- ADC water level ---------- */
static uint16_t read_water_level_adc(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Channel = WATER_SENSOR_CHANNEL;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
  uint16_t val = (uint16_t)HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
  return val;
}

static void calibrate_full_water_level(void)
{
  usb_printf("Calibrating full water level (stand level) in 2s...\r\n");
  HAL_Delay(2000);
  water_level_full = read_water_level_adc();
  if (water_level_full == 0) water_level_full = 1; /* safety */
  usb_printf("Calibrated full level ADC = %u\r\n", water_level_full);
}

static void pump_on(void)  { HAL_GPIO_WritePin(PUMP_GPIO_PORT, PUMP_PIN, GPIO_PIN_SET); }
static void pump_off(void) { HAL_GPIO_WritePin(PUMP_GPIO_PORT, PUMP_PIN, GPIO_PIN_RESET); }

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */

  /* Basic USB ready message */
  HAL_Delay(200);
  usb_printf("STM32F407VG USB CDC Ready\r\n");

  /* Power ADXL345: set measure bit in POWER_CTL (0x2D = 0x08) */
  {
    uint8_t cmd = 0x08;
    HAL_I2C_Mem_Write(&hi2c1, ADXL345_ADDR, ADXL345_REG_POWER_CTL, 1, &cmd, 1, 100);
    HAL_Delay(100);
  }

  /* Initialize NanoEdge AI anomaly detection */
  if (neai_anomalydetection_init() == NEAI_OK) {
    usb_printf("NanoEdgeAI initialized\r\n");
  } else {
    usb_printf("NanoEdgeAI init ERROR\r\n");
  }

  /* Learning phase */
  for (uint16_t i = 0; i < 20; ++i) {
    usb_printf("Learning iteration %u/20\r\n", (unsigned)i+1);
    fill_acc_buffer();
    neai_anomalydetection_learn(acc_buffer);
    HAL_Delay(50);
  }
  usb_printf("Learning finished\r\n");

  /* Calibrate water full level */
  calibrate_full_water_level();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* 1) Anomaly detection */
    fill_acc_buffer();
    neai_anomalydetection_detect(acc_buffer, &neai_similarity);
    if (neai_similarity >= 90) {
      usb_printf("NOMINAL,%u\r\n", (unsigned)neai_similarity);
    } else {
      usb_printf("ANOMALY,%u\r\n", (unsigned)neai_similarity);
    }

    /* 2) Water level read and pump control */
    {
      uint16_t wl = read_water_level_adc();
      float percent = (100.0f * (float)wl) / (float)water_level_full;
      if (percent < 0.0f) percent = 0.0f;
      if (percent > 100.0f) percent = 100.0f;
      usb_printf("WaterLevel,%u,%.2f%%\r\n", wl, percent);

      if (percent > (float)WATER_LOW_THRESHOLD_PERCENT) {
        pump_on();
        usb_printf("Pump ON\r\n");
      } else {
        pump_off();
        usb_printf("Pump OFF\r\n");
      }
    }

    /* 3) DS18B20 temperature */
    {
      float t = ds18b20_get_temperature();
      if (t < -500.0f) {
        usb_printf("WaterTemp,ERR\r\n");
      } else {
        usb_printf("WaterTemp,%.2fC\r\n", t);
      }
    }

    HAL_Delay(1000); /* main loop delay */
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/* SystemClock_Config kept as generated by CubeMX; if your project uses a different
   PLL values you previously set, keep those. This file uses the standard CubeMX
   SystemClock_Config (user-provided in your template). */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  /* Keep CubeMX defaults you had — update these if your clock config differs */
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  /* Keep CubeMX APB prescalers you used (this template uses typical F407 values) */
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
    /* trap */
  }
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* user can print file / line over USB here if desired */
}
#endif /* USE_FULL_ASSERT */
