/* STM32F103ZET6: 448 KiB Flash, 64 KiB SRAM. */

OUTPUT_FORMAT("elf32-littlearm")    /* 表示链接器最终生成格式 */
OUTPUT_ARCH(arm)        /* 程序的目标架构 */
ENTRY(Reset_Handler)    /* ELF 文件的入口符号 */

/* FLASH 和 RAM 给内存区域起的名字  r = Read，可读  x = Execute，可执行  ORIGIN：起源*/
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08010000, LENGTH = 448K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);    /* _estack链接器符号,计算栈顶地址 */
_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x800;

__app_image_start = ORIGIN(FLASH);

SECTIONS
{
    .firmware_header ORIGIN(FLASH) :
    {
        FILL(0x00)                      /* 后续空隙用 0 填充 */
        KEEP(*(.appinfo.hardware))      /* 放进.appinfo.hardware段 */
        KEEP(*(.appinfo.software))      /* 接着放入.appinfo.software */
        LONG(__app_image_size)          /* LONG(...) 是 GNU 链接脚本的写数据命令，写成 4 个字节 */
        . = 0x200;                      /* 将本段扩展到 0x200＝512 字节 */
    } > FLASH

    /* 冒号左边的 定义一个“输出段（output section,括号里面表示收集所有 .o 文件里的 .isr_vector “输入段（input section）”。 */
    .isr_vector (ORIGIN(FLASH) + 0x200) :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        *(.RamFunc)
        *(.RamFunc*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

__app_image_end = LOADADDR(.data) + SIZEOF(.data);
__app_image_size = __app_image_end - __app_image_start;

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > RAM

    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > RAM

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
    }
}

ASSERT(SIZEOF(.firmware_header) == 0x200,
       "firmware header must be 512 bytes")
ASSERT(ADDR(.isr_vector) == ORIGIN(FLASH) + 0x200,
       "application vector address mismatch")
ASSERT((ADDR(.isr_vector) & 0x1FF) == 0,
       "application vector must be 0x200 aligned")
ASSERT(__app_image_size <= LENGTH(FLASH),
       "application image exceeds 448 KiB")

