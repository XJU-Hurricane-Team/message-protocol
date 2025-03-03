/**
 * @file    msg_protocol.h
 * @author  Deadline039
 * @brief   消息协议
 * @version 2.0
 * @date    2024-03-01
 * @note    接收暂只支持DMA, 如果使用中断自行到`uart.h`编写相应代码
 *
 *****************************************************************************
 *                             ##### 如何使用 ####
 * (#) 在`msg_type_t`枚举中添加要使用的数据通信类型
 *
 *
 * (#) 发送
 *      (##) 调用`message_register_uart_handle`注册串口消息通讯句柄, 发送将会
 *           使用这个函数注册的句柄
 *      (##) 调用`message_send_data`来发送数据. 如果要更改串口, 重新调用
 *           `message_register_uart_handle`更改发送串口句柄
 *      (##) `message_send_data`函数需要指定`data_mean`(也就是`msg_type_t`
 *           枚举中的类型), `data_type`(数据类型, 整数, 浮点或者字符串等),
 *           `data`(数据指针, 也就是要发送的数据), 以及`data_len`, 数据长度
 * (#) 接收
 *      (##) 调用`message_add_polling_handle`添加要轮询的串口
 *      (##) 调用`message_register_recv_callback`注册接收回调函数, 当收到消息
 *           以后会调用回调函数.
 *      (##) 需要持续调用`message_polling_data`来轮询消息, 可以放到RTOS的一个任
 *           务或者定时器里. 当收到消息后根据`data_mean`来调用相应的回调函数
 *      (##) 回调函数参数形式必须是void (uint8_t, msg_data_type_t, void*)
 *           第一个参数是消息长度, 第二个参数是数据类型(整数, 浮点或者字符串等),
 *           第三个参数是数据区内容, 无返回值
 *      (##) `message_polling_data`仅支持DMA接收, 如果是串口接收需要自行编写回调
 *           函数与接收逻辑
 *      (##) 调用`message_remove_polling_handle`删除要轮询的串口
 ******************************************************************************
 *    Date    | Version |   Author    | Version Info
 * -----------+---------+-------------+----------------------------------------
 * 2024-04-13 |   1.0   | Deadline039 | 初版
 */

#ifndef __MSG_PROTOCOL_H
#define __MSG_PROTOCOL_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* 帧起始标志 (Start Of Frame), 建议大于 0x80 */
#define MSG_SOF               0xFE
/* 帧结束标志 (End Of Frame), 建议大于 0x80 */
#define MSG_EOF               0xFF

/* 始能 CRC 校验 */
#define MSG_ENABLE_CRC        1
/* CRC 校验结果类型 */
#define MSG_CRC_TYPE          uint32_t

/* 始能转义.  */
#define MSG_ENABLE_ESCAPE     1
/* 线程安全处理, 启用后会使用互斥信号量来管理全局变量, 仅支持 FreeRTOS. */
#define MSG_ENABLE_RTOS       1

/* 始能统计, 启用后统计发送消息量, 队列最大深度等信息, 误码率等信息 */
#define MSG_ENABLE_STATISTICS 1

/* 内存分配相关 */
#define MSG_MALLOC(x)         malloc(x)
#define MSG_CALLOC(n, x)      calloc(n, x)
#define MSG_REALLOC(p, x)     realloc(p, x)
#define MSG_FREE(p)           free(p)

/**
 * @brief 消息类型
 */
typedef enum {
    MSG_TYPE_DEMO, /*!< 测试类型, 需要保证所有通信设备的类型一致 (可以复制本枚举) */

    MSG_TYPE_LENGTH_RESERVE /*!< 保留类型, 用于定义消息类型个数 */
} msg_type_t;

/**
 * @brief 消息数据类型
 */
typedef enum {
    MSG_DATA_UINT8 = 0x00U,
    MSG_DATA_INT8,
    MSG_DATA_UINT16,
    MSG_DATA_INT16,
    MSG_DATA_INT32,
    MSG_DATA_UINT32,
    MSG_DATA_INT64,
    MSG_DATA_UINT64,
    MSG_DATA_FP32,
    MSG_DATA_FP64,
    MSG_DATA_STRING,
    MSG_DATA_CUSTOM /*!< 自定义的数据类型, 如结构体 */
} msg_data_type_t;

/**
 * @brief 消息函数状态
 */
typedef enum {
    MSG_OK,           /*!< 操作成功 */
    MSG_ERROR,        /*!< 操作失败 */
    MSG_MEM_FAIL,     /*!< 内存分配失败 */
    MSG_BUF_OVERFLOW, /*!< 缓冲区溢出 */
    MSG_RECV_NO_MSG,  /*!< 无消息 */
    MSG_RECV_ERROR,   /*!< 有错误 */
    MSG_RECV_NEW_MSG, /*!< 接收到新消息 */
} msg_status_t;

/**
 * @brief 消息回调函数 (非阻塞)
 * 
 * @param msg_type 消息类型
 * @param data_type 消息数据类型
 * @param msg_len 消息数据长度
 * @param[in] msg_data 消息内容
 * @note 此接口是接收解包好的数据, `msg_data`是去掉帧头尾, 校验, 转义的内容
 */
typedef void (*msg_recv_callback_t)(msg_type_t /* msg_type */,
                                    msg_data_type_t /* data_type */,
                                    size_t /* msg_len */,
                                    uint8_t * /* msg_data */);

/**
 * @brief 发送消息接口
 * 
 * @param msg_type 消息类型
 * @param msg_len 本次消息长度
 * @param[in] msg_data 消息内容
 * @return 发送状态, 成功返回`MSG_OK`或者 0
 * @note 此接口只需要发送数据即可, 数据已打包好
 */
typedef msg_status_t (*msg_send_port_t)(msg_type_t /* msg_type */,
                                        size_t /* msg_len */,
                                        uint8_t * /* msg_data */);

/**
 * @brief 接收消息接口 (非阻塞)
 * 
 * @param msg_type 消息类型
 * @param buf_len 缓冲区大小 (避免溢出)
 * @param[out] data_buf 缓冲区指针 (接收到的消息放这里)
 * @return 接收到的长度, 无数据返回 0
 * @note 此接口只需要将数据写入到`data_buf`即可, 解析完后会自动调用
 */
typedef size_t (*msg_recv_port_t)(msg_type_t /* msg_type */,
                                  size_t /* buf_len */,
                                  uint8_t * /* data_buf */);

msg_status_t message_register_recv_callback(msg_type_t msg_type,
                                            msg_recv_callback_t msg_callback);
msg_status_t message_unregister_recv_callback(msg_type_t msg_type);

msg_status_t message_register_send_port(msg_type_t msg_type,
                                        msg_send_port_t send_port);
msg_status_t message_unregister_send_port(msg_type_t msg_type);

msg_status_t message_register_recv_port(msg_type_t msg_type,
                                        msg_recv_port_t recv_port);
msg_status_t message_unregister_recv_port(msg_type_t msg_type);

msg_status_t message_set_send_buf(msg_type_t msg_type, size_t size);
msg_status_t message_set_recv_buf(msg_type_t msg_type, size_t size);

msg_status_t message_send_data(msg_type_t msg_type, msg_data_type_t data_type,
                               uint8_t *data, size_t data_len);
#if MSG_ENABLE_RTOS
void message_send_finish_handle(msg_type_t msg_type);
#endif /* MSG_ENABLE_RTOS */

msg_status_t message_polling_data(void);

#endif /* __MSG_PROTOCOL_H */