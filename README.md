## 概述

将通信数据进行封装，允许注册不同类型的消息，并为每种消息设立独立的回调函数进行数据处理。

demo 程序详见：[f429-demo](https://github.com/XJU-Hurricane-Team/message-protocol/tree/main/f429-demo)

## 数据帧格式：

数据类型（1 byte：高四位标记 ID, 低四位标记数据类型）：数据长度(1 byte)：数据内容（n byte）：结束标志符（1 byte, 定义为`MSG_EOF`）

## 发送

- 调用`message_register_send_uart`注册串口消息通讯句柄, 发送将会使用这个函数注册的句柄
- 调用`message_send_data`来发送数据. 如果要更改串口, 重新调用`message_register_uart_handle`更改发送串口句柄
- `message_send_data`函数需要指定消息 ID (`msg_id_t`), 消息数据类型 (`msg_type_t`), `data`(数据指针, 也就是要发送的数据), 以及`data_len`, 数据长度

## 接收 

- 调用`message_register_polling_uart`添加消息 ID 对应的轮询串口
- 调用`message_register_recv_callback`注册接收回调函数, 当收到消息以后会调用回调函数.
- 需要持续调用`message_polling_data`来轮询消息, 可以放到 RTOS 的一个任务或者定时器里. 当收到消息后根据`msg_id_t`来调用相应的回调函数
- 回调函数参数形式必须是`void func(uint32_t, uint8_t, uint8_t*)`。第一个参数是消息长度, 第二个参数是消息标识 (高四位是 ID, 低四位是数据类型)，第三个参数是数据区内容, 无返回值

## 其他
- 数据接收缓存区应该设置为消息长度的 5 到 10 倍为宜
- 发送缓存区要比消息长度大，会根据发送的内容动态扩容缩容
- 可以启用`MSG_ENABLE_STATISTICS`宏定义来启用每种消息的接收情况（接收成功、错误计数，内存分配失败计数，队列长度与最大深度等）
- 接收目前仅支持 DMA 方式
- 为了做到透传，消息会对内容转义。定义`MSG_ESC`可以选择转义字符，建议选择出现频次低的字节。
