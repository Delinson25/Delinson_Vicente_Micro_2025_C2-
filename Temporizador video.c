#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define led1 2

uint8_t led_level = 0;
static const char *tag = "Main";
TimerHandle_t xTimers;
int interval = 1000;
int timerID = 1;

// Declaraciones de funciones
esp_err_t init_led(void);
esp_err_t blink_led(void);
esp_err_t set_timer(void);

// Función de callback del temporizador
void vTimerCallback(TimerHandle_t xTimer)
{
    blink_led();
    ESP_LOGI(tag, "Temporizador activado, LED cambiado de estado");
}

// Función principal
void app_main(void)
{
    init_led();
    set_timer();

    // Ya no es necesario llamar a blink_led aquí porque el temporizador controla el parpadeo
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Espera prolongada, el temporizador ya maneja el parpadeo
    }
}

// Inicializa el LED como salida
esp_err_t init_led(void)
{
    gpio_reset_pin(led1);
    gpio_set_direction(led1, GPIO_MODE_OUTPUT);
    return ESP_OK;
}

// Cambia el estado del LED (encendido/apagado)
esp_err_t blink_led(void)
{
    led_level = !led_level;
    gpio_set_level(led1, led_level);
    return ESP_OK;
}

// Configura el temporizador de software
esp_err_t set_timer(void)
{
    ESP_LOGI(tag, "Configuración inicial del temporizador");
    xTimers = xTimerCreate("Timer",              // Nombre del temporizador
                           pdMS_TO_TICKS(interval), // Período de 1 segundo
                           pdTRUE,               // Recarga automática
                           (void *)timerID,      // ID opcional
                           vTimerCallback);      // Función de callback

    if (xTimers == NULL)
    {
        ESP_LOGE(tag, "Error al crear el temporizador");
        return ESP_FAIL;
    }
    else
    {
        if (xTimerStart(xTimers, 0) != pdPASS)
        {
            ESP_LOGE(tag, "Error al iniciar el temporizador");
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}
