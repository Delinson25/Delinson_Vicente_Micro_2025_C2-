#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "freertos/timers.h"
#include "driver/adc.h"


#define LED1_PIN  2
#define LED_R_PIN 33
#define LED_G_PIN 25
#define LED_B_PIN 26


static const char *TAG = "Main";
static TimerHandle_t xTimers;

static const int TIMER_INTERVAL_MS = 50; 
static const int TIMER_ID = 1;           

static uint8_t led1_state = 0;
static int adc_value = 0;

static esp_err_t init_leds(void);
static esp_err_t blink_led1(void);
static esp_err_t init_timer(void);
static esp_err_t init_adc(void);
static void set_rgb_led(int r, int g, int b);



static void vTimerCallback(TimerHandle_t pxTimer)
{

    blink_led1();

    
    adc_value = adc1_get_raw(ADC1_CHANNEL_4);
    ESP_LOGI(TAG, "ADC VAL: %d", adc_value);

    
    int adc_case = adc_value / 1000; 

    switch (adc_case) {
        case 0:
            set_rgb_led(0, 0, 0);
            break;
        case 1:
            set_rgb_led(1, 0, 0);
            break;
        case 2:
            set_rgb_led(1, 1, 0);
            break;
        case 3:
        case 4:
            set_rgb_led(1, 1, 1);
            break;
        default:
            
            break;
    }
}


static esp_err_t init_leds(void)
{
    int leds[] = {LED1_PIN, LED_R_PIN, LED_G_PIN, LED_B_PIN};
    for (int i = 0; i < 4; i++) {
        gpio_reset_pin(leds[i]);
        gpio_set_direction(leds[i], GPIO_MODE_OUTPUT);
    }
    return ESP_OK;
}


static esp_err_t blink_led1(void)
{
    led1_state = !led1_state;
    gpio_set_level(LED1_PIN, led1_state);
    return ESP_OK;
}


static void set_rgb_led(int r, int g, int b)
{
    gpio_set_level(LED_R_PIN, r);
    gpio_set_level(LED_G_PIN, g);
    gpio_set_level(LED_B_PIN, b);
}


static esp_err_t init_timer(void)
{
    ESP_LOGI(TAG, "Inicializando Timer...");
    xTimers = xTimerCreate(
        "Timer",
        pdMS_TO_TICKS(TIMER_INTERVAL_MS),
        pdTRUE,
        (void *)TIMER_ID,
        vTimerCallback
    );

    if (xTimers == NULL) {
        ESP_LOGE(TAG, "El timer no fue creado...");
        return ESP_FAIL;
    }

    if (xTimerStart(xTimers, 0) != pdPASS) {
        ESP_LOGE(TAG, "El timer no pudo ponerse en estado activo");
        return ESP_FAIL;
    }

    return ESP_OK;
}


static esp_err_t init_adc(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
    return ESP_OK;
}


void app_main(void)
{
    init_leds();
    init_adc();
    init_timer();
}
