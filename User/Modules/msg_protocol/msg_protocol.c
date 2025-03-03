/**
 * @file    msg_protocol.c
 * @author  Deadline039
 * @brief   消息协议以及收发
 * @version 2.0
 * @date    2024-03-01
 */

#include "msg_protocol.h"

#include <stdbool.h>

#if MSG_ENABLE_RTOS
#include "FreeRTOS.h"
#include "semphr.h"
#endif /* MSG_ENABLE_RTOS */

#if MSG_ENABLE_CRC
#include "bsp_crc/bsp_crc32.h"
#endif /* MSG_ENABLE_CRC */

/**
 * @brief FIFO 节点
 */
typedef struct __fifo_node {
    uint8_t *data;            /*!< 数据区域内容 */
    uint32_t len;             /*!< 数据长度 */
    bool complete;            /*!< 处理完毕, 是完整的数据包 */
    struct __fifo_node *next; /*!< 下一个内容 */
    struct __fifo_node *prev; /*!< 上一个内容 */
} fifo_node_t;

/**
 * @brief 帧处理类型
 */
typedef enum {
    HEAD_FRAME, /*!< 处理头帧 */
    TAIL_FRAME, /*!< 处理尾帧 */
    NO_FRAME,   /*!< 保留帧 */
} deal_type_t;

/**
 * @brief CRC处理联合体
 */
typedef union {
    uint8_t array[sizeof(MSG_CRC_TYPE)];
    MSG_CRC_TYPE result;
} send_crc_t;

/**
 * @brief 消息对象
 */
static struct msg_struct {
    msg_send_port_t send;         /*!< 发送接口 */
    msg_recv_port_t recv;         /*!< 接收接口 */
    msg_recv_callback_t callback; /*!< 回调函数 */

    uint8_t *send_buf;    /*!< 发送缓冲区, 用于处理 CRC, 帧标识, 转义等内容 */
    size_t send_buf_size; /*!< 发送缓冲区大小 */
#if MSG_ENABLE_RTOS
    SemaphoreHandle_t send_semp; /*!< 发送缓冲区的互斥信号量 */
#endif                           /* MSG_ENABLE_RTOS */

    uint8_t *recv_buf;    /*!< 接收缓冲区, 用于处理 CRC, 帧标识, 转义等内容 */
    size_t recv_buf_size; /*!< 接收缓冲区大小 */

    /*!< 接收队列, 接收入队, 处理完出队 */
    fifo_node_t *fifo_head; /*!< 队列头指针 */
    fifo_node_t *fifo_tail; /*!< 队列尾指针 */

    deal_type_t DEAL_FLAGE; /*!< 帧处理标识 */

#if MSG_ENABLE_STATISTICS
    /* 发送相关 */
    uint32_t send_success; /*!< 发送成功计数 */
    uint32_t send_fail;    /*!< 发送失败计数 */

    /* 接收相关 */
    uint32_t recv_count; /*!< 接收计数 */
    uint32_t recv_error; /*!< 接收错误计数 */
    uint32_t recv_fail;  /*!< 接收失败计数 */

    /* 接收队列相关 */
    uint32_t fifo_len;     /*!< 当前 FIFO 数据量 */
    uint32_t max_fifo_len; /*!< 最大 FIFO 数据量 */

#endif /* MSG_ENABLE_STATISTICS */
} msg_list[MSG_TYPE_LENGTH_RESERVE];

/**
 * @brief 注册消息回调函数
 * 
 * @param msg_type 消息类型
 * @param msg_callback 消息回调函数
 * @return 操作状态
 * @note 回调函数必须为非阻塞函数 
 */
msg_status_t message_register_recv_callback(msg_type_t msg_type,
                                            msg_recv_callback_t msg_callback) {
    if (msg_type >= MSG_TYPE_LENGTH_RESERVE) {
        return MSG_ERROR;
    }

    if (msg_callback == NULL) {
        return MSG_ERROR;
    }

    msg_list[msg_type].callback = msg_callback;

    return MSG_OK;
}

/**
 * @brief 取消注册消息回调函数
 * 
 * @param msg_type 消息类型
 * @return 操作状态
 */
msg_status_t message_unregister_recv_callback(msg_type_t msg_type) {
    if (msg_type >= MSG_TYPE_LENGTH_RESERVE) {
        return MSG_ERROR;
    }

    msg_list[msg_type].callback = NULL;

    return MSG_OK;
}

/**
 * @brief 注册消息发送函数
 * 
 * @param msg_type 消息类型
 * @param msg_callback 消息发送函数
 * @return 操作状态
 */
msg_status_t message_register_send_port(msg_type_t msg_type,
                                        msg_send_port_t send_port) {
    if (msg_type >= MSG_TYPE_LENGTH_RESERVE) {
        return MSG_ERROR;
    }

    if (send_port == NULL) {
        return MSG_ERROR;
    }

    msg_list[msg_type].send = send_port;

    return MSG_OK;
}

/**
 * @brief 取消消息发送函数
 * 
 * @param msg_type 消息类型
 * @return 操作状态
 */
msg_status_t message_unregister_send_port(msg_type_t msg_type) {
    if (msg_type >= MSG_TYPE_LENGTH_RESERVE) {
        return MSG_ERROR;
    }

    msg_list[msg_type].send = NULL;

    return MSG_OK;
}

/**
 * @brief 注册消息接收函数
 * 
 * @param msg_type 消息类型
 * @param msg_callback 消息接收函数
 * @return 操作状态
 * @note 接收函数必须是非阻塞式, 否则会导致其他消息无法处理
 */
msg_status_t message_register_recv_port(msg_type_t msg_type,
                                        msg_recv_port_t recv_port) {
    if (msg_type >= MSG_TYPE_LENGTH_RESERVE) {
        return MSG_ERROR;
    }

    if (recv_port == NULL) {
        return MSG_ERROR;
    }

    msg_list[msg_type].recv = recv_port;

    return MSG_OK;
}

/**
 * @brief 注册消息接收函数
 * 
 * @param msg_type 消息类型
 * @return 操作状态
 */
msg_status_t message_unregister_recv_port(msg_type_t msg_type) {
    if (msg_type >= MSG_TYPE_LENGTH_RESERVE) {
        return MSG_ERROR;
    }

    msg_list[msg_type].recv = NULL;

    return MSG_OK;
}

/**
 * @brief 设置发送缓冲区大小
 * 
 * @param msg_type 消息类型
 * @param size 缓冲区大小
 * @return 操作状态
 * @note 此缓冲区是用来处理转义, 帧标识等信息用的. 
 *       大小建议为一帧数据的 1.5 ~ 2 倍左右, 不够会动态扩容
 *       如果使用操作系统将会自动创建一个互斥信号量以保证线程安全
 */
msg_status_t message_set_send_buf(msg_type_t msg_type, size_t size) {
    if (msg_type >= MSG_TYPE_LENGTH_RESERVE) {
        return MSG_ERROR;
    }

    if (size == 0) {
        return MSG_ERROR;
    }

    struct msg_struct *msg = &msg_list[msg_type];

#if MSG_ENABLE_RTOS
    if (msg->send_semp == NULL) {
        /* 没有创建过互斥信号量, 创建一个 */
        msg->send_semp = xSemaphoreCreateMutex();
    }

    if (msg->send_semp == NULL) {
        /* 创建失败, 返回 */
        return MSG_MEM_FAIL;
    }
#endif /* MSG_ENABLE_RTOS */

    if (msg->send_buf == NULL) {
        /* 没有分配过, 分配一次 */
        msg->send_buf = MSG_MALLOC(size);
    } else {
        /* 已创建缓冲区, 重新分配大小 */
        uint8_t *new_buf = MSG_REALLOC(msg->send_buf, size);

        if (new_buf != NULL) {
            msg->send_buf = new_buf;
        } else {
            return MSG_MEM_FAIL;
        }
    }

    if (msg->send_buf == NULL) {
        /* 创建失败, 返回 */
        return MSG_MEM_FAIL;
    }

    msg->send_buf_size = size;

    return MSG_OK;
}

/**
 * @brief 设置接收缓冲区
 * 
 * @param msg_type 消息类型
 * @param size 缓冲区大小
 * @return 操作状态
 * @note 此缓冲区为`message_polling_data`函数服务.
 *       大小建议为一帧数据的 1.5 ~ 2 倍左右, 不够会动态扩容
 */
msg_status_t message_set_recv_buf(msg_type_t msg_type, size_t size) {
    if (msg_type >= MSG_TYPE_LENGTH_RESERVE) {
        return MSG_ERROR;
    }

    if (size == 0) {
        return MSG_ERROR;
    }

    struct msg_struct *msg = &msg_list[msg_type];

    if (msg->recv_buf == NULL) {
        /* 没有分配过, 分配一次 */
        msg->recv_buf = MSG_MALLOC(size);
    } else {
        /* 已创建缓冲区, 重新分配大小 */
        uint8_t *new_buf = MSG_REALLOC(msg->recv_buf, size);

        if (new_buf != NULL) {
            msg->recv_buf = new_buf;
        } else {
            return MSG_MEM_FAIL;
        }
    }

    if (msg->recv_buf == NULL) {
        /* 创建失败, 返回 */
        return MSG_MEM_FAIL;
    }

    msg->recv_buf_size = size;

    return MSG_OK;
}

/**
 * @brief 发送消息
 * 
 * @param msg_type 消息类型
 * @param data_type 数据类型
 * @param[in] data 消息内容
 * @param data_len 消息长度
 * @return 操作状态
 * @note 此函数会打包封装消息, 然后通过注册的接口发送. 
 *       需要先通过`message_register_send_port`注册发送接口,
 *       发送完毕需要
 */
msg_status_t message_send_data(msg_type_t msg_type, msg_data_type_t data_type,
                               uint8_t *data, size_t data_len) {
    if (msg_type >= MSG_TYPE_LENGTH_RESERVE) {
        return MSG_ERROR;
    }

    if (data == NULL) {
        return MSG_ERROR;
    }

    if (data_len == 0) {
        return MSG_ERROR;
    }

    struct msg_struct *msg = &msg_list[msg_type];

    if (msg->send == NULL) {
        return MSG_ERROR;
    }

    if (msg->send_buf == NULL) {
        return MSG_MEM_FAIL;
    }

#if MSG_ENABLE_RTOS
    if (msg->send_semp == NULL) {
        /* 必须创建互斥信号量 */
        return MSG_ERROR;
    }
    xSemaphoreTake(msg->send_semp, portMAX_DELAY);
#endif /* MSG_ENALBE_RTOS */

    if (data_len >= msg->send_buf_size) {
        /* 扩容, 按照大小两倍扩容 */
        uint8_t *new_buf = MSG_REALLOC(msg->send_buf, data_len * 2);
        if (new_buf == NULL) {
            return MSG_MEM_FAIL;
        }
        msg->send_buf = new_buf;
        msg->send_buf_size = data_len * 2;
    }

    /* 数组索引 */
    uint8_t *send_buf = msg->send_buf;
    size_t buf_index = 0;

    /*# 1. byte[0:1], 帧起始标识 + 类型 ###################*/
    send_buf[0] = MSG_SOF;
    send_buf[1] = data_type;

    /*# 2. 长度字节计算填充 ###############################*/
    /* 数据长度 */
    size_t len = data_len;

    buf_index = 2;
    while (len > 0x7F) {
        send_buf[buf_index] = 0x80;
        ++buf_index;
        len -= 0x7F;
    }

    /* 最后一个字节为剩余长度 */
    send_buf[buf_index] = len;

    /*# 3. 数据区内容填充, 加转义 ##########################*/
    /* 循环长度条件, 小于 数据长度 + 长度占用的字节数 + 起始帧 + 转义占用 */
    len = data_len + buf_index + 1;
    /* 数据内容索引 */
    size_t data_index = 0;
    ++buf_index;

    while (buf_index < len) {
        /* 将数据写入到缓冲区 */
        send_buf[buf_index] = data[data_index];

#if MSG_ENABLE_ESCAPE
        if (data[data_index] == MSG_EOF) {
            ++buf_index;
            send_buf[buf_index] = data[data_index];
            /* 长度 + 1, 避免未写入完提前跳出循环导致丢数据 */
            ++len;
        }
#endif /* MSG_ENABLE_ESCAPE */

        ++buf_index;
        ++data_index;
    }

    /*# 4. 写入完毕, 添加 CRC 校验 #########################*/
#if MSG_ENABLE_CRC
    /* 对帧头, 长度, 数据区内容校验 */
    MSG_CRC_TYPE crc_result = bsp_crc32_calc(send_buf, buf_index);
    for (size_t i = 0; i < sizeof(crc_result); ++i) {
        send_buf[buf_index] = crc_result & 0xFF;
#if MSG_ENABLE_ESCAPE
        if (send_buf[buf_index] == MSG_EOF) {
            send_buf[buf_index + 1] = send_buf[buf_index];
            ++buf_index;
        }
#endif /* MSG_ENABLE_ESCCAPE */
        ++buf_index;
        crc_result >>= 8;
    }
#endif /* MSG_ENABLE_CRC */

    /*# 5. 添加帧结束标识 ##################################*/
    send_buf[buf_index] = MSG_EOF;

    /*# 6. 打包完毕, 发送! #################################*/
    if (msg->send(msg_type, buf_index + 1, send_buf) != MSG_OK) {
        /* 返回发送失败 */
#if MSG_ENABLE_STATISTICS
        ++msg->send_fail;
#endif /* MSG_ENABLE_STATISTICS */

        return MSG_ERROR;
    }

#if MSG_ENABLE_STATISTICS
    ++msg->send_success;
#endif /* MSG_ENABLE_STATISTICS */
    return MSG_OK;
}

#if MSG_ENABLE_RTOS

/**
 * @brief 发送完毕后释放互斥信号量
 * 
 * @param msg_type 消息类型
 * @warning 不要在中断中使用! 互斥信号量在中断没有意义.
 *          硬件资源与缓冲区资源是两个不同的资源, 不应该用一个信号量
 */
void message_send_finish_handle(msg_type_t msg_type) {
    if (msg_list[msg_type].send_semp != NULL) {
        xSemaphoreGive(msg_list[msg_type].send_semp);
    }
}
#endif /* MSG_ENABLE_RTOS */

/**
 * @brief 处理缓存区数据帧并加入队列
 * @note 处理思路：
 *       每一次处理缓存区都进行循环直到遍历所有数据，
 *       在每一次循环中根据消息结构体里的标志位来决定是找SOF还是EOF。
 *       如果是找SOF又分为找到完整帧与只找到头帧两种情况，
 *       如果找到完整帧，则进行CRC校验，然后分配空间存储消息，反转义，加入队列。
 *       如果只找到头帧，则存储前半个帧，并更改消息结构体中的标志位，让下一次循环找EOF。
 *       如果是找EOF，没找到清空队列中的最后一个节点（存储的是前半截帧）并更改标志位让下一次循环找SOF，
 *       找到的话选择与前半截帧合并作为完整的帧进行校验，也更改标志位让下一次循环找SOF。
 *       
 *       重构思路：
 *       将完整帧的处理（CRC校验和反转义的过程）和找EOF标识符的过程单独封装成函数进行复用。
 *       对于数据帧的格式进行调整，CRC校验位数固定，数据区长度部分取消。
 * 
 * @param msg 消息对象
 * @param recv_len 缓存区数据长度
 * @return msg_type_t 
 */
static msg_type_t message_data_enqueue(struct msg_struct *msg,
                                       uint32_t recv_len) {
    uint8_t *recv_buf = msg->recv_buf;
    uint32_t buf_idx = 0;

    /* 创建队列节点 */
    if (msg->DEAL_FLAGE != TAIL_FRAME) {
        fifo_node_t *new_node =
            (fifo_node_t *)MSG_CALLOC(1, sizeof(fifo_node_t));
        if (new_node == NULL) {
            return MSG_MEM_FAIL;
        }
        if (msg->fifo_head == NULL) {
            msg->fifo_head = new_node;
            new_node->next = NULL;
            new_node->prev = NULL;
            msg->fifo_tail = new_node;
        } else {
            msg->fifo_tail->next = new_node;
            new_node->prev = msg->fifo_tail;
            msg->fifo_tail = new_node;
        }
    }

    while (buf_idx < recv_len) {

        switch (msg->DEAL_FLAGE) {
            /* 处理找起始标志位的情况 */
            case HEAD_FRAME:

                /* 找到起始标识 */
                while (recv_buf[buf_idx] != MSG_SOF) {
                    ++buf_idx;
                    /* 没找到, 返回 */
                    if (buf_idx >= recv_len) {
                        return MSG_RECV_ERROR;
                    }
                }
                recv_buf = &msg->recv_buf[buf_idx];

                /* 找到结束标识符确定消息长度 */
                uint32_t msg_len = 0;
                uint32_t eof_num = 0;
                bool is_full_frame = false;
                while (1) {
                    ++msg_len;
                    eof_num = 0;
                    /* 判断连续的eof标识符个数，偶数表示为转义，奇数表明最后一个为结束标识符 */
                    while (recv_buf[msg_len] == MSG_EOF) {
                        ++eof_num;
                        ++msg_len;
                    }
                    if (eof_num % 2 == 1) { /*!<获得一个完整的帧的处理机制 */
                        is_full_frame = true;
                        break;
                    } else if (msg_len >= (recv_len - buf_idx)) {
                        /*!<获得半个帧的处理机制 */
                        is_full_frame = false;
                        break;
                    }
                }
                buf_idx +=
                    msg_len; /* 索引指向当前帧的后面，以满足循环的退出条件 */
                /* 帧处理 */
                if (is_full_frame) {
                    /* 处理完整的帧 */
                    /* 进行CRC校验，存疑->大小端问题 */
                    send_crc_t send_crc;
                    for (int i = 0; i < sizeof(MSG_CRC_TYPE); ++i) {
                        send_crc.array[i] = recv_buf[msg_len - i - 2];
                    }
                    uint32_t recv_crc = bsp_crc32_calc(
                        recv_buf, msg_len - (sizeof(MSG_CRC_TYPE) + 1));
                    if (send_crc.result != recv_crc) {
                        break; /* CRC错误退出当前帧的处理 */
                    }

                    /* 为完整的帧分配空间 */
                    uint8_t *frame_space = (uint8_t *)MSG_CALLOC(1, msg_len);
                    if (frame_space == NULL) {
                        return MSG_MEM_FAIL;
                    }
                    /* 复制数据并进行反转义 */
                    uint32_t real_len = 0;
                    for (int i = 0; i < msg_len; i++) {
                        /* 反转义只用考虑偶数个EOF出现的情况 */
                        if (recv_buf[i] == MSG_EOF && i != msg_len - 1) {
                            frame_space[real_len] = recv_buf[++i];
                        } else {
                            frame_space[real_len] = recv_buf[i];
                        }
                        real_len++;
                    }
                    msg->fifo_tail->data = frame_space;
                    msg->fifo_tail->len = real_len;
                    msg->fifo_tail->complete = true;
                    msg->DEAL_FLAGE = HEAD_FRAME;

                } else {
                    /* 存储前半个帧*/
                    uint8_t *frame_space = (uint8_t *)MSG_CALLOC(1, msg_len);
                    if (frame_space == NULL) {
                        return MSG_MEM_FAIL;
                    }
                    memcpy(frame_space, recv_buf, msg_len);
                    msg->fifo_tail->data = frame_space;

                    msg->fifo_tail->len = msg_len;
                    msg->fifo_tail->complete = false;
                    msg->DEAL_FLAGE = TAIL_FRAME;
                }
                break;
            case TAIL_FRAME:
                /* 先找EOF标识符 */
                uint32_t half_msg_len = 0;
                uint32_t half_eof_num = 0;
                while (1) {
                    ++half_msg_len;
                    half_eof_num = 0;
                    /* 判断连续的eof标识符个数，偶数表示为转义，奇数表明最后一个为结束标识符 */
                    while (recv_buf[half_msg_len] == MSG_EOF) {
                        ++half_eof_num;
                        ++half_msg_len;
                    }
                    if (half_eof_num % 2 ==
                        1) { /* 获得一个完整的帧的处理机制 */
                        break;
                    } else if (half_msg_len >= (recv_len - buf_idx)) {
                        /* 没有找到帧的结束标识符删除已经存储起来的上半帧 */
                        msg->fifo_tail = msg->fifo_tail->prev;
                        MSG_FREE(
                            msg->fifo_tail->next
                                ->data); /* 释放节点指向的数据以及节点自身的存储空间 */
                        MSG_FREE(msg->fifo_tail->next);
                        msg->fifo_tail->next = NULL;
                        msg->DEAL_FLAGE = HEAD_FRAME;
                        return MSG_RECV_ERROR;
                    }
                }
                /* 将找到的尾帧和上次处理得到的头帧合并 */
                uint32_t head_len = msg->fifo_head->len;
                uint32_t full_len = head_len + half_msg_len;
                uint8_t *full_frame =
                    MSG_REALLOC(msg->fifo_tail->data,
                                full_len); /* 重新分配空间以容纳完整的帧 */
                if (full_frame == NULL) {
                    return MSG_MEM_FAIL;
                }
                memcpy(full_frame + head_len, recv_buf, half_msg_len);
                msg->fifo_tail->len = full_len;
                /* 处理拼凑成的完整的帧 */
                /* 进行CRC校验，存疑->大小端问题 */
                send_crc_t send_crc_fix;
                for (int i = 0; i < sizeof(MSG_CRC_TYPE); ++i) {
                    send_crc_fix.array[i] = full_frame[full_len - i - 2];
                }
                uint32_t recv_crc = bsp_crc32_calc(
                    full_frame, full_len - (sizeof(MSG_CRC_TYPE) + 1));
                if (send_crc_fix.result != recv_crc) {
                    MSG_FREE(full_frame);
                    MSG_FREE(msg->fifo_tail->data);
                    msg->fifo_tail = msg->fifo_tail->prev;
                    MSG_FREE(msg->fifo_tail->next->data);
                    MSG_FREE(msg->fifo_tail->next);
                    break; /* CRC错误退出当前帧的处理 */
                }
                /* 复制数据并进行反转义 */
                uint32_t real_len =
                    0; /* !< 需要更改！以去除头部标志位和长度标识符 */
                for (int i = 0; i < full_len; i++) {
                    if (full_frame[i] == MSG_EOF && i != full_len - 1) {
                        full_frame[real_len] = full_frame[++i];
                    } else {
                        full_frame[real_len] = full_frame[i];
                    }
                    real_len++;
                }
                msg->fifo_tail->data = full_frame;
                msg->DEAL_FLAGE = HEAD_FRAME;
                break;
            default:
                break;
        }
    }
}

/**
 * @brief 轮询数据并回调
 * 
 * @return 轮询状态
 * @note `callback`函数不能为阻塞函数
 */
msg_status_t message_polling_data(void) {
    struct msg_struct *msg = NULL;
    uint32_t recv_len;

    for (msg_type_t msg_idx = 0; msg_idx < MSG_TYPE_LENGTH_RESERVE; ++msg_idx) {
        msg = &msg_list[msg_idx];

        if (msg->recv == NULL) {
            continue;
        }

        recv_len = msg->recv(msg_idx, msg->recv_buf_size, msg->recv_buf);
        if (recv_len == 0) {
            continue;
        }

        message_data_enqueue(msg, recv_len);
    }
    return MSG_RECV_NO_MSG;
}
