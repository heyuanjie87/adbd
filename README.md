# Android Debug Bridge daemon implementation in RT-Thread
在PC与RT-Thread之间建立文件传输与执行shell的通道。

## 1. 已实现功能
- 通信：tcpip
- 通信：usb
- 服务：文件pull/push
- 服务：shell

## 2. 依赖

- 传输 - LWIP/winusb
- 文件系统、POSIX、LIBC
- shell - 依赖finsh/msh 
- 上位机工具

## 3. 配置ADBD

### 3.1 启用ADBD
在rtconfig.h写入如下定义:

```
#define PKG_USING_ADB   
#define ADB_SERVICE_SHELL_ENABLE    /* 开启shell服务 */   
#define ADB_SERVICE_FILESYNC_ENABLE /* 开启文件服务 */   
#define ADB_TR_TCPIP_ENABLE    /* 开启tcp传输(可选) */   
#define ADB_TR_USB_ENABLE      /* 开启usb传输(可选,至少需选一种),
                                  usb device需开启winusb */   
```

## 4. 参考文档

### 4.1 ADB官方文档

- [ADB简介](docs/OVERVIEW.TXT)
- [协议简介](docs/PROTOCOL.TXT)
- [文件服务](docs/SYNC.TXT)

### PC工具
- win:[adb.exe](http://adbshell.com/downloads)
