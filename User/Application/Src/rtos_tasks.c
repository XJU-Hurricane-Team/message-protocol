/**
 * @file    rtos_tasks.c
 * @author  Deadline039
 * @brief   RTOS tasks.
 * @version 1.0
 * @date    2024-01-31
 */

#include "includes.h"

static TaskHandle_t start_task_handle;
void start_task(void *pvParameters);

static TaskHandle_t task1_handle;
void task1(void *pvParameters);

static TaskHandle_t task2_handle;
void task2(void *pvParameters);

static TaskHandle_t task3_handle;
void task3(void *pvParameters);

/*****************************************************************************/

/**
 * @brief FreeRTOS start up.
 *
 */
void freertos_start(void) {
    xTaskCreate(start_task, "start_task", 128, NULL, 2, &start_task_handle);
    vTaskStartScheduler();
}

/**
 * @brief Start up task.
 *
 * @param pvParameters Start parameters.
 */
void start_task(void *pvParameters) {
    UNUSED(pvParameters);
    taskENTER_CRITICAL();

    xTaskCreate(task1, "task1", 128, NULL, 2, &task1_handle);
    xTaskCreate(task2, "task2", 128, NULL, 2, &task2_handle);
    xTaskCreate(task3, "task3", 128, NULL, 2, &task3_handle);

    vTaskDelete(start_task_handle);
    taskEXIT_CRITICAL();
}

/**
 * @brief Task1: Blink.
 *
 * @param pvParameters Start parameters.
 */
void task1(void *pvParameters) {
    UNUSED(pvParameters);

    LED0_OFF();
    LED1_ON();

    while (1) {
        LED0_TOGGLE();
        vTaskDelay(1000);
    }
}

/**
 * @brief 回调函数 demo
 *
 * @param msg_length 消息帧长度
 * @param msg_id_type 消息 ID 和数据类型 (高四位为 ID, 低四位为数据类型)
 * @param[in] msg_data 消息数据接收区
 */
static void msg_callback_demo(uint32_t msg_length, uint8_t msg_id_type,
                              uint8_t *msg_data) {
    static uint32_t count = 0;
    UNUSED(msg_data);
    printf("length: %d, type: %x times: %d \n", msg_length, msg_id_type,
           ++count);
}

/**
 * @brief Task2: 消息轮询
 *
 * @param pvParameters Start parameters.
 */
void task2(void *pvParameters) {
    UNUSED(pvParameters);

    message_register_polling_uart(MSG_ID_DEMO, &usart3_handle, 200);
    message_register_recv_callback(MSG_ID_DEMO, msg_callback_demo);

    while (1) {
        message_polling_data();
    }
}

/**
 * @brief Task3: 按键发送消息
 *
 * @param pvParameters Start parameters.
 */
void task3(void *pvParameters) {
    UNUSED(pvParameters);
    printf("Welcome use message protocol! \n"
           "Press KEY0 send 0x00 ~ 0xFF (256 byte). \n");

    size_t size = 100;
    uint8_t *test_data = (uint8_t *)pvPortMalloc(sizeof(uint8_t) * size);

    message_register_send_uart(MSG_ID_DEMO, &uart4_handle, size);

    key_press_t key = KEY_NO_PRESS;

    while (1) {
        key = key_scan(0);
        switch (key) {
            case KEY0_PRESS: {
                for (uint32_t i = 0; i < size; i++) {
                    test_data[i] = i;
                }

                message_send_data(MSG_ID_DEMO, MSG_DATA_UINT8, test_data, size);

            } break;

            default: {
            } break;
        }

        vTaskDelay(10);
    }
}
