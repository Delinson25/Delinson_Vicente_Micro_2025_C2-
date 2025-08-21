#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "driver/gpio.h"

// WiFi
#define WIFI_SSID      "Delinson"
#define WIFI_PASS      "0123456789"
#define WIFI_RETRY_MAX 5

// MQTT
#define MQTT_SERVER    "mqtt://test.mosquitto.org:1883"
#define MQTT_CHANNEL   "delinson_mqtt/esp32_test"

// Pines
#define MOTOR_ARR GPIO_NUM_5
#define MOTOR_CIE GPIO_NUM_18
#define BUZZER    GPIO_NUM_27
#define LAMP      GPIO_NUM_26
#define BTN_ARR   GPIO_NUM_33
#define BTN_CIE   GPIO_NUM_32
#define BTN_STOP  GPIO_NUM_35
#define BTN_PP    GPIO_NUM_34
#define FIN_ARR   GPIO_NUM_25
#define FIN_CIE   GPIO_NUM_14

typedef enum {INI,CERR,ABR,CIER,ABIE,ERR,STOP,EMERG} estado_t;
volatile estado_t estAct=INI, estSig=INI;
volatile bool init_accion=true;

struct { uint8_t MA:1, MC:1, BZ:1, LAMP:1; } io;
static bool lamp_on=false;
static int64_t last_time=0, blink_ms=0;

static EventGroupHandle_t wifi_events;
#define WIFI_OK BIT0
#define WIFI_FAIL BIT1

esp_mqtt_client_handle_t mqtt_client = NULL;

static void fsm_set_state(estado_t s){ estSig=s; init_accion=true; }
static void actualizar_io(void){
    gpio_set_level(MOTOR_ARR,io.MA);
    gpio_set_level(MOTOR_CIE,io.MC);
    gpio_set_level(BUZZER,io.BZ);
    gpio_set_level(LAMP,io.LAMP);
}

static bool leer_btn(uint8_t p){return gpio_get_level(p)==0;}
static bool leer_fin(uint8_t p){return gpio_get_level(p)==1;}

static void gpio_setup(void){
    gpio_config_t cfg={0};
    cfg.intr_type=GPIO_INTR_DISABLE; cfg.mode=GPIO_MODE_OUTPUT;
    cfg.pin_bit_mask=(1ULL<<MOTOR_ARR)|(1ULL<<MOTOR_CIE)|(1ULL<<BUZZER)|(1ULL<<LAMP);
    gpio_config(&cfg);
    cfg.mode=GPIO_MODE_INPUT; cfg.pin_bit_mask=(1ULL<<BTN_ARR)|(1ULL<<BTN_CIE)|(1ULL<<BTN_STOP)|(1ULL<<BTN_PP);
    cfg.pull_up_en=GPIO_PULLUP_ENABLE; gpio_config(&cfg);
    cfg.pin_bit_mask=(1ULL<<FIN_ARR)|(1ULL<<FIN_CIE); cfg.pull_down_en=GPIO_PULLDOWN_ENABLE; gpio_config(&cfg);
}

static void handle_buttons(void){
    static bool a=0,c=0,s=0,p=0;
    bool btn_a=leer_btn(BTN_ARR),btn_c=leer_btn(BTN_CIE),btn_s=leer_btn(BTN_STOP),btn_p=leer_btn(BTN_PP);
    if(btn_a && !a) fsm_set_state(ABR);
    if(btn_c && !c) fsm_set_state(CERR);
    if(btn_s && !s) fsm_set_state(STOP);
    if(btn_p && !p) fsm_set_state((estAct==CIER)?ABR:(estAct==ABIE)?CERR:CERR);
    a=btn_a; c=btn_c; s=btn_s; p=btn_p;
}

void fsm_execute(void){
    estAct=estSig;
    int64_t now=esp_timer_get_time()/1000;
    handle_buttons();
    if(leer_fin(FIN_ARR)&&leer_fin(FIN_CIE)&&estAct!=ERR) fsm_set_state(ERR);

    switch(estAct){
        case INI: if(init_accion){io.MA=io.MC=io.BZ=io.LAMP=0; blink_ms=0; init_accion=false; actualizar_io();} break;
        case ABR: if(init_accion){io.MA=1;io.MC=0;io.BZ=0;blink_ms=500;lamp_on=false;last_time=now;init_accion=false;actualizar_io();}
                  if(blink_ms>0 && now-last_time>=blink_ms){lamp_on=!lamp_on;io.LAMP=lamp_on;last_time=now;actualizar_io();}
                  if(leer_fin(FIN_ARR)) fsm_set_state(ABIE); break;
        case CERR: if(init_accion){io.MA=0;io.MC=1;io.BZ=0;blink_ms=250;lamp_on=false;last_time=now;init_accion=false;actualizar_io();}
                  if(blink_ms>0 && now-last_time>=blink_ms){lamp_on=!lamp_on;io.LAMP=lamp_on;last_time=now;actualizar_io();}
                  if(leer_fin(FIN_CIE)) fsm_set_state(CIER); break;
        case ABIE: if(init_accion){io.MA=io.MC=io.BZ=0;io.LAMP=1;blink_ms=0;init_accion=false;actualizar_io();} break;
        case CIER: if(init_accion){io.MA=io.MC=io.BZ=io.LAMP=0;blink_ms=0;init_accion=false;actualizar_io();} break;
        case STOP: if(init_accion){io.MA=io.MC=io.BZ=io.LAMP=0;blink_ms=0;init_accion=false;actualizar_io();} break;
        case EMERG: if(init_accion){io.MA=io.MC=0;io.BZ=1;io.LAMP=1;blink_ms=0;init_accion=false;actualizar_io();} break;
        case ERR: if(init_accion){io.MA=io.MC=0;io.BZ=1;blink_ms=300;lamp_on=false;last_time=now;init_accion=false;actualizar_io();}
                  if(blink_ms>0 && now-last_time>=blink_ms){lamp_on=!lamp_on;io.LAMP=lamp_on;last_time=now;actualizar_io();} break;
        default: break;
    }
}

// MQTT
static void clean_msg(char *s){char *p=s; while(*p==' '||*p=='\n'||*p=='\r'||*p=='\t') p++; if(p!=s) memmove(s,p,strlen(p)+1); for(int i=0;s[i];i++) if(s[i]>='A'&&s[i]<='Z') s[i]+='a'-'A';}
static void mqtt_cb(void *handler_args, esp_event_base_t base,int32_t id,void* d){
    esp_mqtt_event_handle_t e=d; mqtt_client=e->client;
    switch((esp_mqtt_event_id_t)id){
        case MQTT_EVENT_CONNECTED: esp_mqtt_client_subscribe(mqtt_client,MQTT_CHANNEL,0); break;
        case MQTT_EVENT_DATA:{
            char msg[64]={0}; int l=(e->data_len<63)?e->data_len:63; strncpy(msg,e->data,l); msg[l]='\0'; clean_msg(msg);
            if(strcmp(msg,"abrir")==0) fsm_set_state(ABR);
            else if(strcmp(msg,"cerrar")==0) fsm_set_state(CERR);
            else if(strcmp(msg,"stop")==0) fsm_set_state(STOP);
            else if(strcmp(msg,"reset")==0) fsm_set_state(INI);
            else if(strcmp(msg,"emergencia")==0) fsm_set_state(EMERG);
        } break;
        default: break;
    }
}

static void start_mqtt(void){const esp_mqtt_client_config_t cfg={.broker.address.uri=MQTT_SERVER}; mqtt_client=esp_mqtt_client_init(&cfg); esp_mqtt_client_register_event(mqtt_client,ESP_EVENT_ANY_ID,mqtt_cb,NULL); esp_mqtt_client_start(mqtt_client);}

// WiFi
static void wifi_evt(void* a,esp_event_base_t b,int32_t e,void*d){
    if(b==WIFI_EVENT && e==WIFI_EVENT_STA_START) esp_wifi_connect();
    else if(b==WIFI_EVENT && e==WIFI_EVENT_STA_DISCONNECTED){static int r=0; if(r<WIFI_RETRY_MAX){esp_wifi_connect();r++;} else xEventGroupSetBits(wifi_events,WIFI_FAIL);}
    else if(b==IP_EVENT && e==IP_EVENT_STA_GOT_IP) xEventGroupSetBits(wifi_events,WIFI_OK);
}

static void wifi_init(void){
    wifi_events=xEventGroupCreate();
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t any_id,got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&wifi_evt,NULL,&any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,&wifi_evt,NULL,&got_ip));
    wifi_config_t wcfg={.sta={.ssid=WIFI_SSID,.password=WIFI_PASS,.threshold.authmode=WIFI_AUTH_WPA2_PSK}};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    EventBits_t bits=xEventGroupWaitBits(wifi_events,WIFI_OK|WIFI_FAIL,pdFALSE,pdFALSE,portMAX_DELAY);
    if(bits & WIFI_OK) start_mqtt();
}

void app_main(void){
    gpio_setup();
    wifi_init();
    fsm_set_state(STOP);
    const esp_timer_create_args_t args={.callback=(esp_timer_cb_t)fsm_execute,.name="fsm_timer"};
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&args,&timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer,50*1000));
}
