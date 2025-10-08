#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <stdio.h>
#include "pico/stdlib.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "pins.h"
#include "ssd1306.h"

// === Definições para SSD1306 ===
ssd1306_t disp;

QueueHandle_t xQueueBtn;
QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;
SemaphoreHandle_t xSemaphoreTrigger;

const int ECHO = 16;
const int TRIGGER = 17;

// == funcoes de inicializacao ===
void btn_callback(uint gpio, uint32_t events) {
    if (events & GPIO_IRQ_EDGE_FALL) xQueueSendFromISR(xQueueBtn, &gpio, 0);
}

void pin_callback(uint gpio, uint32_t events){
    uint32_t time = to_us_since_boot(get_absolute_time());
    xQueueSendFromISR(xQueueTime, &time, 0);
}


void oled_display_init(void) {
    i2c_init(i2c1, 400000);
    gpio_set_function(2, GPIO_FUNC_I2C);
    gpio_set_function(3, GPIO_FUNC_I2C);
    gpio_pull_up(2);
    gpio_pull_up(3);

    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
    ssd1306_clear(&disp);
    ssd1306_show(&disp);
}

void btns_init(void) {
    gpio_init(BTN_PIN_R);
    gpio_set_dir(BTN_PIN_R, GPIO_IN);
    gpio_pull_up(BTN_PIN_R);

    gpio_init(BTN_PIN_G);
    gpio_set_dir(BTN_PIN_G, GPIO_IN);
    gpio_pull_up(BTN_PIN_G);

    gpio_init(BTN_PIN_B);
    gpio_set_dir(BTN_PIN_B, GPIO_IN);
    gpio_pull_up(BTN_PIN_B);

    gpio_init(ECHO); gpio_set_dir(ECHO, GPIO_IN); gpio_disable_pulls(ECHO);
    gpio_init(TRIGGER); gpio_set_dir(TRIGGER, GPIO_OUT);

    gpio_set_irq_enabled_with_callback(ECHO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &pin_callback);

}

void led_rgb_init(void) {
    gpio_init(LED_PIN_R);
    gpio_set_dir(LED_PIN_R, GPIO_OUT);
    gpio_put(LED_PIN_R, 1);

    gpio_init(LED_PIN_G);
    gpio_set_dir(LED_PIN_G, GPIO_OUT);
    gpio_put(LED_PIN_G, 1);

    gpio_init(LED_PIN_B);
    gpio_set_dir(LED_PIN_B, GPIO_OUT);
    gpio_put(LED_PIN_B, 1);
}

void trigger_task(void *p){
    while (1){
        gpio_put(TRIGGER, 1);
        sleep_us(10);
        gpio_put(TRIGGER, 0);
        xSemaphoreGive(xSemaphoreTrigger);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void echo_task(void *p){
    uint32_t t_inicial = 0;
    uint32_t t_final = 0;
    btns_init();
    while(1){
        if (xQueueReceive(xQueueTime, &t_inicial,  pdMS_TO_TICKS(100))){
            if (xQueueReceive(xQueueTime, &t_final,  pdMS_TO_TICKS(100))){
                float distance  = (t_final - t_inicial)/58.0f;
                xQueueSend(xQueueDistance, &distance, 0);
            }
        }
    }

}

void oled_task(void *p){
    led_rgb_init();
    oled_display_init();
    btns_init();


    float distance;
    char buffer[32];

    while(1){
        if(xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(100))) {
            if (xQueueReceive(xQueueDistance, &distance,  pdMS_TO_TICKS(100))) {
                if ((int)distance > 100) {
                    gpio_put(LED_PIN_R, 0); gpio_put(LED_PIN_G, 0); gpio_put(LED_PIN_B, 1);
                }
                else{
                    gpio_put(LED_PIN_G, 0); gpio_put(LED_PIN_B, 1); gpio_put(LED_PIN_R, 1);
                }
            } else {
                gpio_put(LED_PIN_R, 0); gpio_put(LED_PIN_G, 1); gpio_put(LED_PIN_B, 1);
                sprintf(buffer, "%s", "ERRO");
                ssd1306_clear(&disp);
                ssd1306_draw_string(&disp, 8, 0, 2, buffer);
                ssd1306_show(&disp);
                continue;
            }

            sprintf(buffer, "%d", (int)distance);

            ssd1306_clear(&disp);
            ssd1306_draw_string(&disp, 8, 0, 2, buffer);
            ssd1306_draw_square(&disp, 0, 32, (int)distance/4, 16);
            ssd1306_show(&disp);
        }
    }
}


void task_1(void *p) {
    btns_init();
    led_rgb_init();
    oled_display_init();

    uint btn_data;

    while (1) {
        if (xQueueReceive(xQueueBtn, &btn_data, pdMS_TO_TICKS(2000))) {
            printf("btn: %d \n", btn_data);

            switch (btn_data) {
                case BTN_PIN_B:
                    gpio_put(LED_PIN_B, 0);
                    ssd1306_draw_string(&disp, 8, 0, 2, "BLUE");
                    ssd1306_show(&disp);
                    break;
                case BTN_PIN_G:
                    gpio_put(LED_PIN_G, 0);
                    ssd1306_draw_string(&disp, 8, 24, 2, "GREEN");
                    ssd1306_show(&disp);
                    break;
                case BTN_PIN_R:
                    gpio_put(LED_PIN_R, 0);

                    ssd1306_draw_string(&disp, 8, 48, 2, "RED");
                    ssd1306_show(&disp);
                    break;
                default:
                    // Handle other buttons if needed
                    break;
            }
        } else {
            ssd1306_clear(&disp);
            ssd1306_show(&disp);
            gpio_put(LED_PIN_R, 1);
            gpio_put(LED_PIN_G, 1);
            gpio_put(LED_PIN_B, 1);
        }
    }
}


int main() {
    stdio_init_all();

    xQueueBtn = xQueueCreate(32, sizeof(uint));
    xQueueTime = xQueueCreate(32, sizeof(uint));
    xQueueDistance = xQueueCreate(32, sizeof(float));
    
    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(oled_task, "Task 1", 8192, NULL, 1, NULL);
    xTaskCreate(trigger_task, "Trigger Task", 8192, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo task", 8192, NULL, 1, NULL);


    vTaskStartScheduler();

    while (true);
}
