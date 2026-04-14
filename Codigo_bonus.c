#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"

// Pines
#define SEG_A 13
#define SEG_B 12
#define SEG_C 14
#define SEG_D 27
#define SEG_E 26
#define SEG_F 25
#define SEG_G 33

#define DIGIT1 18
#define DIGIT2 19
#define DIGIT3 21

#define LED_VERDE 5
#define LED_ROJO  17

#define BTN_DERECHA   16
#define BTN_IZQUIERDA 4

#define PWM_DERECHA   22
#define PWM_IZQUIERDA 23

#define POT_ADC_CHANNEL ADC_CHANNEL_6


#define PWM_FREQ 5000
#define PWM_MAX  1023
#define DELAY_CAMBIO_MS 500

const int numeros[10][7] = {
    {1,1,1,1,1,1,0},{0,1,1,0,0,0,0},{1,1,0,1,1,0,1},
    {1,1,1,1,0,0,1},{0,1,1,0,0,1,1},{1,0,1,1,0,1,1},
    {1,0,1,1,1,1,1},{1,1,1,0,0,0,0},{1,1,1,1,1,1,1},
    {1,1,1,1,0,1,1}
};

const gpio_num_t segmentos[7] = {
    SEG_A,SEG_B,SEG_C,SEG_D,SEG_E,SEG_F,SEG_G
};

adc_oneshot_unit_handle_t adc1_handle;

// Estado motor
int direccion_actual = 0;
int direccion_solicitada = 0;
bool esperando_cambio = false;
int64_t tiempo_cambio = 0;


// Display
void apagar_display(){
    gpio_set_level(DIGIT1,0);
    gpio_set_level(DIGIT2,0);
    gpio_set_level(DIGIT3,0);
}

void mostrar(int num){
    for(int i=0;i<7;i++){
        gpio_set_level(segmentos[i],numeros[num][i]);
    }
}

void refrescar(int val){
    int c=val/100;
    int d=(val/10)%10;
    int u=val%10;

    apagar_display(); mostrar(c); gpio_set_level(DIGIT1,1);
    vTaskDelay(pdMS_TO_TICKS(2));

    apagar_display(); mostrar(d); gpio_set_level(DIGIT2,1);
    vTaskDelay(pdMS_TO_TICKS(2));

    apagar_display(); mostrar(u); gpio_set_level(DIGIT3,1);
    vTaskDelay(pdMS_TO_TICKS(2));
}

// LEDs
void led_verde(){ gpio_set_level(LED_VERDE,1); gpio_set_level(LED_ROJO,0); }
void led_rojo(){ gpio_set_level(LED_VERDE,0); gpio_set_level(LED_ROJO,1); }

// ADC
void adc_init(){
    adc_oneshot_unit_init_cfg_t cfg={.unit_id=ADC_UNIT_1};
    adc_oneshot_new_unit(&cfg,&adc1_handle);

    adc_oneshot_chan_cfg_t ch={
        .bitwidth=ADC_BITWIDTH_DEFAULT,
        .atten=ADC_ATTEN_DB_12
    };
    adc_oneshot_config_channel(adc1_handle,POT_ADC_CHANNEL,&ch);
}

int leer_adc(){
    int r; adc_oneshot_read(adc1_handle,POT_ADC_CHANNEL,&r);
    return r;
}

// PWM
void pwm_init(){
    ledc_timer_config_t t={
        .speed_mode=LEDC_LOW_SPEED_MODE,
        .timer_num=LEDC_TIMER_0,
        .duty_resolution=LEDC_TIMER_10_BIT,
        .freq_hz=PWM_FREQ
    };
    ledc_timer_config(&t);

    ledc_channel_config_t ch1={
        .gpio_num=PWM_DERECHA,
        .channel=LEDC_CHANNEL_0,
        .speed_mode=LEDC_LOW_SPEED_MODE,
        .timer_sel=LEDC_TIMER_0
    };
    ledc_channel_config(&ch1);

    ledc_channel_config_t ch2={
        .gpio_num=PWM_IZQUIERDA,
        .channel=LEDC_CHANNEL_1,
        .speed_mode=LEDC_LOW_SPEED_MODE,
        .timer_sel=LEDC_TIMER_0
    };
    ledc_channel_config(&ch2);
}

void apagar_motor(){
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);

    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1);
}

void set_pwm(int duty){
    if(direccion_actual==0){
        ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);

        ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1);
    }else{
        ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1);

        ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
    }
}

// MAIN
void app_main(){

    gpio_config_t out={
        .mode=GPIO_MODE_OUTPUT,
        .pin_bit_mask=
        (1ULL<<SEG_A)|(1ULL<<SEG_B)|(1ULL<<SEG_C)|(1ULL<<SEG_D)|
        (1ULL<<SEG_E)|(1ULL<<SEG_F)|(1ULL<<SEG_G)|
        (1ULL<<DIGIT1)|(1ULL<<DIGIT2)|(1ULL<<DIGIT3)|
        (1ULL<<LED_VERDE)|(1ULL<<LED_ROJO)
    };
    gpio_config(&out);

    gpio_config_t in={
        .mode=GPIO_MODE_INPUT,
        .pin_bit_mask=(1ULL<<BTN_DERECHA)|(1ULL<<BTN_IZQUIERDA),
        .pull_up_en=1
    };
    gpio_config(&in);

    adc_init();
    pwm_init();

    led_verde();

    int val=0;
    int last1=1,last2=1;

    while(1){

        refrescar(val);

        int raw=leer_adc();
        val=(raw*100)/4095;
        int duty=(raw*PWM_MAX)/4095;

        int b1=gpio_get_level(BTN_DERECHA);
        int b2=gpio_get_level(BTN_IZQUIERDA);

        if(last1==1 && b1==0){
            direccion_solicitada=0;
            led_verde();
        }

        if(last2==1 && b2==0){
            direccion_solicitada=1;
            led_rojo();
        }

        last1=b1;
        last2=b2;

        int64_t now=esp_timer_get_time()/1000;

        //CAMBIO SEGURO CORREGIDO
        if(!esperando_cambio && direccion_solicitada!=direccion_actual){
            apagar_motor();
            esperando_cambio=true;
            tiempo_cambio=now+DELAY_CAMBIO_MS;
        }

        if(esperando_cambio){
            if(now>=tiempo_cambio){
                direccion_actual=direccion_solicitada;
                esperando_cambio=false;
            } else {
                apagar_motor();
            }
        }

    
        set_pwm(duty);
    }
}