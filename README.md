# Android Debug Bridge daemon implementation in RT-Thread

## 1. 介绍

以TCP为通信接口，在PC与RT-Thread之间建立文件传输与执行shell
的通道。

## 2. 依赖

- 传输 - LWIP
- 文件系统、POSIX、LIBC
- shell - 依赖finsh/msh 

- 上位机工具 - (win: adb.exe)

## 2. 配置ADBD

### 2.1 启用ADBD
在rtconfig.h写入如下定义:

`#define PKG_USING_ADB`   
`#define ADB_SERVICE_SHELL_ENABLE`    /* 开启shell服务 */   
`#define ADB_SERVICE_FILESYNC_ENABLE` /* 开启文件服务 */   

## 3. 参考文档

### 3.1 ADB官方文档

- [ADB简介](docs/OVERVIEW.TXT)
- [协议简介](docs/PROTOCOL.TXT)
- [文件服务](docs/SYNC.TXT)
