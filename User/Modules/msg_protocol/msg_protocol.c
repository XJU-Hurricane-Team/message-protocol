/**
 * @file    msg_protocol.c
 * @author  Deadline039
 * @brief   消息协议以及收发
 * @version 1.0
 * @date    2024-03-01
 * @note    接收暂只支持DMA, 如果使用中断自行到`uart.h`编写相应代码
 */

#include "msg_protocol.h"

#include <string.h>
#include <stdbool.h>

#if MSG_ENABLE_RTOS
#include "FreeRTOS.h"
#include "semphr.h"
#endif /* MSG_ENABLE_RTOS */

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wzero-length-array"
#endif /* __GNUC__ */

/**
 * @brief 消息包 FIFO 节点
 */
typedef struct __fifo_node {
    uint32_t len;             /*!< 当前节点数据量大小 */
    struct __fifo_node *next; /*!< 下一个 FIFO 节点 */
#ifdef __GNUC__
    uint8_t data[0]; /*!< 数据缓冲区 */
#else                /* __GNUC__ */
    uint8_t data[]; /*!< 数据缓冲区 */
#endif               /* __GNUC__ */
} fifo_node_t;

/**
 * @brief 消息实例
 */
static struct msg_instance {
    msg_recv_callback_t recv_callback; /*!< 接收回调函数 */
    UART_HandleTypeDef *send_uart;     /*!< 发送串口句柄 */
    UART_HandleTypeDef *recv_uart;     /*!< 接收串口句柄 */

    uint8_t *send_buf;     /*!< 发送缓冲区 */
    uint32_t send_buf_len; /*!< 发送缓冲区大小 */

#if MSG_ENABLE_RTOS
    SemaphoreHandle_t send_buf_semp; /*!< 发送缓冲区二值型号量 */
#endif                               /* MSG_ENABLE_RTOS */

    uint8_t *recv_buf;      /*!< 接收缓冲区 */
    uint32_t recv_buf_size; /*!< 接收缓冲区大小 */

    fifo_node_t *head; /*!< 接收队列头指针 */
    fifo_node_t *tail; /*!< 接收队列尾指针 */

#ifdef MSG_ESC
    bool escape; /*!< 是否要将下一个字符转义 */
#endif           /* MSG_ESC */

#if MSG_ENABLE_STATISTICS
    uint32_t recv_success; /*!< 接收成功计数 */
    uint32_t recv_error;   /*!< 接收错误计数 */

    uint32_t fifo_len;     /*!< 当前队列长度 */
    uint32_t max_fifo_len; /*!< 最大队列长度 */

    uint32_t mem_fail; /*!< 内存分配失败计数 */
#endif                 /* MSG_ENABLE_STATISTICS */
} msg_list[MSG_ID_RESERVE_LEN];

/**
 * @brief 注册接收回调函数指针
 *
 * @param msg_id 数据含义
 * @param msg_callback 回调指针
 * @note 如果更换回调函数, 重新调用该函数即可
 */
void message_register_recv_callback(msg_id_t msg_id,
                                    msg_recv_callback_t msg_callback) {
    if (msg_id >= MSG_ID_RESERVE_LEN) {
        return;
    }

    msg_list[msg_id].recv_callback = msg_callback;
}

/**
 * @brief 注册数据发送句柄
 *
 * @param msg_id 数据含义
 * @param huart 发送串口句柄
 * @param buf_size 缓冲区大小
 */
void message_register_send_uart(msg_id_t msg_id, UART_HandleTypeDef *huart,
                                uint32_t buf_size) {
    if (msg_id >= MSG_ID_RESERVE_LEN) {
        return;
    }

    struct msg_instance *msg = &msg_list[msg_id];

    msg->send_uart = huart;
    if (msg->send_buf != NULL) {
        MSG_FREE(msg->send_buf);
    }

    msg->send_buf = (uint8_t *)MSG_MALLOC(buf_size);
    if (msg->send_buf == NULL) {
#if MSG_ENABLE_STATISTICS
        ++msg->mem_fail;
#endif /* MSG_ENABLE_STATISTICS */
        return;
    }

    msg->send_buf_len = buf_size;
#if MSG_ENABLE_RTOS
    if (msg->send_buf_semp == NULL) {
        msg->send_buf_semp = xSemaphoreCreateMutex();
    }
#endif /* MSG_ENABLE_RTOS */
}

/**
 * @brief 注册数据接口串口句柄
 *
 * @param msg_id 数据含义
 * @param huart 接收串口句柄
 * @param buf_size 缓冲区大小
 */
void message_register_polling_uart(msg_id_t msg_id, UART_HandleTypeDef *huart,
                                   uint32_t buf_size) {
    if (msg_id >= MSG_ID_RESERVE_LEN) {
        return;
    }

    struct msg_instance *msg = &msg_list[msg_id];
    msg->recv_uart = huart;
    msg->recv_buf = (uint8_t *)MSG_MALLOC(buf_size);
    if (msg->recv_buf == NULL) {
#if MSG_ENABLE_STATISTICS
        ++msg->mem_fail;
#endif /* MSG_ENABLE_STATISTICS */
        return;
    }
    msg->recv_buf_size = buf_size;
}

/**
 * @brief 填充并发送数据, 支持多种类型
 *
 * @param msg_id 数据含义
 * @param data_type 数据类型
 * @param data 数据内容
 * @param data_len 发送长度
 */
void message_send_data(msg_id_t msg_id, msg_type_t data_type, uint8_t *data,
                       uint32_t data_len) {
    if (data == NULL || data_len == 0) {
        return;
    }

    if (msg_id >= MSG_ID_RESERVE_LEN) {
        return;
    }

    struct msg_instance *msg = &msg_list[msg_id];

    if (msg->send_uart == NULL || msg->send_buf == NULL) {
        return;
    }

#if MSG_ENABLE_RTOS
    xSemaphoreTake(msg->send_buf_semp, portMAX_DELAY);
#endif /* MSG_ENABLE_RTOS */

    if (msg->send_buf_len <= data_len + 3) {
        /* 不够, 扩容到原来的 2 倍 */
        uint8_t *new_buf = (uint8_t *)MSG_REALLOC(msg->send_buf, data_len * 2);
        if (new_buf == NULL) {
#if MSG_ENABLE_STATISTICS
            ++msg->mem_fail;
#endif /* MSG_ENABLE_STATISTICS */
            return;
        }

        msg->send_buf = new_buf;
        msg->send_buf_len = data_len * 2;
    } else if ((data_len + 3) * 3 <= msg->send_buf_len) {
        /* 长度小于 1/3, 缩容 1/2 */
        uint8_t *new_buf = (uint8_t *)MSG_REALLOC(msg->send_buf, data_len / 2);
        if (new_buf == NULL) {
#if MSG_ENABLE_STATISTICS
            ++msg->mem_fail;
#endif /* MSG_ENABLE_STATISTICS */
            return;
        }

        msg->send_buf = new_buf;
        msg->send_buf_len = data_len / 2;
    }

    uint8_t *send_buf = (uint8_t *)msg->send_buf;
    uint32_t buf_idx = 0;

    /* 第一个字节, 高四位标记 ID, 低四位标记数据类型 */
    send_buf[buf_idx] = (uint8_t)(msg_id << 4) | data_type;
    ++buf_idx;
    /* 第二个字节, 标记数据长度 */
    send_buf[buf_idx] = (uint8_t)data_len;
    ++buf_idx;

    /* 复制数据到字节流 */
    for (uint32_t data_idx = 0; data_idx < data_len; ++data_idx) {
#ifdef MSG_ESC
        if ((data[data_idx] == MSG_EOF) || (data[data_idx] == MSG_ESC)) {
            /* 转义 */
            send_buf[buf_idx] = MSG_ESC;
            ++buf_idx;
        }
#endif /* MSG_ESC */

        send_buf[buf_idx] = data[data_idx];
        ++buf_idx;
    }

    /* 最后一个字节, 标记数据末尾 */
    send_buf[buf_idx] = MSG_EOF;
    ++buf_idx;

    if (msg->send_uart->hdmatx != NULL) {
        uart_dmatx_write(msg->send_uart, send_buf, buf_idx);
        uart_dmatx_send(msg->send_uart);
    } else {
        HAL_UART_Transmit(msg->send_uart, send_buf, buf_idx, 0xFFFF);
    }

#if MSG_ENABLE_RTOS
    xSemaphoreGive(msg->send_buf_semp);
#endif /* MSG_ENABLE_RTOS */
}

static void message_data_enqueue(struct msg_instance *msg, uint32_t recv_len);
static void message_data_dequeue(struct msg_instance *msg);

/**
 * @brief 轮询数据, 并调用相应的函数
 *
 */
void message_polling_data(void) {
    struct msg_instance *msg;
    uint32_t recv_len;
    for (msg_id_t i = 0; i < MSG_ID_RESERVE_LEN; ++i) {
        msg = &msg_list[i];
        if (msg->recv_uart == NULL) {
            continue;
        }

        if (msg->recv_callback == NULL) {
            /* 避免串口 fifo 溢出, 当没有回调函数的时候也要读一次 */
            continue;
        }

        message_data_dequeue(msg);

        recv_len =
            uart_dmarx_read(msg->recv_uart, msg->recv_buf, msg->recv_buf_size);
        if (recv_len == 0) {
            continue;
        }

        message_data_enqueue(msg, recv_len);
    }
}

/**
 * @brief 消息数据入队
 * 
 * @param msg 消息实例
 * @param recv_len 接收到的数据长度
 */
static void message_data_enqueue(struct msg_instance *msg, uint32_t recv_len) {
    if (msg->tail == NULL) {
        msg->tail =
            (fifo_node_t *)MSG_MALLOC(sizeof(fifo_node_t) + msg->recv_buf_size);
        if (msg->tail == NULL) {
#if MSG_ENABLE_STATISTICS
            ++msg->mem_fail;
#endif /* MSG_ENABLE_STATISTICS */
            return;
        }
        msg->head = msg->tail;
#if MSG_ENABLE_STATISTICS
        ++msg->fifo_len;
#endif /* MSG_ENABLE_STATISTICS */
    }

    fifo_node_t *new_node = NULL;
    fifo_node_t *tail = msg->tail;

    for (uint32_t i = 0; i < recv_len; ++i) {
#ifdef MSG_ESC
        if ((msg->recv_buf[i] == MSG_ESC) && (msg->escape == false)) {
            /* 遇到转义, 跳过这一字节到下一字节 */
            msg->escape = true;
            continue;
        }
#endif /* MSG_ESC */

        tail->data[tail->len] = msg->recv_buf[i];
        ++tail->len;

        if (tail->len >= msg->recv_buf_size) {
            /* 数据内容超出缓冲区大小, 长度 - 1, 末尾强制为结束标识 */
            --tail->len;
            tail->data[tail->len] = MSG_EOF;
        }

#ifdef MSG_ESC
        if (msg->escape) {
            msg->escape = false;
            /* 这是被转移的字符, 避免执行到下面的结束标识被当成结束符处理 */
            continue;
        }
#endif /* MSG_ESC */

        if (msg->recv_buf[i] == MSG_EOF) {
            new_node = (fifo_node_t *)MSG_MALLOC(sizeof(fifo_node_t) +
                                                 msg->recv_buf_size);
            if (new_node == NULL) {
#if MSG_ENABLE_STATISTICS
                ++msg->mem_fail;
#endif /* MSG_ENABLE_STATISTICS */
                return;
            }

            new_node->len = 0;
            new_node->next = NULL;
            /* 尾指针后移 */
            tail->next = new_node;
            msg->tail = new_node;
            tail = msg->tail;

#if MSG_ENABLE_STATISTICS
            ++msg->fifo_len;
#endif /* MSG_ENABLE_STATISTICS */
        }

#if MSG_ENABLE_STATISTICS
        if (msg->fifo_len > msg->max_fifo_len) {
            msg->max_fifo_len = msg->fifo_len;
        }
#endif /* MSG_ENABLE_STATISTICS */
    }
}

/**
 * @brief 消息数据出队并调用回调函数
 * 
 * @param msg 消息实例
 */
static void message_data_dequeue(struct msg_instance *msg) {
    fifo_node_t *head = msg->head;

    if (head == NULL) {
        return;
    }

    if ((head->len == 0) || (head->data[head->len - 1] != MSG_EOF)) {
        /* 当前 FIFO 节点数据不完整 */
        return;
    }

    fifo_node_t *current_head = head;

    if ((current_head->len - 3) == current_head->data[1]) {
        /* 调用回调函数后释放头指针 */
        if (msg->recv_callback != NULL) {
            /* 数据内容去掉头标识, 长度和结束标识, 所以长度减去 3 字节 */
            msg->recv_callback(current_head->len - 3, current_head->data[0],
                               &current_head->data[2]);
        }
#if MSG_ENABLE_STATISTICS
        ++msg->recv_success;
#endif /* MSG_ENABLE_STATISTICS */

    } else {
/* 实际接收到的长度与数据包里的长度不一致 */
#if MSG_ENABLE_STATISTICS
        ++msg->recv_error;
#endif /* MSG_ENABLE_STATISTICS */
    }

    /* 头指针后移, 释放旧的头指针 */
    msg->head = msg->head->next;
    if (msg->head == NULL) {
        msg->tail = NULL;
    }

    MSG_FREE(current_head);

#if MSG_ENABLE_STATISTICS
    --msg->fifo_len;
#endif /* MSG_ENABLE_STATISTICS */
}