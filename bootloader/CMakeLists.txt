cmake_minimum_required(VERSION 3.22)    # 这个工程要求 CMake 至少是 3.22 版本
project(stm32f103_bootloader LANGUAGES C ASM)   # 定义工程名称为 stm32f103_bootloader，使用 C 和 ASM 语言

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)  # CMAKE_SOURCE_DIR 源码目录，CMAKE_BINARY_DIR 构建目录
    message(FATAL_ERROR "Use an out-of-source build: cmake -S bootloader -B bootloader/build")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)       # CMake 生成 compile_commands.json

add_executable(bootloader
    # 启动文件、芯片库和MD5均为本工程自己的文件。
    Libraries/CMSIS/startup/gcc/startup_stm32f103xe.s
    Libraries/CMSIS/system_stm32f10x.c
    Libraries/FWlib/src/stm32f10x_gpio.c
    Libraries/FWlib/src/stm32f10x_rcc.c
    Libraries/FWlib/src/stm32f10x_spi.c
    Libraries/FWlib/src/stm32f10x_flash.c
    
    Libraries/MbedTLS-3.6.7/library/md5.c
    Libraries/MbedTLS-3.6.7/library/platform_util.c

    User/bsp/w25q64/bsp_w25q64.c
    User/bsp/flash/bsp_internal_flash.c
    User/app/boot/boot_controller.c
    User/app/boot/boot_jump.c
    User/main.c
)
set_target_properties(bootloader PROPERTIES     # 给 bootloader 这个目标设置属性
    OUTPUT_NAME bootloader
    SUFFIX ".elf"   # 设置生成的可执行文件后缀为 .elf
    # Unix Makefiles 下，修改链接脚本也必须触发重新链接。
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32F103ZETx_BOOT.ld"   # 设置链接脚本依赖，确保修改后重新链接
)
target_include_directories(bootloader PRIVATE   # 给 bootloader 添加头文件搜索目录
    Libraries/CMSIS
    Libraries/FWlib/inc
    Libraries/MbedTLS-3.6.7/include
    User
    User/bsp/w25q64
    User/bsp/flash
    User/app/boot
)
target_compile_definitions(bootloader PRIVATE       # 添加预处理宏定义
    STM32F10X_HD
    USE_STDPERIPH_DRIVER
    "MBEDTLS_CONFIG_FILE=\"boot_mbedtls_config.h\""
)
target_compile_options(bootloader PRIVATE   # 设置编译选项
    -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
    -ffunction-sections -fdata-sections -fno-common -ffreestanding
    -Wa,--noexecstack -Wall -Wextra
    $<$<COMPILE_LANGUAGE:C>:-std=c11>
    "$<$<CONFIG:Debug>:-Og;-g3>"
    $<$<CONFIG:Release>:-Os>
)
target_link_options(bootloader PRIVATE   # 设置链接选项
    -mcpu=cortex-m3 -mthumb -mfloat-abi=soft
    -nostartfiles --specs=nano.specs --specs=nosys.specs
    -Wl,-z,noexecstack
    "-T${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32F103ZETx_BOOT.ld"
    -Wl,--gc-sections -Wl,--print-memory-usage
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/bootloader.map,--cref"
)
add_custom_command(TARGET bootloader POST_BUILD   # 自定义命令，在构建后生成 hex 和 bin 文件
    COMMAND "${CMAKE_OBJCOPY}" -O ihex $<TARGET_FILE:bootloader>
            "${CMAKE_CURRENT_BINARY_DIR}/bootloader.hex"
    COMMAND "${CMAKE_OBJCOPY}" -O binary --gap-fill=0x00 $<TARGET_FILE:bootloader>
            "${CMAKE_CURRENT_BINARY_DIR}/bootloader.bin"
    COMMAND "${CMAKE_SIZE}" --format=berkeley $<TARGET_FILE:bootloader>
    COMMENT "Generating bootloader.hex and bootloader.bin"
    VERBATIM
)