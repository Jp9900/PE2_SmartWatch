#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_sleep.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "COMBINED_APP";

// --- Pin definitions ---
#define BUTTON_SLEEP_GPIO   GPIO_NUM_1   // Knop voor deep sleep (input, pull-up)
#define BUTTON_WAKE_GPIO    GPIO_NUM_0   // Knop om uit deep sleep te komen (EXT0 wakeup pin, input, pull-up)
#define BUTTON_LED_TOGGLE   GPIO_NUM_2   // Knop om LED aan/uit te schakelen (input, pull-up)
#define LED_GPIO            GPIO_NUM_19  // LED output

// ADC config
#define ADC_GPIO            GPIO_NUM_4
#define ADC_UNIT            ADC_UNIT_1
#define ADC_CHANNEL         ADC_CHANNEL_4  // GPIO4 = ADC1_CHANNEL_4
#define ADC_ATTEN           ADC_ATTEN_DB_12 // 0-3.6V range
#define ADC_BIT_WIDTH       ADC_BITWIDTH_12

#define DEBOUNCE_DELAY_MS  100

// BLE UUIDs
#define GATTS_SERVICE_UUID_BATTERY    0x180F
#define GATTS_CHAR_UUID_BATTERY_LEVEL 0x2A19
#define GATTS_NUM_HANDLE              4

// --- Globals BLE ---
static uint16_t gatts_if_global = 0;
static uint16_t conn_id_global = 0;
static uint16_t battery_level_handle = 0;
static uint16_t cccd_handle = 0;
static bool is_connected = false;
static bool notify_enabled = false;

// Battery level 0..100%
static uint8_t battery_level = 0;

// Timers
static TimerHandle_t debounce_timer_sleep;
static TimerHandle_t debounce_timer_button;
static volatile bool button_press_pending = false;

// ADC handle
static adc_oneshot_unit_handle_t adc_handle = NULL;

// --- Forward declarations BLE handlers ---
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                        esp_ble_gatts_cb_param_t *param);

                                        

// --- Debounce timer callback for deep sleep button ---
static void debounce_timer_sleep_cb(TimerHandle_t xTimer)
{
    // Check knop nog steeds ingedrukt (active low)
    if (gpio_get_level(BUTTON_SLEEP_GPIO) == 0) {
        ESP_LOGI(TAG, "Sleep knop bevestigd, ga in deep sleep");

        // LED uitzetten vlak voor slapen
        gpio_set_level(LED_GPIO, 0);

        // Configureer wake-up bron: EXT0 op BUTTON_WAKE_GPIO, active low
        esp_sleep_enable_ext0_wakeup(BUTTON_WAKE_GPIO, 0);

        // Start deep sleep
        esp_deep_sleep_start();
    } else {
        ESP_LOGI(TAG, "Sleep knop niet meer ingedrukt, negeer");
    }
}

// ISR voor deep sleep knop
static void IRAM_ATTR gpio_isr_handler_sleep(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTimerStartFromISR(debounce_timer_sleep, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

// --- Debounce timer callback voor LED toggle knop ---
static void debounce_timer_button_cb(TimerHandle_t xTimer)
{
    int level = gpio_get_level(BUTTON_LED_TOGGLE);
    if (level == 0) {
        int led_level = gpio_get_level(LED_GPIO);
        gpio_set_level(LED_GPIO, !led_level);
        ESP_LOGI(TAG, "Button pressed, LED toggled to %d", !led_level);
    }
    button_press_pending = false;
}

// ISR voor LED toggle knop
static void IRAM_ATTR gpio_isr_handler_button(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (!button_press_pending) {
        button_press_pending = true;
        xTimerStartFromISR(debounce_timer_button, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

// --- BLE event handlers ---
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT: {
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
        }
        default:
            break;
    }
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                        esp_ble_gatts_cb_param_t *param)
{
    switch (event) {
        case ESP_GATTS_REG_EVT: {
            ESP_LOGI(TAG, "GATTS_REG_EVT");
            esp_ble_gap_set_device_name("ESP32-BatteryADC");
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

            // Voeg CCCD toe
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
            ESP_LOGI(TAG, "GATTS_WRITE_EVT, handle %d, len %d", param->write.handle, param->write.len);

            if (param->write.handle == cccd_handle && param->write.len == 2) {
                uint16_t descr_value = param->write.value[0] | (param->write.value[1] << 8);
                notify_enabled = (descr_value == 0x0001);
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

void app_main(void)
{
     // Sleep knop input (GPIO1), pull-up, falling edge interrupt
    gpio_config_t sleep_btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_SLEEP_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&sleep_btn_conf);

    // Wake knop input (GPIO0), pull-up, geen interrupt
    gpio_config_t wake_btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_WAKE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&wake_btn_conf);

    // LED toggle knop input (GPIO2), pull-up, falling edge interrupt
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_LED_TOGGLE),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&btn_conf);

       // LED output
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
    
    gpio_set_level(LED_GPIO, 0);
    esp_err_t ret;

    // --- NVS init ---
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- Bluetooth controller init only BLE ---
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(gatts_profile_event_handler));
    ESP_ERROR_CHECK(esp_ble_gatts_app_register(0));



    // --- Maak debounce timers aan ---
    debounce_timer_sleep = xTimerCreate("debounce_sleep", pdMS_TO_TICKS(DEBOUNCE_DELAY_MS), pdFALSE, NULL, debounce_timer_sleep_cb);
    debounce_timer_button = xTimerCreate("debounce_button", pdMS_TO_TICKS(DEBOUNCE_DELAY_MS), pdFALSE, NULL, debounce_timer_button_cb);

    // --- Installeer ISR service en koppel ISR handlers ---
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_SLEEP_GPIO, gpio_isr_handler_sleep, NULL);
    gpio_isr_handler_add(BUTTON_LED_TOGGLE, gpio_isr_handler_button, NULL);

    // --- ADC setup ---
    adc_oneshot_unit_init_cfg_t adc_init_cfg = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_init_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BIT_WIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg));

    ESP_LOGI(TAG, "Start main loop");

    while (1) {
        int raw = 0;
        ret = adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw);
        if (ret == ESP_OK) {
            float voltage = (raw * 3.3f) / 4095.0f;
            // Indien voltage divider aanwezig, corrigeer hier (bijv x2)
            // voltage *= 2;

            battery_level = (uint8_t)(voltage / 3.3f * 100);

            ESP_LOGI(TAG, "ADC Raw: %d, Voltage: %.2f V, Battery Level: %d%%", raw, voltage, battery_level);

            if (is_connected && notify_enabled) {
                esp_ble_gatts_send_indicate(gatts_if_global, conn_id_global,
                                           battery_level_handle,
                                           sizeof(battery_level),
                                           &battery_level,
                                           false);
                ESP_LOGI(TAG, "Notify sent");
            }
        } else {
            ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


