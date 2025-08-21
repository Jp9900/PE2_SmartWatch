/*
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "ADC_BATTERY";

// ==== Configuratie ====
// Zet hier je enable-pin
#define GPIO_ADC_ENABLE  4
// GPIO5 = ADC1_CH3 op ESP32-C6
#define ADC_CHANNEL      ADC_CHANNEL_5
#define ADC_UNIT         ADC_UNIT_1
#define ADC_BIT_WIDTH    ADC_BITWIDTH_12
#define ADC_VREF         3550  // referentiespanning in mV bij ADC_ATTEN_DB_12
#define ADC_MAX_VALUE    4095  // 12-bit resolutie

static adc_oneshot_unit_handle_t adc_handle;

// Functie om ADC uit te lezen en om te zetten naar reële batterijspanning
float calcvoltage(void)
{
    int raw_adc = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw_adc));
    ESP_LOGI(TAG, "RAW ADC waarde: %d", raw_adc);
    
    float voltage = ((float)raw_adc / ADC_MAX_VALUE) * ADC_VREF; // spanning over de deler
    return voltage * 2.0f; // spanningsdeler correctie (maal 2)
}

// ==== Initialisatie ====
void init_adc(void)
{
    // Enable-pin als uitgang
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_ADC_ENABLE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // ADC unit initialiseren
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    // Kanaal configureren
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,  // meetbereik instellen
        .bitwidth = ADC_BIT_WIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg));
}

// ==== app_main ====
void app_main(void)
{
    init_adc();
    int raw = 0;
    while (1) {
        // Enable ADC
        gpio_set_level(GPIO_ADC_ENABLE, 1);
        vTaskDelay(pdMS_TO_TICKS(1000)); // korte stabilisatie

        float voltage = calcvoltage();
        ESP_LOGI(TAG, "Batterijspanning (AAN): %.2f mV", voltage);
       
    }
}
*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "ADC_BATTERY";

// ==== Configuratie ====
// Zet hier je enable-pin
#define GPIO_ADC_ENABLE  4
// GPIO5 = ADC1_CH3 op ESP32-C6
#define ADC_CHANNEL      ADC_CHANNEL_5
#define ADC_UNIT         ADC_UNIT_1
#define ADC_BIT_WIDTH    ADC_BITWIDTH_12

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t adc_cali_handle;
static bool adc_calibrated = false;

void init_adc(void)
{
    // Enable-pin als uitgang
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_ADC_ENABLE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // ADC unit initialiseren
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    // Kanaal configureren
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,  // meetbereik instellen
        .bitwidth = ADC_BIT_WIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg));

    // Calibratie instellen
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BIT_WIDTH,
    };

    if (adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle) == ESP_OK) {
        adc_calibrated = true;
        ESP_LOGI(TAG, "ADC calibratie actief (curve fitting)");
    } else {
        ESP_LOGW(TAG, "ADC calibratie NIET beschikbaar, gebruik raw waarden");
    }
}


// Functie om batterijspanning te meten
float calcvoltage(void)
{
    int raw_adc = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw_adc));
    ESP_LOGI(TAG, "RAW ADC waarde: %d", raw_adc);

    int voltage_mv = 0;
    if (adc_calibrated) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, raw_adc, &voltage_mv));
    } else {
        // fallback zonder calibratie (ruwer)
        voltage_mv = (raw_adc * 3550) / 4095;
    }

    // Spanningsdeler correctie (2x)
    return voltage_mv * 2.0f;
}

// ==== app_main ====
void app_main(void)
{
    init_adc();

    while (1) {
        gpio_set_level(GPIO_ADC_ENABLE, 1);
        vTaskDelay(pdMS_TO_TICKS(500)); // korte stabilisatie

        float batt_mv = calcvoltage();
        ESP_LOGI(TAG, "Batterijspanning: %.2f mV", batt_mv);

        gpio_set_level(GPIO_ADC_ENABLE, 0);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
