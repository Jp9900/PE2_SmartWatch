#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LED_GPIO 21  // Onboard LED van Beetle ESP32-C6

static const char *TAG = "BLINKY";

void app_main(void) {
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    while (1) {
        gpio_set_level(LED_GPIO, 0); // LED aan
        ESP_LOGI(TAG, "hihi");
        vTaskDelay(pdMS_TO_TICKS(100));
        
        gpio_set_level(LED_GPIO, 1); // LED uit
        ESP_LOGI(TAG, "haha");
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
