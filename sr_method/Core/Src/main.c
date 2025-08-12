#include "main.h"
#include "i2c.h"
#include "usb_device.h"
#include "gpio.h"
#include "adc.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "NanoEdgeAI.h"
#include "knowledge.h"
#include "usbd_cdc_if.h"

void SystemClock_Config(void);

typedef struct { int16_t x, y, z; } vector3_t;

#define ADXL345_ADDR (0x53 << 1)
#define ADXL345_REG_POWER_CTL 0x2D
#define ADXL345_REG_DATAX0   0x32
#define ACC_BUFFER_SIZE 300

#define PUMP_GPIO_PORT GPIOA
#define PUMP_PIN GPIO_PIN_5
#define WATER_SENSOR_CHANNEL ADC_CHANNEL_6  // PA6
#define WATER_LOW_THRESHOLD_PERCENT 20

// DS18B20 settings
#define DS18B20_PORT GPIOA
#define DS18B20_PIN GPIO_PIN_4

float acc_buffer[ACC_BUFFER_SIZE];
uint16_t water_level_full = 0;

// === USB Print ===
void usb_printf(const char *fmt, ...) {
    char buffer[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    CDC_Transmit_FS((uint8_t *)buffer, strlen(buffer));
    HAL_Delay(5);
}

// === ADXL345 ===
HAL_StatusTypeDef Read_ADXL345(vector3_t *accel) {
    uint8_t buf[6];
    if (HAL_I2C_Mem_Read(&hi2c1, ADXL345_ADDR, ADXL345_REG_DATAX0, 1, buf, 6, 100) != HAL_OK)
        return HAL_ERROR;

    accel->x = (int16_t)((buf[1] << 8) | buf[0]);
    accel->y = (int16_t)((buf[3] << 8) | buf[2]);
    accel->z = (int16_t)((buf[5] << 8) | buf[4]);
    return HAL_OK;
}

void fill_accelerometer_buffer(void) {
    vector3_t acc;
    for (int i = 0; i < ACC_BUFFER_SIZE / 3; i++) {
        while (Read_ADXL345(&acc) != HAL_OK);
        acc_buffer[i * 3 + 0] = acc.x * 0.0039f;
        acc_buffer[i * 3 + 1] = acc.y * 0.0039f;
        acc_buffer[i * 3 + 2] = acc.z * 0.0039f;
        HAL_Delay(1);
    }
}

// === ADC ===
uint16_t read_adc_channel(uint32_t channel) {
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);
    uint16_t value = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return value;
}

uint16_t read_water_level(void) {
    return read_adc_channel(WATER_SENSOR_CHANNEL);
}

// === DS18B20 One-Wire Functions ===
void DS18B20_Pin_Output(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DS18B20_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DS18B20_PORT, &GPIO_InitStruct);
}

void DS18B20_Pin_Input(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DS18B20_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(DS18B20_PORT, &GPIO_InitStruct);
}

uint8_t DS18B20_Reset(void) {
    uint8_t response = 0;
    DS18B20_Pin_Output();
    HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    DS18B20_Pin_Input();
    HAL_Delay(1);
    if (!HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN)) response = 1;
    HAL_Delay(1);
    return response;
}

void DS18B20_Write(uint8_t data) {
    DS18B20_Pin_Output();
    for (int i = 0; i < 8; i++) {
        HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_RESET);
        if (data & (1 << i))
            HAL_GPIO_WritePin(DS18B20_PORT, DS18B20_PIN, GPIO_PIN_SET);
        HAL_Delay(1);
    }
}

uint8_t DS18B20_Read(void) {
    uint8_t value = 0;
    DS18B20_Pin_Input();
    for (int i = 0; i < 8; i++) {
        if (HAL_GPIO_ReadPin(DS18B20_PORT, DS18B20_PIN))
            value |= (1 << i);
        HAL_Delay(1);
    }
    return value;
}

float DS18B20_GetTemp(void) {
    uint8_t temp_l, temp_h;
    int16_t temp;
    DS18B20_Reset();
    DS18B20_Write(0xCC);
    DS18B20_Write(0x44);
    HAL_Delay(750);
    DS18B20_Reset();
    DS18B20_Write(0xCC);
    DS18B20_Write(0xBE);
    temp_l = DS18B20_Read();
    temp_h = DS18B20_Read();
    temp = (temp_h << 8) | temp_l;
    return temp / 16.0;
}

// === Water Pump ===
void calibrate_full_water_level(void) {
    usb_printf("Calibrating full water level...\r\n");
    HAL_Delay(5000);
    water_level_full = read_water_level();
    usb_printf("Water level full calibrated to: %d\r\n", water_level_full);
}

void pump_on(void) { HAL_GPIO_WritePin(PUMP_GPIO_PORT, PUMP_PIN, GPIO_PIN_SET); }
void pump_off(void) { HAL_GPIO_WritePin(PUMP_GPIO_PORT, PUMP_PIN, GPIO_PIN_RESET); }

int main(void) {
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_USB_DEVICE_Init();
    MX_ADC1_Init();

    HAL_Delay(500);
    usb_printf("STM32 USB Ready\r\n");

    uint8_t cmd = 0x08;
    HAL_I2C_Mem_Write(&hi2c1, ADXL345_ADDR, ADXL345_REG_POWER_CTL, 1, &cmd, 1, 100);
    HAL_Delay(100);

    enum neai_state error_code = neai_anomalydetection_init();
    usb_printf("NanoEdgeAI init: %s\r\n", error_code == NEAI_OK ? "OK" : "ERROR");

    for (uint16_t i = 0; i < 20; i++) {
        usb_printf("Learning iteration %d/20\r\n", i + 1);
        fill_accelerometer_buffer();
        neai_anomalydetection_learn(acc_buffer);
    }
    usb_printf("Learning finished\r\n");

    calibrate_full_water_level();
    uint8_t similarity = 0;

    while (1) {
        // ADXL345 anomaly detection
        fill_accelerometer_buffer();
        neai_anomalydetection_detect(acc_buffer, &similarity);

        if (similarity >= 90)
            usb_printf("NOMINAL,%d\r\n", similarity);
        else
            usb_printf("ANOMALY,%d\r\n", similarity);

        // Water level
        uint16_t water_level = read_water_level();
        float level_percent = (100.0f * water_level) / water_level_full;
        usb_printf("WaterLevel,%d,%.2f%%\r\n", water_level, level_percent);

        // Pump control
        if (level_percent < WATER_LOW_THRESHOLD_PERCENT) {
            pump_on();
            usb_printf("Pump ON\r\n");
        } else {
            pump_off();
            usb_printf("Pump OFF\r\n");
        }

        // DS18B20 Temperature
        float temperature = DS18B20_GetTemp();
        usb_printf("WaterTemp,%.2fC\r\n", temperature);

        HAL_Delay(1000);
    }
}

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 72;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 3;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {}
}
