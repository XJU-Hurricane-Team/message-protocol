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
    xTaskCreate(task2, "task2", 128, NULL, 1, &task2_handle);
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
        LED1_TOGGLE();
        vTaskDelay(1000);
    }
}

/**
 * @brief Task2: print running time.
 *
 * @param pvParameters Start parameters.
 */
void task2(void *pvParameters) {
    UNUSED(pvParameters);

    while (1) {
        message_polling_data();
    }
}

msg_status_t msg_send_demo_func(msg_type_t msg_type, size_t msg_len,
                                uint8_t *msg_data) {
    HAL_StatusTypeDef res = HAL_OK;

    switch (msg_type) {
        case MSG_TYPE_DEMO:
            res = HAL_UART_Transmit(&usart2_handle, msg_data, msg_len, 1000);
            message_send_finish_handle(msg_type);
            break;

        default:
            break;
    }

    if (res != HAL_OK) {
        return MSG_ERROR;
    }

    return MSG_OK;
}

/**
 * @brief Task3: Scan the key and print which key pressed.
 *
 * @param pvParameters Start parameters.
 */
void task3(void *pvParameters) {
    UNUSED(pvParameters);
    printf("Welcome use message protocol! \n"
           "Press KEY0 send 0x00 ~ 0xFF (256 byte). \n");

    size_t size = 256;
    uint8_t *test_data = (uint8_t *)pvPortMalloc(sizeof(uint8_t) * size);

    message_register_send_port(MSG_TYPE_DEMO, msg_send_demo_func);
    message_set_send_buf(MSG_TYPE_DEMO, 100);

    key_press_t key = KEY_NO_PRESS;

    while (1) {
        key = key_scan(0);
        switch (key) {
            case KEY0_PRESS: {
                for (uint32_t i = 0; i < size; i++) {
                    test_data[i] = i;
                }
                message_send_data(MSG_TYPE_DEMO, MSG_DATA_UINT8, test_data,
                                  size);
            } break;

            default: {
            } break;
        }

        vTaskDelay(10);
    }
}
