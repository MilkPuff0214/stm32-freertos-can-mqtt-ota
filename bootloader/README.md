# 独立裸机 Bootloader

本目录是 STM32F103ZET6 的独立 Boot 工程，负责候选镜像校验、内部 Flash 安装及启动 Application。项目按代码检查与资料归档收尾，验收证据边界见[项目结项记录](../docs/PROJECT_CLOSEOUT.md)。

## 目录

- [职责与工程边界](#职责与工程边界)
- [镜像与持久交接](#镜像与持久交接)
- [构建与产物](#构建与产物)
- [已检查事实与验证边界](#已检查事实与验证边界)

## 职责与工程边界

Boot 不运行 FreeRTOS、W5500、MQTT 或 FTP。`User/app/boot/`负责升级编排与 CPU 交接，`User/bsp/`负责 W25Q64 和内部 Flash 操作，`Libraries/`保存本工程使用的 CMSIS、标准外设库及 Mbed TLS 3.6.7 MD5 子集。

源码、头文件、库、启动文件、工具链和链接脚本均由本目录自有；构建不包含或链接`firmware/`中的文件。Boot 与 Application 通过[协议文档](../docs/protocol.md#11-升级协议)规定的实际字节及地址交接，不共享 C 结构体或公共源码。

## 镜像与持久交接

| 内容 | 地址或范围 | 约束 |
| --- | --- | --- |
| Boot | `0x08000000～0x0800FFFF` | 64 KiB，安装路径不得擦写 |
| Application 完整文件 | `0x08010000～0x0807FFFF` | 最大 448 KiB |
| Application 描述头 | `0x08010000～0x080101FF` | 固定 512 字节 |
| Application 向量表 | `0x08010200` | 从该表取得 MSP 与 Reset_Handler |
| W25Q64 元数据 A/B | `0x000000`、`0x001000` | 独立 4 KiB 扇区，各保存 256 字节 v3 记录 |
| W25Q64 候选槽 | `0x010000～0x10FFFF` | 单个 1 MiB 槽，保存完整`application.bin` |

描述头包含硬件型号、软件型号和完整文件长度，余下 476 字节全零；MD5 覆盖整个文件，包括描述头，期望摘要保存于元数据。Boot 在擦除内部应用区前重新校验硬件型号、长度、向量范围和完整文件 MD5，写入后逐块读回并复核内部文件 MD5。

A/B 元数据使用 CRC32 与最后单独写入的`CMIT`提交标记：先写入并读回前 252 字节，再写最后 4 字节，完整读回有效后才切换活动记录。启动选择有效且代次较新的记录，支持 32 位代次回绕；同代内容冲突时停留故障态。

Application 持久提交`UPGRADE_REQUESTED`后复位，Boot 先提交`INSTALLING`并登记安装次数，再擦写内部 Flash；安装期间复位后重新校验同一候选并从头安装。成功后一次提交`BOOT_PENDING`及首次启动次数`1`，直接跳转新应用。新应用满足本地确认条件并完整提交`CONFIRMED`后，普通复位不再消耗启动次数。

安装与未确认启动各最多尝试 3 次，均在执行前持久记账；次数耗尽或严重故障时停留 Boot，使用调试器人工恢复。方案只保存一个候选和一个内部应用，不提供旧版本自动回滚；首版不做软件版本防降级，MD5 只用于完整性检查，不提供固件签名认证。

## 构建与产物

项目使用 ARM GCC 14.3.1、CMake 和 GNU Make。将`arm-none-eabi-gcc`及 GNU Make 加入本机`PATH`，从仓库根目录执行：

```powershell
cmake -S bootloader -B bootloader/build -G "Unix Makefiles" `
    -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake `
    -DCMAKE_BUILD_TYPE=Debug
cmake --build bootloader/build --parallel
```

工具链参数使用 Boot 本地的`bootloader/cmake/arm-none-eabi-gcc.cmake`，该相对路径由 CMake 按 Boot 源目录解析。工具链通过`PATH`发现编译器，并从同一工具目录选择其他 ARM 工具，不需要在仓库保存本机绝对路径。链接脚本为`linker/STM32F103ZETx_BOOT.ld`；产物为`build/bootloader.elf`、`.hex`、`.bin`和`.map`，`build/`属于生成目录。

以上命令供用户需要复现构建时使用；本轮没有重新构建或烧录。

## 已检查事实与验证边界

截至 2026-10-04，已有文档记录 Boot 的 Debug/Release 构建通过，以及用户烧录 Boot/Application 后工具读回成功。本轮只读检查现有`bootloader.bin`：文件大小为 9,088 字节，初始 MSP 为`0x20010000`，Reset_Handler 为`0x080001E5`，处于 64 KiB Boot 范围内。对照现有 map 与 BIN，最终 CPU 交接对应`msr msp, r0`及`bx r1`，切换 MSP 后不再执行 C 语句。

Boot 与 Application 的元数据 v3、CRC32、CMIT、Flash 地址和 512 字节头已完成静态兼容检查；未发现此次审查范围内阻断正常主流程的代码问题。现有 GNU 反汇编工具在本轮启动时遇到 Windows DLL 重定位错误，因此本轮未重新执行反汇编。

现有证据仍未覆盖完整正常升级、新应用持久`CONFIRMED`及再次复位后的实机运行。故障注入测试按用户决定跳过，保护代码继续保留；烧录读回和静态检查不记作这些路径已实测通过。最终归档与 Git 提交说明统一见[项目结项记录](../docs/PROJECT_CLOSEOUT.md)。
