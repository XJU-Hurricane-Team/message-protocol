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

static TaskHandle_t msg_polling_task_handle;
void msg_polling_task(void *pvParameters);

static TaskHandle_t send_demo_task_handle;
void send_demo_task(void *pvParameters);

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

    xTaskCreate(task1, "task1", 64, NULL, 2, &task1_handle);
    xTaskCreate(msg_polling_task, "msg_polling_task", 256, NULL, 3,
                &msg_polling_task_handle);
    xTaskCreate(send_demo_task, "send_demo_task", 256, NULL, 3,
                &send_demo_task_handle);

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

    while (1) {
        LED0_TOGGLE();
        vTaskDelay(500);
    }
}

/**
 * @brief 消息轮询
 *
 * @param pvParameters Start parameters.
 */
void msg_polling_task(void *pvParameters) {
    UNUSED(pvParameters);

    while (1) {
        message_polling_data();
        vTaskDelay(1);
    }
}

/**
 * @brief 每个 ID 的消息接收情况统计
 */
static struct {
    uint32_t check_pass;  /*!< 校验通过计数 */
    uint32_t check_fail;  /*!< 校验失败计数 */
    uint32_t id_type_err; /*!< ID 与数据类型错误 */
    uint32_t length_err;  /*!< 长度错误计数 */
} msg_id_statistics[MSG_ID_RESERVE_LEN];

static void msg_callback_id1(uint32_t msg_length, uint8_t msg_id_type,
                             uint8_t *msg_data);
static void msg_callback_id2(uint32_t msg_length, uint8_t msg_id_type,
                             uint8_t *msg_data);
static void msg_callback_id3(uint32_t msg_length, uint8_t msg_id_type,
                             uint8_t *msg_data);
static void msg_callback_id4(uint32_t msg_length, uint8_t msg_id_type,
                             uint8_t *msg_data);

/**
 * @brief 按键发送消息
 *
 * @param pvParameters Start parameters.
 */
void send_demo_task(void *pvParameters) {
    UNUSED(pvParameters);
    printf("Welcome use message protocol! \n"
           "Press KEY0 to start. \n");

    size_t size = 200;
    uint8_t *test_data = (uint8_t *)pvPortMalloc(sizeof(uint8_t) * size);

    /** 测试条件: 
     * ID1: 长度 20 byte, 发送频率 500 Hz.  TX2 -> RX3
     * ID2: 长度 50 byte, 发送频率 250 Hz.  TX3 -> RX4
     * ID3: 长度 100 byte, 发送频率 200 Hz. TX4 -> RX5
     * ID4: 长度 200 byte, 发送频率 100 Hz. TX5 -> RX2
     * 测试时间: 5 分钟, 结束后打印测试结果
     * 数据内容: 0-255 每个字节依次递增, 下一次发送时数据 + 1
     * 回调内容: 对数据区内容校验, 并统计结果. 
     */

    /* 挂起所有任务, 避免轮询导致错误 */
    vTaskSuspendAll();

    message_register_send_uart(MSG_ID_1, &usart2_handle, 30);
    message_register_polling_uart(MSG_ID_1, &usart3_handle, 200, 256);
    message_register_recv_callback(MSG_ID_1, msg_callback_id1);

    message_register_send_uart(MSG_ID_2, &usart3_handle, 60);
    message_register_polling_uart(MSG_ID_2, &uart4_handle, 200, 256);
    message_register_recv_callback(MSG_ID_2, msg_callback_id2);

    message_register_send_uart(MSG_ID_3, &uart4_handle, 110);
    message_register_polling_uart(MSG_ID_3, &uart5_handle, 200, 256);
    message_register_recv_callback(MSG_ID_3, msg_callback_id3);

    message_register_send_uart(MSG_ID_4, &uart5_handle, 220);
    message_register_polling_uart(MSG_ID_4, &usart2_handle, 240, 256);
    message_register_recv_callback(MSG_ID_4, msg_callback_id4);

    xTaskResumeAll();

    /* 等待 key1 按下 */
    while (key_scan(0) != KEY0_PRESS) {
        vTaskDelay(1);
    }

    printf("Start Sending! \n");

    LED1_ON();

    uint8_t times = 0;
    uint32_t start_time = HAL_GetTick();

    uint8_t count1 = 0;
    uint8_t count2 = 0;
    uint8_t count3 = 0;
    uint8_t count4 = 0;

    uint32_t index = 0;

    while (1) {
        if (HAL_GetTick() - start_time > 5 * 60 * 1000) {
            /* 5 分钟到 */
            break;
        }

        if (times % 2 == 0) {
            ++count1;

            for (index = 0; index < 20; ++index) {
                test_data[index] = count1 + index;
            }

            message_send_data(MSG_ID_1, MSG_DATA_UINT8, test_data, 20);
        }

        if (times % 4 == 0) {
            ++count2;

            for (index = 0; index < 50; ++index) {
                test_data[index] = count2 + index;
            }
            message_send_data(MSG_ID_2, MSG_DATA_UINT8, test_data, 50);
        }

        if (times % 5 == 0) {
            ++count3;

            for (index = 0; index < 100; ++index) {
                test_data[index] = count3 + index;
            }
            message_send_data(MSG_ID_3, MSG_DATA_UINT8, test_data, 100);
        }

        if (times % 10 == 0) {
            ++count4;

            for (index = 0; index < 200; ++index) {
                test_data[index] = count4 + index;
            }
            message_send_data(MSG_ID_4, MSG_DATA_UINT8, test_data, 200);
        }

        ++times;
        vTaskDelay(1);
    }

    /* 延时 1s, 等待 message_polling 处理剩余最后一点数据 */
    vTaskDelay(1000);

    vPortFree(test_data);

    printf("id: 1, check_pass: %u, check_fail: %u, len_err: %u, id_type_err: "
           "%u. \n",
           msg_id_statistics[MSG_ID_1].check_pass,
           msg_id_statistics[MSG_ID_1].check_fail,
           msg_id_statistics[MSG_ID_1].length_err,
           msg_id_statistics[MSG_ID_1].id_type_err);

    printf("id: 2, check_pass: %u, check_fail: %u, len_err: %u, id_type_err: "
           "%u. \n",
           msg_id_statistics[MSG_ID_2].check_pass,
           msg_id_statistics[MSG_ID_2].check_fail,
           msg_id_statistics[MSG_ID_2].length_err,
           msg_id_statistics[MSG_ID_2].id_type_err);

    printf("id: 3, check_pass: %u, check_fail: %u, len_err: %u, id_type_err: "
           "%u. \n",
           msg_id_statistics[MSG_ID_3].check_pass,
           msg_id_statistics[MSG_ID_3].check_fail,
           msg_id_statistics[MSG_ID_3].length_err,
           msg_id_statistics[MSG_ID_3].id_type_err);

    printf("id: 4, check_pass: %u, check_fail: %u, len_err: %u, id_type_err: "
           "%u. \n",
           msg_id_statistics[MSG_ID_4].check_pass,
           msg_id_statistics[MSG_ID_4].check_fail,
           msg_id_statistics[MSG_ID_4].length_err,
           msg_id_statistics[MSG_ID_4].id_type_err);
    LED1_OFF();

    /* 更多数据下断点在 msg_protocol 中查看`msg_list`中的统计 */
    vTaskDelay(portMAX_DELAY);
}

/**
 * @brief ID1 回调函数
 *
 * @param msg_length 消息帧长度
 * @param msg_id_type 消息 ID 和数据类型 (高四位为 ID, 低四位为数据类型)
 * @param[in] msg_data 消息数据接收区
 */
static void msg_callback_id1(uint32_t msg_length, uint8_t msg_id_type,
                             uint8_t *msg_data) {
    /* 回调计数, 用于校验数据内容 */
    static uint8_t count;

    ++count;
    if (msg_length != 20) {
        /* ID1 长度为 20 */
        ++msg_id_statistics[MSG_ID_1].length_err;
        return;
    }

    if (msg_id_type != ((MSG_ID_1 << 4) | MSG_DATA_UINT8)) {
        /* 类型错误 */
        ++msg_id_statistics[MSG_ID_1].id_type_err;
        return;
    }

    for (uint32_t i = 0; i < msg_length; ++i) {
        /* 校验数据区内容 */
        if ((uint8_t)(i + count) != msg_data[i]) {
            /* 数据区与发送不一致, 返回 */
            ++msg_id_statistics[MSG_ID_1].check_fail;
            return;
        }
    }

    /* 校验通过 */
    ++msg_id_statistics[MSG_ID_1].check_pass;
}

/**
 * @brief ID2 回调函数
 *
 * @param msg_length 消息帧长度
 * @param msg_id_type 消息 ID 和数据类型 (高四位为 ID, 低四位为数据类型)
 * @param[in] msg_data 消息数据接收区
 */
static void msg_callback_id2(uint32_t msg_length, uint8_t msg_id_type,
                             uint8_t *msg_data) {
    /* 回调计数, 用于校验数据内容 */
    static uint8_t count;

    ++count;
    if (msg_length != 50) {
        /* ID2 长度为 20 */
        ++msg_id_statistics[MSG_ID_2].length_err;
        return;
    }

    if (msg_id_type != ((MSG_ID_2 << 4) | MSG_DATA_UINT8)) {
        /* 类型错误 */
        ++msg_id_statistics[MSG_ID_2].id_type_err;
        return;
    }

    for (uint32_t i = 0; i < msg_length; ++i) {
        /* 校验数据区内容 */
        if ((uint8_t)(i + count) != msg_data[i]) {
            /* 数据区与发送不一致, 返回 */
            ++msg_id_statistics[MSG_ID_2].check_fail;
            return;
        }
    }

    /* 校验通过 */
    ++msg_id_statistics[MSG_ID_2].check_pass;
}

/**
 * @brief ID3 回调函数
 *
 * @param msg_length 消息帧长度
 * @param msg_id_type 消息 ID 和数据类型 (高四位为 ID, 低四位为数据类型)
 * @param[in] msg_data 消息数据接收区
 */
static void msg_callback_id3(uint32_t msg_length, uint8_t msg_id_type,
                             uint8_t *msg_data) {
    /* 回调计数, 用于校验数据内容 */
    static uint8_t count;

    ++count;
    if (msg_length != 100) {
        /* ID3 长度为 20 */
        ++msg_id_statistics[MSG_ID_3].length_err;
        return;
    }

    if (msg_id_type != ((MSG_ID_3 << 4) | MSG_DATA_UINT8)) {
        /* 类型错误 */
        ++msg_id_statistics[MSG_ID_3].id_type_err;
        return;
    }

    for (uint32_t i = 0; i < msg_length; ++i) {
        /* 校验数据区内容 */
        if ((uint8_t)(i + count) != msg_data[i]) {
            /* 数据区与发送不一致, 返回 */
            ++msg_id_statistics[MSG_ID_3].check_fail;
            return;
        }
    }

    /* 校验通过 */
    ++msg_id_statistics[MSG_ID_3].check_pass;
}

/**
 * @brief ID4 回调函数
 *
 * @param msg_length 消息帧长度
 * @param msg_id_type 消息 ID 和数据类型 (高四位为 ID, 低四位为数据类型)
 * @param[in] msg_data 消息数据接收区
 */
static void msg_callback_id4(uint32_t msg_length, uint8_t msg_id_type,
                             uint8_t *msg_data) {
    /* 回调计数, 用于校验数据内容 */
    static uint8_t count;

    ++count;
    if (msg_length != 200) {
        /* ID4 长度为 20 */
        ++msg_id_statistics[MSG_ID_4].length_err;
        return;
    }

    if (msg_id_type != ((MSG_ID_4 << 4) | MSG_DATA_UINT8)) {
        /* 类型错误 */
        ++msg_id_statistics[MSG_ID_4].id_type_err;
        return;
    }

    for (uint32_t i = 0; i < msg_length; ++i) {
        /* 校验数据区内容 */
        if ((uint8_t)(i + count) != msg_data[i]) {
            /* 数据区与发送不一致, 返回 */
            ++msg_id_statistics[MSG_ID_4].check_fail;
            return;
        }
    }

    /* 校验通过 */
    ++msg_id_statistics[MSG_ID_4].check_pass;
}

#ifdef configASSERT
/**
 * @brief FreeRTOS assert failed function. 
 * 
 * @param pcFile File name
 * @param ulLine File line
 */
void vAssertCalled(const char *pcFile, unsigned int ulLine) {
    fprintf(stderr, "FreeRTOS assert failed. File: %s, line: %u. \n", pcFile,
            ulLine);
}
#endif /* configASSERT */

#if configCHECK_FOR_STACK_OVERFLOW
/**
 * @brief The application stack overflow hook is called when a stack overflow is detected for a task.
 *
 * @param xTask the task that just exceeded its stack boundaries.
 * @param pcTaskName A character string containing the name of the offending task.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    UNUSED(xTask);
    fprintf(stderr, "Stack overflow! Taskname: %s. \n", pcTaskName);
}
#endif /* configCHECK_FOR_STACK_OVERFLOW */

#if configUSE_MALLOC_FAILED_HOOK
/**
 * @brief This hook function is called when allocation failed.
 * 
 */
void vApplicationMallocFailedHook(void) {
    fprintf(stderr, "FreeRTOS malloc failed! \n");
}
#endif /* configUSE_MALLOC_FAILED_HOOK */
