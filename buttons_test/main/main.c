#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LED_GPIO     19      // LED op GPIO19
#define BUTTON_ON    0       // Knop voor AAN op GPIO1
#define BUTTON_OFF   1       // Knop voor UIT op GPIO2


static const char *TAG = "BUTTON_LED";

// ISR (Interrupt Service Routine) flags
volatile bool led_on_flag = false;
volatile bool led_off_flag = false;

#define DEBOUNCE_TICKS pdMS_TO_TICKS(50)  // debounce van 50 ms

static volatile uint32_t last_interrupt_time_on = 0;
static volatile uint32_t last_interrupt_time_off = 0;

// Algemene debounce ISR handler
static IRAM_ATTR bool debounce_check(volatile uint32_t *last_time_var) {
    uint32_t now = xTaskGetTickCountFromISR(); // Huidige tijd in RTOS-ticks
    if ((now - *last_time_var) > DEBOUNCE_TICKS) { // Genoeg tijd verstreken?
        *last_time_var = now; // Update laatst bekende tijd
        return true;  // OK, geldige trigger
    }
    return false; // Te snel = bounce = negeren
}

// ISR voor knop ON
static IRAM_ATTR void button_on_isr_handler(void *arg) {
    if (debounce_check(&last_interrupt_time_on)) {
        led_on_flag = true;
    }
}

// ISR voor knop OFF
static IRAM_ATTR void button_off_isr_handler(void *arg) {
    if (debounce_check(&last_interrupt_time_off)) {
        led_off_flag = true;
    }
}


void setup_gpio(void) {
    // Configureer LED GPIO
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    // Configureer knop GPIO's als input met pull-up en interrupt
    gpio_reset_pin(BUTTON_ON);
    gpio_set_direction(BUTTON_ON, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_ON);
    gpio_set_intr_type(BUTTON_ON, GPIO_INTR_NEGEDGE);  // trigger op dalende flank

    // Configureer BUTTON_OFF
    gpio_reset_pin(BUTTON_OFF);
    gpio_set_direction(BUTTON_OFF, GPIO_MODE_INPUT);
    gpio_pullup_en(BUTTON_OFF);
    gpio_set_intr_type(BUTTON_OFF, GPIO_INTR_NEGEDGE);

    // Installeer ISR service
    gpio_install_isr_service(0);  // 0 = default interrupt priority
    gpio_isr_handler_add(BUTTON_ON, button_on_isr_handler, NULL);
    gpio_isr_handler_add(BUTTON_OFF, button_off_isr_handler, NULL);
}

void app_main(void) {

    setup_gpio();  // Initialiseer GPIO's
    ESP_LOGI(TAG, "Starten van LED en knop applicatie");

    while (1) {
        // Check ISR-flags
        if (led_on_flag) {
            gpio_set_level(LED_GPIO, 1);
            ESP_LOGI(TAG, "LED AAN door knop op GPIO1");
            led_on_flag = false;
        }

        if (led_off_flag) {
            gpio_set_level(LED_GPIO, 0);
            ESP_LOGI(TAG, "LED UIT door knop op GPIO2");
            led_off_flag = false;
        }

      
    }
}
