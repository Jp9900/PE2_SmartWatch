
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"

static const char *TAG = "BLE_ADC_BATTERY";

// BLE Service & Char UUIDs
#define GATTS_SERVICE_UUID_BATTERY    0x180F
#define GATTS_CHAR_UUID_BATTERY_LEVEL 0x2A19
#define GATTS_NUM_HANDLE              4

// GPIO voor knop en LED
#define GPIO_BUTTON     2
#define GPIO_LED        19
#define DEBOUNCE_TIME_MS 200

// ADC-configuratie
#define GPIO_ADC_ENABLE    3
#define ADC_GPIO           4
#define ADC_CHANNEL        ADC_CHANNEL_3  // GPIO4 komt overeen met kanaal 3 op unit 1
#define ADC_UNIT           ADC_UNIT_1
#define ADC_ATTEN          ADC_ATTEN_DB_12
#define ADC_BIT_WIDTH      ADC_BITWIDTH_12

// BLE state
static uint16_t gatts_if_global = 0;
static uint16_t conn_id_global = 0;
static uint16_t battery_level_handle = 0;
static uint16_t cccd_handle = 0;
static bool is_connected = false;
static bool notify_enabled = false;
static uint8_t battery_level = 0; // Batterijpercentage

// ADC
static adc_oneshot_unit_handle_t adc_handle;

// Knop/LED status
static bool led_state = false;
static int64_t last_button_press_time = 0;

void init_adc(void);
uint8_t read_battery_percentage(void);



// Functie om knop te controleren en LED te togglen met debounce
void check_button_toggle_led() {
    static bool last_button_state = true; // Start met niet-ingedrukt (hoog door pull-up)
    bool current_state = gpio_get_level(GPIO_BUTTON);
    int64_t now = esp_timer_get_time() / 1000; // in ms

    if (!current_state && last_button_state) {
        if (now - last_button_press_time > DEBOUNCE_TIME_MS) {
            // Geldige druk
            led_state = !led_state;
            gpio_set_level(GPIO_LED, led_state);
            ESP_LOGI(TAG, "Knop gedrukt - LED %s", led_state ? "AAN" : "UIT");
            last_button_press_time = now;
        }
    }

    last_button_state = current_state;
}
// Initialiseer GPIO's voor knop en LED
void init_gpio_button_led() {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << GPIO_LED);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    gpio_set_level(GPIO_LED, led_state); // Startstatus
}


// BLE GAP event handler
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "Adv data set complete, start advertising");
            esp_ble_adv_params_t adv_params = {
                .adv_int_min = 0x20,
                .adv_int_max = 0x40,
                .adv_type = ADV_TYPE_IND,
                .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
                .channel_map = ADV_CHNL_ALL,
                .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
            };
            esp_ble_gap_start_advertising(&adv_params);
            break;
        default:
            break;
    }
}
// Functie om spanning te lezen en om te zetten naar % batterij
uint8_t read_battery_percentage()
{
    int raw = 0;
    esp_err_t ret = adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC lezen mislukt: %s", esp_err_to_name(ret));
        return 0;
    }

    float voltage = (raw * 3.3f / 4095.0f) * 1000.0f;  // naar mV
    voltage *= 2.0f; // als er spanningsdeler wordt gebruikt

    ESP_LOGI(TAG, "ADC Raw: %d, voltage: %.2f mV", raw, voltage);

    // Eenvoudige mapping van voltage naar % (bijv. 3000mV = 0%, 4200mV = 100%)
    float percentage = (voltage - 3000.0f) / (4200.0f - 3000.0f) * 100.0f;

    if (percentage > 100.0f) percentage = 100.0f;
    if (percentage < 0.0f) percentage = 0.0f;

    return (uint8_t)percentage;
}
void init_adc()
{
    // GPIO configureren voor ADC enable (GPIO3 als uitgang)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_ADC_ENABLE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Zet ADC enable hoog
    gpio_set_level(GPIO_ADC_ENABLE, 1);
    vTaskDelay(pdMS_TO_TICKS(1000)); // Even wachten

    // Initialiseer ADC unit
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    // Kanaal configureren
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BIT_WIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg));
}
// BLE GATTS event handler
static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                        esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT: {
            ESP_LOGI(TAG, "GATTS_REG_EVT");
            esp_ble_gap_set_device_name("ESP32-Battery");
            esp_ble_gap_config_adv_data(&(esp_ble_adv_data_t){
                .set_scan_rsp = false,
                .include_name = true,
                .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
            });

            esp_gatt_srvc_id_t service_id = {
                .is_primary = true,
                .id.inst_id = 0,
                .id.uuid.len = ESP_UUID_LEN_16,
                .id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_BATTERY,
            };

            esp_ble_gatts_create_service(gatts_if, &service_id, GATTS_NUM_HANDLE);
            break;
        }

        case ESP_GATTS_CREATE_EVT: {
            ESP_LOGI(TAG, "GATTS_CREATE_EVT, service_handle %d", param->create.service_handle);
            esp_ble_gatts_start_service(param->create.service_handle);

            esp_bt_uuid_t char_uuid = {
                .len = ESP_UUID_LEN_16,
                .uuid.uuid16 = GATTS_CHAR_UUID_BATTERY_LEVEL,
            };
            esp_gatt_char_prop_t property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
            esp_attr_control_t control = {.auto_rsp = ESP_GATT_AUTO_RSP};

            esp_attr_value_t char_val = {
                .attr_max_len = sizeof(battery_level),
                .attr_len = sizeof(battery_level),
                .attr_value = &battery_level,
            };

            esp_ble_gatts_add_char(param->create.service_handle, &char_uuid,
                                   ESP_GATT_PERM_READ,
                                   property, &char_val, &control);
            break;
        }

        case ESP_GATTS_ADD_CHAR_EVT: {
            ESP_LOGI(TAG, "GATTS_ADD_CHAR_EVT, attr_handle = %d", param->add_char.attr_handle);
            battery_level_handle = param->add_char.attr_handle;

            esp_bt_uuid_t descr_uuid = {
                .len = ESP_UUID_LEN_16,
                .uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,
            };
            esp_ble_gatts_add_char_descr(param->add_char.service_handle,
                                         &descr_uuid,
                                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                         NULL, NULL);
            break;
        }

        case ESP_GATTS_ADD_CHAR_DESCR_EVT: {
            ESP_LOGI(TAG, "GATTS_ADD_CHAR_DESCR_EVT, attr_handle = %d", param->add_char_descr.attr_handle);
            cccd_handle = param->add_char_descr.attr_handle;
            break;
        }

        case ESP_GATTS_CONNECT_EVT: {
            ESP_LOGI(TAG, "Client connected");
            gatts_if_global = gatts_if;
            conn_id_global = param->connect.conn_id;
            is_connected = true;
            notify_enabled = false;
            break;
        }

        case ESP_GATTS_DISCONNECT_EVT: {
            ESP_LOGI(TAG, "Client disconnected");
            is_connected = false;
            notify_enabled = false;

            esp_ble_adv_params_t adv_params = {
                .adv_int_min = 0x20,
                .adv_int_max = 0x40,
                .adv_type = ADV_TYPE_IND,
                .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
                .channel_map = ADV_CHNL_ALL,
                .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
            };
            esp_ble_gap_start_advertising(&adv_params);
            break;
        }

        case ESP_GATTS_WRITE_EVT: {
            if (param->write.handle == cccd_handle && param->write.len == 2) {
                uint16_t descr_value = param->write.value[0] | (param->write.value[1] << 8);
                notify_enabled = (descr_value == 0x0001);
                ESP_LOGI(TAG, "Notify %s by client", notify_enabled ? "enabled" : "disabled");
            }

            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                            param->write.trans_id, ESP_GATT_OK, NULL);
            }
            break;
        }

        default:
            break;
    }
}

uint8_t read_battery_percentage_with_enable(bool enable)
{
    gpio_set_level(GPIO_ADC_ENABLE, enable ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(10));  // korte stabilisatie

    int raw = 0;
    esp_err_t ret = adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC lezen mislukt (%s): enable=%d", esp_err_to_name(ret), enable);
        return 0;
    }

    float voltage = (raw * 3.3f / 4095.0f) * 1000.0f;
    voltage *= 2.0f;

    ESP_LOGI(TAG, "[%s] ADC Raw: %d, Voltage: %.2f mV",
             enable ? "AAN" : "UIT", raw, voltage);

    float percentage = (voltage - 3000.0f) / (4200.0f - 3000.0f) * 100.0f;
    if (percentage > 100.0f) percentage = 100.0f;
    if (percentage < 0.0f) percentage = 0.0f;

    return (uint8_t)percentage;
}

void app_main(void)
{
    esp_err_t ret;

    // NVS init
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Bluetooth stack init
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_profile_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));

    init_adc();              // Init ADC
    init_gpio_button_led();  // Init GPIO voor knop en LED

    int loop_counter = 0;

     // Hoofdlus
    while (1) {
        check_button_toggle_led();  // Check knop elke 100 ms

        // Elke 5 seconden beide waardes lezen
        if (loop_counter % 50 == 0) {
            // 1. Meting met ADC enable AAN
            uint8_t batt_on = read_battery_percentage_with_enable(true);
            ESP_LOGI(TAG, "Batterij actief (AAN): %d%%", batt_on);

            if (is_connected && notify_enabled) {
                esp_ble_gatts_send_indicate(gatts_if_global, conn_id_global,
                                            battery_level_handle,
                                            sizeof(batt_on), &batt_on, false);
                ESP_LOGI(TAG, "Notificatie verzonden: [AAN]");
            }

            vTaskDelay(pdMS_TO_TICKS(1000));  // Wacht 1 seconde

            // 2. Meting met ADC enable UIT
            uint8_t batt_off = read_battery_percentage_with_enable(false);
            ESP_LOGI(TAG, "Batterij passief (UIT): %d%%", batt_off);

            if (is_connected && notify_enabled) {
                esp_ble_gatts_send_indicate(gatts_if_global, conn_id_global,
                                            battery_level_handle,
                                            sizeof(batt_off), &batt_off, false);
                ESP_LOGI(TAG, "Notificatie verzonden: [UIT]");
            }
        }

        loop_counter++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

}


