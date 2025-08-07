#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_timer.h"

static const char *TAG = "mqttws_example";

// Máquina de estados
typedef enum
{
    ESTADO_INICIAL,
    ESTADO_ESPERANDO,
    ESTADO_PROCESANDO
} estado_t;

estado_t estado_actual = ESTADO_INICIAL;
esp_mqtt_client_handle_t global_mqtt_client = NULL; // Cliente global

// Función que se ejecutará cada 50 ms
void timer_callback(void *arg)
{
    static int contador_ciclos = 0;
    contador_ciclos++;

    // Cambio de estado
    switch (estado_actual)
    {
    case ESTADO_INICIAL:
        ESP_LOGI(TAG, "Estado: INICIAL → ESPERANDO");
        estado_actual = ESTADO_ESPERANDO;
        break;
    case ESTADO_ESPERANDO:
        ESP_LOGI(TAG, "Estado: ESPERANDO → PROCESANDO");
        estado_actual = ESTADO_PROCESANDO;
        break;
    case ESTADO_PROCESANDO:
        ESP_LOGI(TAG, "Estado: PROCESANDO → INICIAL");
        estado_actual = ESTADO_INICIAL;
        break;
    default:
        break;
    }

    // Cada 20 ciclos (1 segundo), publicar el estado por MQTT
    if (contador_ciclos >= 20)
    {
        contador_ciclos = 0;

        char mensaje[64];
        switch (estado_actual)
        {
        case ESTADO_INICIAL:
            snprintf(mensaje, sizeof(mensaje), "Estado: INICIAL");
            break;
        case ESTADO_ESPERANDO:
            snprintf(mensaje, sizeof(mensaje), "Estado: ESPERANDO");
            break;
        case ESTADO_PROCESANDO:
            snprintf(mensaje, sizeof(mensaje), "Estado: PROCESANDO");
            break;
        default:
            snprintf(mensaje, sizeof(mensaje), "Estado: DESCONOCIDO");
        }

        if (global_mqtt_client != NULL)
        {
            esp_mqtt_client_publish(global_mqtt_client, "tarea5-mqtt/estado", mensaje, 0, 0, 0);
            ESP_LOGI(TAG, "Publicado por MQTT: %s", mensaje);
        }
    }
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        msg_id = esp_mqtt_client_subscribe(client, "tarea5-mqtt/ilumina", 0);
        ESP_LOGI(TAG, "Suscripción exitosa, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_publish(client, "tarea5-mqtt/ilumina", "data:30", 0, 0, 0);
        ESP_LOGI(TAG, "Publicación inicial exitosa, msg_id=%d", msg_id);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;

    default:
        ESP_LOGI(TAG, "Otro evento id: %d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.emqx.io:1883",
        .credentials.username = "tarea5-mqtt",
        .credentials.authentication.password = "tarea5-mqtt"};

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    // Guardar en variable global para uso desde el timer
    global_mqtt_client = client;
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Inicio del sistema...");
    ESP_LOGI(TAG, "[APP] Memoria libre: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Conexión a red WiFi (usa configuración de menuconfig)
    ESP_ERROR_CHECK(example_connect());

    // Iniciar cliente MQTT
    mqtt_app_start();

    // Crear y arrancar timer de 50 ms
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &timer_callback,
        .name = "mi_timer_50ms"};

    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 50000)); // 50 ms
}
