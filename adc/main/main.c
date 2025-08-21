

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

    float batt_percent = (batt_mv - 3000.0f) / (4200.0f - 3000.0f) * 100.0f;
    if (batt_percent < 0) batt_percent = 0;
    if (batt_percent > 100) batt_percent = 100;
    ESP_LOGI(TAG, "Batterijpercentage: %.1f%%", batt_percent);

    // === Lifetime berekening ===
    // Stel batterijcapaciteit in (voorbeeld: 300mAh)
    float battery_capacity_mAh = 300.0f;
    // Totaal stroomverbruik (display + ESP + RTC), schatting: 40mA (display) + 10mA (ESP idle) + 1mA (RTC)
    float current_draw_mA = 40.0f + 10.0f + 1.0f; // pas aan indien nodig
    // Resterende capaciteit in mAh
    float remaining_capacity_mAh = (batt_percent / 100.0f) * battery_capacity_mAh;
    // Resterende tijd in uren
    float hours_left = remaining_capacity_mAh / current_draw_mA;
    // Omzetten naar uren en minuten
    int hours = (int)hours_left;
    int minutes = (int)((hours_left - hours) * 60);
    ESP_LOGI(TAG, "Geschatte resterende gebruiksduur: %d uur %d min", hours, minutes);

        gpio_set_level(GPIO_ADC_ENABLE, 0);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
