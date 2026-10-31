# Coffee Watch

**为手冲精品咖啡设计的咖啡助手。**

Coffee Watch 将 M5Stack StopWatch、蓝牙咖啡秤和浓度仪连接起来，通过一套特定的冲煮流程，快速读取多个设备的数据，自动计算并呈现一杯咖啡的冲煮结果，让记录自然融入冲煮过程。

## 从冲煮到报告

1. **选择器具**：支持 V60、Hario Switch、AeroPress 和 Ceado Hoop。
2. **称量空壶**：自动记录分享壶重量；粉量直接读取秤上预先保存的数据。
3. **开始冲煮**：点击 Start，让秤进入 Auto READY；注水后跟随秤的事件自动开始计时，展示注水量与粉水比。
4. **结算出液**：冲煮结束、移除滤杯触发 Stop 后，按提示拿走分享壶，自动计算出液量，无需再次手动去皮。
5. **测量浓度**：用浓度仪测量咖啡，接收本次 TDS 数据。
6. **查看报告**：汇总粉量、注水量、出液量、时间、粉水比、TDS、萃取率与单位粉量保水量，并展示 Golden Cup 图表。

重量和体积统一保留一位小数。萃取率使用实际出液量计算：

```text
萃取率（%） = TDS（%） × 出液量（g） ÷ 咖啡粉量（g）
```

## 当前支持的设备

| 设备 | 用途 | 已验证版本 |
| --- | --- | --- |
| M5Stack StopWatch | 交互、设备连接与结果展示 | ESP32-S3 |
| BOOKOO Themis Ultra | 粉量、重量与冲煮计时 | 固件 4.0.1，Ratio + Auto 模式 |
| DiFluid RS | TDS 测量 | 固件 v002-005 |

目前是已完成实机冲煮验证的 prototype。固件连接配置中的两台设备，尚未提供通用设备选择界面。上述流程依赖对应设备和固件的事件行为，其他型号或版本尚未验证。

## 构建与使用

使用 ESP-IDF 5.5.x，在 `firmware/demo` 目录执行：

```sh
python3 fetch_drivers.py
IDF_COMPONENT_MANAGER=0 idf.py build
idf.py -p /dev/cu.YOUR_STOPWATCH_PORT flash
```

首次构建会按固定提交下载 M5Stack 驱动。完整的设备准备、操作方式和烧录说明见 [固件使用指南](firmware/demo/README.md)。

## 开发文档

- [产品流程与交互](spec/product.md)
- [蓝牙接口](spec/bluetooth.md)
- [验收清单](spec/acceptance.md)
- [器具图形资源](spec/icons/)
- [浏览器视觉原型](spec/prototype.html)：早期界面设计参考，使用模拟设备；流程以当前固件为准。

在仓库根目录运行协议和状态机测试：

```sh
python3 -m unittest discover -s tools -v
```

测试需要 C 编译器；原生 AES 测试使用 macOS CommonCrypto，设备固件使用 ESP-IDF mbedTLS。构建产物、下载的驱动和本地抓包不纳入版本控制。
