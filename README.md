# Aspeed I2C Slave

i2c 从设备驱动, 接收 master 的数据并解析, 需要 master 驱动支持注册为 slave

# 目录

- [1. 文件目录说明](#1-文件目录说明)
- [2. 原理](#2-原理)
- [3. cmd 说明](#3-cmd-说明)
  - [3.1 cpu(0x00)](#31-cpu0x00)
  - [3.2 mem(0x01)](#32-mem0x01)
- [X. TODO](#x-todo)

# 1. 文件目录说明

| 文件目录 | 说明 |
| :---: | :---: |
| asp_slave.c | i2c 从设备驱动 |

# 2. 原理

触发alert信号, master 主动向 slave 写

# 3. cmd 说明

| cmd | 说明 |
| :---: | :---: |
| 0x00 | cpu数据 |
| 0x01 | mem数据 |

## 3.1 cpu(0x00)

| 长度(byte) | 说明 |
| :---: | :---: |
| 1 | cpu核心数 |
| n | 根据核心数, 从CPU0开始的CPU利用率 |

数据格式
```shell
S 0x20 W [A] 0x00 [A] 0x04[A]
	cpu_core [A] ur_cpu0 [A] ur_cpu1 [A] ur_cpun [A] P
```

## 3.2 mem(0x01)

| 长度(byte) | 说明 |
| :---: | :---: |
| 2 | 内存容量(GB), 发送高8位, 再发送低8位 |
| 1 | 内存利用率 |

数据格式
```shell
S 0x20 W [A] 0x01 [A] 0x03 [A]
	mem_hi [A] mem_lo [A] ur_mem [A] P
```

# X. TODO
1. 可能修改为以信号的形式通知上层 sdbus 程序获取数据
