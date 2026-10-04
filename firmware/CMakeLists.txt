cmake_minimum_required(VERSION 3.22)                    #指明cmake的最低版本要求

project(stm32f103_firmware LANGUAGES C ASM)             #说明项目名称和使用的语言：C和汇编

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)          #if (源码目录 == 构建目录) STREQUAL：“string equal”
    message(FATAL_ERROR "In-source builds are not supported. Use: cmake -S . -B build")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   #这个 JSON 文件会记录每一个 .c/.cpp 文件真正使用的编译命令

# 这条命令创建了一个 CMake target
add_executable(firmware
    Libraries/CMSIS/startup/gcc/startup_stm32f103xe.S
    Libraries/CMSIS/system_stm32f10x.c
    Libraries/FWlib/src/stm32f10x_gpio.c
    Libraries/FWlib/src/stm32f10x_rcc.c
    Libraries/FWlib/src/stm32f10x_usart.c
    Libraries/FWlib/src/stm32f10x_dma.c
    Libraries/FWlib/src/stm32f10x_tim.c
    Libraries/FWlib/src/stm32f10x_dbgmcu.c
    Libraries/FWlib/src/stm32f10x_adc.c
    Libraries/FWlib/src/stm32f10x_can.c
    Libraries/FWlib/src/stm32f10x_spi.c
    

    # FreeRTOS Kernel V11.3.0 minimal sources.
    Libraries/FreeRTOS-Kernel/tasks.c
    Libraries/FreeRTOS-Kernel/list.c
    Libraries/FreeRTOS-Kernel/portable/GCC/ARM_CM3/port.c
    Libraries/FreeRTOS-Kernel/portable/MemMang/heap_4.c
    # Queue、Semaphore和Mutex共用的底层实现。
    Libraries/FreeRTOS-Kernel/queue.c

    # W5500 Ethernet driver.
    Libraries/ioLibrary_Driver-3.2.0/Ethernet/wizchip_conf.c
    Libraries/ioLibrary_Driver-3.2.0/Ethernet/W5500/w5500.c

    # Application-side FreeRTOS integration.
    User/rtos/freertos_hooks.c
    User/rtos/w5500/rtos_w5500_access.c

    # FreeRTOS coreMQTT v2.3.1 (MQTT 3.1.1).
    Libraries/coreMQTT-2.3.1/source/core_mqtt.c
    Libraries/coreMQTT-2.3.1/source/core_mqtt_serializer.c
    Libraries/coreMQTT-2.3.1/source/core_mqtt_state.c
    Libraries/coreMQTT-2.3.1/source/core_json.c

    # Application tasks.
    User/app/led/app_led_task.c
    User/app/key/app_key_task.c
    User/app/serial/app_serial_task.c
    User/app/console/app_console.c
    User/app/diagnostics/app_rtos_diagnostics.c
    User/app/adc/app_adc_task.c
    User/app/can/app_can_task.c
    User/app/startup/app_startup.c
    User/app/network/app_network_task.c
    User/app/network/app_tcp_client.c
    User/app/mqtt/app_mqtt_task.c
    User/app/mqtt/app_mqtt_transport_w5500.c
    User/app/mqtt/app_mqtt_client.c
    User/app/mqtt_business/app_mqtt_business_task.c
    User/app/ftp/app_ftp_client.c
    User/app/ftp/app_ftp_task.c
    User/app/output/app_output_control.c
    User/app/storage/app_storage_crc32.c
    User/app/storage/app_storage_metadata.c
    User/app/storage/app_storage_task.c
    User/app/storage/app_firmware_image.c
    Libraries/MbedTLS-3.6.7/library/md5.c
    Libraries/MbedTLS-3.6.7/library/platform_util.c

    # Board support package.
    User/bsp/led/bsp_led.c
    User/bsp/key/bsp_key.c
    User/bsp/usart/bsp_usart.c
    User/bsp/timer/bsp_run_time_counter.c
    User/bsp/adc/bsp_adc.c
    User/bsp/can/bsp_can.c
    User/bsp/buzzer/bsp_buzzer.c
    User/bsp/board/bsp_board.c
    User/bsp/w5500/bsp_w5500.c
    User/bsp/w25q64/bsp_w25q64.c

    User/main.c
)

# 设置特定源文件的编译选项
set_source_files_properties(
    Libraries/ioLibrary_Driver-3.2.0/Ethernet/wizchip_conf.c
    PROPERTIES
    COMPILE_OPTIONS "-Wno-unused-parameter"
)

# 设置目标属性
set_target_properties(firmware PROPERTIES
    OUTPUT_NAME application
    SUFFIX ".elf"   # 生成的固件文件后缀名为 .elf
)

if(NOT DEFINED ENV{STM32_MQTT_PRIVATE_CONFIG_DIR})
    message(FATAL_ERROR "Set STM32_MQTT_PRIVATE_CONFIG_DIR to the private MQTT config directory")
endif()

target_include_directories(firmware PRIVATE
    Libraries/CMSIS
    Libraries/FWlib/inc
    User
    Libraries/FreeRTOS-Kernel/include
    Libraries/FreeRTOS-Kernel/portable/GCC/ARM_CM3
    
    User/app

    User/bsp/led
    User/app/led

    User/bsp/key
    User/app/key

    User/bsp/usart
    User/app/serial

    User/app/console
    User/app/event
    User/app/diagnostics
    User/app/adc
    
    User/bsp/timer
    User/bsp/adc

    User/bsp/can
    User/app/can

    User/bsp/buzzer
    User/bsp/board
    User/app/startup

    Libraries/ioLibrary_Driver-3.2.0/Ethernet
    Libraries/ioLibrary_Driver-3.2.0/Ethernet/W5500
    User/app/network
    User/bsp/w5500
    User/rtos/w5500

    Libraries/coreMQTT-2.3.1/source/include
    Libraries/coreMQTT-2.3.1/source/interface
    User/app/mqtt
    User/app/mqtt_business
    User/app/ftp

    User/app/output
    
    User/app/storage
    User/bsp/w25q64
    Libraries/MbedTLS-3.6.7/include

    "$ENV{STM32_MQTT_PRIVATE_CONFIG_DIR}"
)

# 定义编译宏，例如 STM32F10X_HD 和 USE_STDPERIPH_DRIVER
target_compile_definitions(firmware PRIVATE
    STM32F10X_HD
    USE_STDPERIPH_DRIVER
    MBEDTLS_CONFIG_FILE="app_storage_mbedtls_config.h"
)

# 给 firmware 这个 target 的“源文件编译过程”
# 添加下面这些 GCC 参数。
target_compile_options(firmware PRIVATE
    -mcpu=cortex-m3         # CPU 是 Cortex-M3
    -mthumb                 # 生成 Thumb 指令
    -mfloat-abi=soft
    -ffunction-sections     # 每个函数尽量单独放一个 section
    -fdata-sections         # 每份数据也尽量单独放 section
    -fno-common             # 不允许全局变量在多个文件中重复定义
    -ffreestanding          # 裸机环境
    -Wa,--noexecstack       # Wa: 传给汇编器的参数 ，让当前工程新生成的单个.o声明不需要可执行栈
    -Wall
    -Wextra
    $<$<COMPILE_LANGUAGE:C>:-std=c11>
    $<$<CONFIG:Debug>:-Og;-g3>
    $<$<CONFIG:Release>:-Os>
)

# .c / .S
#       ↓
# arm-none-eabi-gcc
# + 上面这些参数
#       ↓
# .o


# target_link_options() 设置的就是链接阶段的参数。
target_link_options(firmware PRIVATE
    -mcpu=cortex-m3
    -mthumb
    -mfloat-abi=soft
    -nostartfiles       # 不用 GCC 默认启动文件
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,-z,noexecstack
# 告诉链接器：使用指定的 linker script
# 链接脚本解决的是：
# .text、.data、.bss、栈、堆等最终应该放到 STM32 的哪个地址。
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32F103ZETx_FLASH.ld     # 指定链接脚本，这是 STM32 链接最关键的一项
    -Wl,--gc-sections       # wl:把参数传给链接器，丢弃未使用的函数和数据
    -Wl,--print-memory-usage    # 参数含义：构建结束时打印 Flash/RAM 使用情况
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/application.map,--cref
)

# ELF 生成之后再做额外工作
add_custom_command(TARGET firmware POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:firmware>
            ${CMAKE_CURRENT_BINARY_DIR}/application.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary --gap-fill=0x00
            $<TARGET_FILE:firmware>
            ${CMAKE_CURRENT_BINARY_DIR}/application.bin
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:firmware>
    COMMENT "Generating application.hex and application.bin"
)
