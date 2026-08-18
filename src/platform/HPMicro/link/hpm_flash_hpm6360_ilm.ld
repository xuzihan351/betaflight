/*
******************************************************************************
**
**  File        : hpm_flash_hpm6360_ilm.ld
**
**  Abstract    : Linker script for HPM6360 Device with
**                4MByte FLASH, 768KByte RAM (ILM variant)
**
**                Set heap size, stack size and stack location according
**                to application requirements.
**
**  Target      : HPMicro HPM6360
**
**  Environment : RISC-V gcc toolchain
**
******************************************************************************
**
**  Copyright (c) 2022-2026 HPMicro
**  SPDX-License-Identifier: BSD-3-Clause
**
**  Adapted from hpm_sdk/soc/HPM6300/HPM6360/toolchains/gcc/flash_xip.ld
*/
/* Standalone symbol definitions (provided by CMake in HPM SDK builds) */
_stack_size = 64k;
_heap_size = 16k;
_flash_size = 4M;

ENTRY(_start)

STACK_SIZE = _stack_size;
HEAP_SIZE = _heap_size;
__config_start = ORIGIN(XPI_CFG);
__config_end = ORIGIN(XPI_CFG) + LENGTH(XPI_CFG);
_Min_Stack_Size = 16k;

MEMORY
{
    FLASH (rx) : ORIGIN = 0x80000000, LENGTH = _flash_size - 32k
    XPI_CFG (wr) : ORIGIN = 0x80000000 + _flash_size - 32k, LENGTH = 32K
    ILM (wx) : ORIGIN = 0x00000000, LENGTH = 128K
    DLM (w) : ORIGIN = 0x00080000, LENGTH = 128K
    AXI_SRAM (wx) : ORIGIN = 0x01080000, LENGTH = 256K
    AXI_SRAM_NONCACHEABLE (wx) : ORIGIN = 0x010C0000, LENGTH = 256K
    AHB_SRAM (w) : ORIGIN = 0xF0300000, LENGTH = 32k
}

__nor_cfg_option_load_addr__ = ORIGIN(FLASH) + 0x400;
__boot_header_load_addr__ = ORIGIN(FLASH) + 0x1000;
__app_load_addr__ = ORIGIN(FLASH) + 0x3000;
__boot_header_length__ = __boot_header_end__ - __boot_header_start__;
__app_offset__ = __app_load_addr__ - __boot_header_load_addr__;


SECTIONS
{
    .nor_cfg_option __nor_cfg_option_load_addr__ : {
        KEEP(*(.nor_cfg_option))
    } > FLASH

    .boot_header __boot_header_load_addr__ : {
        __boot_header_start__ = .;
        KEEP(*(.boot_header))
        KEEP(*(.fw_info_table))
        KEEP(*(.dc_info))
        __boot_header_end__ = .;
    } > FLASH

    .start __app_load_addr__ : {
        . = ALIGN(8);
        KEEP(*(.start))
        . = ALIGN(8);
    } > FLASH

    __vector_load_addr__ = ADDR(.start) + SIZEOF(.start);
    .vectors ORIGIN(ILM) : AT(__vector_load_addr__) {
        . = ALIGN(8);
        __vector_ram_start__ = .;
        KEEP(*(.vector_table))
        KEEP(*(.isr_vector))
        KEEP(*(.isr_vector.*))
        KEEP(*(.vector_s_table))
        KEEP(*(.isr_s_vector))
        KEEP(*(.isr_s_vector.*))
        . = ALIGN(8);
        __vector_ram_end__ = .;
    } > ILM

    .text (__vector_load_addr__ + SIZEOF(.vectors)) : {
        . = ALIGN(8);
        *(EXCLUDE_FILE (*io_hpmicro.o *system_hpmicro.o *timer_hpmicro.o *timer_hw_ext.o *dma_hpmicro.o *bus_spi_hpmicro.o *exti_hpmicro.o *adc_hpmicro.o *pwm_output_hpmicro.o *pwm_output_dshot.o *pwm_output_dshot_shared.o *dshot_dpwm.o *trgm_dshot_resource.o *dma_reqmap_mcu.o *serial_uart_hpmicro.o *timer_hpm6360.o *timer_hpm6750.o *pid.o *mixer.o *imu.o *rpm_filter.o *dyn_notch_filter.o *failsafe.o *interpolated_setpoint.o *feedforward.o *rc_smoothing.o *crash_recovery.o *launch_control.o *filter.o *sdft.o *scheduler.o *rx.o *gyro.o *fc/core.o *rc.o *tasks.o *rc_adjustments.o *rc_controls.o *rc_modes.o *dispatch.o *maths.o *vector.o *explog_approx.o *sensor_alignment.o *time.o *typeconversion.o *bitarray.o *acceleration.o *gyro_filter_impl.o *sensors.o *mavlink.o *autopilot_multirotor.o *position.o *position_estimator.o *alt_hold_multirotor.o *compass.o *dshot.o *dshot_command.o *motor.o *pwm_output.o) .text)
        *(EXCLUDE_FILE (*io_hpmicro.o *system_hpmicro.o *timer_hpmicro.o *timer_hw_ext.o *dma_hpmicro.o *bus_spi_hpmicro.o *exti_hpmicro.o *adc_hpmicro.o *pwm_output_hpmicro.o *pwm_output_dshot.o *pwm_output_dshot_shared.o *dshot_dpwm.o *trgm_dshot_resource.o *dma_reqmap_mcu.o *serial_uart_hpmicro.o *timer_hpm6360.o *timer_hpm6750.o *pid.o *mixer.o *imu.o *rpm_filter.o *dyn_notch_filter.o *failsafe.o *interpolated_setpoint.o *feedforward.o *rc_smoothing.o *crash_recovery.o *launch_control.o *filter.o *sdft.o *scheduler.o *rx.o *gyro.o *fc/core.o *rc.o *tasks.o *rc_adjustments.o *rc_controls.o *rc_modes.o *dispatch.o *maths.o *vector.o *explog_approx.o *sensor_alignment.o *time.o *typeconversion.o *bitarray.o *acceleration.o *gyro_filter_impl.o *sensors.o *mavlink.o *autopilot_multirotor.o *position.o *position_estimator.o *alt_hold_multirotor.o) .text*)
        *(.rodata)
        *(.rodata*)
        *(.srodata)
        *(.srodata*)

        *(.hash)
        *(.dyn*)
        *(.gnu*)
        *(.pl*)

        KEEP (*(.init))
        KEEP (*(.fini))

        /* section information for usbh class */
        . = ALIGN(8);
        __usbh_class_info_start__ = .;
        KEEP(*(.usbh_class_info))
        __usbh_class_info_end__ = .;
        . = ALIGN(8);
        __pg_registry_start = .;
        KEEP(*(.pg_registry))
        KEEP(*(.pg_registry.*))
        __pg_registry_end = .;
        . = ALIGN(8);

        . = ALIGN(8);
        __pg_resetdata_start = .;
        KEEP(*(.pg_resetdata))
        KEEP(*(.pg_resetdata.*))
        __pg_resetdata_end = .;
        . = ALIGN(8);

        /* RT-Thread related sections - Start */
        /* section information for finsh shell */
        . = ALIGN(4);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;
        . = ALIGN(4);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;
        . = ALIGN(4);

        . = ALIGN(4);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;
        . = ALIGN(4);

        /* section information for modules */
        . = ALIGN(4);
        __rtmsymtab_start = .;
        KEEP(*(RTMSymTab))
        __rtmsymtab_end = .;

        /* RT-Thread related sections - end */
        . = ALIGN(8);
    } > FLASH

    .eh_frame :
    {
        __eh_frame_start = .;
        KEEP(*(.eh_frame))
        __eh_frame_end = .;
    }  > FLASH

    .eh_frame_hdr :
    {
        KEEP(*(.eh_frame_hdr))
    }  > FLASH
    __eh_frame_hdr_start = SIZEOF(.eh_frame_hdr) > 0 ? ADDR(.eh_frame_hdr) : 0;
    __eh_frame_hdr_end = SIZEOF(.eh_frame_hdr) > 0 ? . : 0;

    .rel : {
        KEEP(*(.rel*))
    } > FLASH

    . = ALIGN(8);
    PROVIDE (__etext = .);
    PROVIDE (_etext = .);
    PROVIDE (etext = .);

    __data_load_addr__ = etext;
    .data : AT(__data_load_addr__) {
        . = ALIGN(8);
        __data_start__ = .;
        __global_pointer$ = . + 0x800;
        *(.data)
        *(.data*)
        *(.sdata)
        *(.sdata*)

        KEEP(*(.jcr))
        KEEP(*(.dynamic))
        KEEP(*(.got*))
        KEEP(*(.got))
        KEEP(*(.gcc_except_table))
        KEEP(*(.gcc_except_table.*))

        . = ALIGN(8);
        PROVIDE(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE(__preinit_array_end = .);

        . = ALIGN(8);
        PROVIDE(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__init_array_end = .);

        . = ALIGN(8);
        PROVIDE(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE(__fini_array_end = .);

        . = ALIGN(8);
        KEEP(*crtbegin*.o(.ctors))
        KEEP(*(EXCLUDE_FILE (*crtend*.o) .ctors))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))

        . = ALIGN(8);
        KEEP(*crtbegin*.o(.dtors))
        KEEP(*(EXCLUDE_FILE (*crtend*.o) .dtors))
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))
        . = ALIGN(8);
        __data_end__ = .;
        PROVIDE (__edata = .);
        PROVIDE (_edata = .);
        PROVIDE (edata = .);
    } > AXI_SRAM

    __fast_load_addr__ = etext + SIZEOF(.data);
    .fast : AT(__fast_load_addr__) {
        . = ALIGN(8);
        PROVIDE(__ramfunc_start__ = .);
        *(.fast)
        *(.fast.*)
        /* HPMicro platform hot-path code: entire .text from these files
           (compiled without LTO via NOT_OPTIMISED_SRC so the linker can
           identify them).  MUST match the EXCLUDE_FILE list in .text above. */
        *io_hpmicro.o(.text .text.*)
        *system_hpmicro.o(.text .text.*)
        *timer_hpmicro.o(.text .text.*)
        *timer_hw_ext.o(.text .text.*)
        *dma_hpmicro.o(.text .text.*)
        *bus_spi_hpmicro.o(.text .text.*)
        *exti_hpmicro.o(.text .text.*)
        *adc_hpmicro.o(.text .text.*)
        *pwm_output_hpmicro.o(.text .text.*)
        *pwm_output_dshot.o(.text .text.*)
        *pwm_output_dshot_shared.o(.text .text.*)
        *dshot_dpwm.o(.text .text.*)
        *trgm_dshot_resource.o(.text .text.*)
        *dma_reqmap_mcu.o(.text .text.*)
        *serial_uart_hpmicro.o(.text .text.*)
        *timer_hpm6360.o(.text .text.*)
        *timer_hpm6750.o(.text .text.*)
        /* Betaflight common hot-path code (flight controller core) */
        *pid.o(.text .text.*)
        *mixer.o(.text .text.*)
        *imu.o(.text .text.*)
        *rpm_filter.o(.text .text.*)
        *dyn_notch_filter.o(.text .text.*)
        *failsafe.o(.text .text.*)
        *interpolated_setpoint.o(.text .text.*)
        *feedforward.o(.text .text.*)
        *rc_smoothing.o(.text .text.*)
        *crash_recovery.o(.text .text.*)
        *launch_control.o(.text .text.*)
        *filter.o(.text .text.*)
        *sdft.o(.text .text.*)
        *scheduler.o(.text .text.*)
        *rx.o(.text .text.*)
        *gyro.o(.text .text.*)
        *fc/core.o(.text .text.*)
        *rc.o(.text .text.*)
        *tasks.o(.text .text.*)
        *rc_adjustments.o(.text .text.*)
        *rc_controls.o(.text .text.*)
        *rc_modes.o(.text .text.*)
        *dispatch.o(.text .text.*)
        *maths.o(.text .text.*)
        *vector.o(.text .text.*)
        *explog_approx.o(.text .text.*)
        *sensor_alignment.o(.text .text.*)
        *time.o(.text .text.*)
        *typeconversion.o(.text .text.*)
        *bitarray.o(.text .text.*)
        *acceleration.o(.text .text.*)
        *gyro_filter_impl.o(.text .text.*)
        *sensors.o(.text .text.*)
        *mavlink.o(.text .text.*)
        *autopilot_multirotor.o(.text .text.*)
        *position.o(.text .text.*)
        *position_estimator.o(.text .text.*)
        *alt_hold_multirotor.o(.text .text.*)
        *compass.o(.text .text.*)
        *dshot.o(.text .text.*)
        *dshot_command.o(.text .text.*)
        *motor.o(.text .text.*)
        *pwm_output.o(.text .text.*)
        . = ALIGN(8);
        PROVIDE(__ramfunc_end__ = .);
    } > ILM

    __tdata_load_addr__ = etext + SIZEOF(.data) + SIZEOF(.fast);
    .tdata : AT(__tdata_load_addr__) {
        . = ALIGN(8);
        PROVIDE(__tdata_start__ = .);
        *(.tdata)
        *(.tdata.*)
        *(.gnu.linkonce.td.*)
        . = ALIGN(8);
        PROVIDE(__tdata_end__ = .);
    } > AXI_SRAM

    .tbss (NOLOAD) : {
        . = ALIGN(8);
        PROVIDE(__tbss_start__ = .);
        __thread_pointer$ = .;
        *(.tbss)
        *(.tbss.*)
        *(.gnu.linkonce.tb.*)
        *(.tcommon)
        . = ALIGN(8);
        PROVIDE(__tbss_end__ = .);
    } > AXI_SRAM

    __noncacheable_init_load_addr__ = etext + SIZEOF(.data) + SIZEOF(.fast) + SIZEOF(.tdata);
    .noncacheable.init : AT(__noncacheable_init_load_addr__) {
        . = ALIGN(8);
        __noncacheable_init_start__ = .;
        KEEP(*(.noncacheable.init))
        KEEP(*(.noncacheable.init.*))
        __noncacheable_init_end__ = .;
        . = ALIGN(8);
    } > AXI_SRAM_NONCACHEABLE

    __fast_ram_init_load_addr__ = etext + SIZEOF(.data) + SIZEOF(.fast) + SIZEOF(.tdata) + SIZEOF(.noncacheable.init);
    .fast_ram.init : AT(__fast_ram_init_load_addr__) {
        . = ALIGN(8);
        __fast_ram_init_start__ = .;
        KEEP(*(.fast_ram.init))
        KEEP(*(.fast_ram.init.*))
        __fast_ram_init_end__ = .;
        . = ALIGN(8);
    } > DLM

    .bss (NOLOAD) : {
        . = ALIGN(8);
        __bss_start__ = .;
        *(.bss)
        *(.bss*)
        *(.sbss*)
        *(.scommon)
        *(.scommon*)
        *(.dynsbss*)
        *(COMMON)
        . = ALIGN(8);
        _end = .;
        PROVIDE(end = .);
        __bss_end__ = .;
    } > AXI_SRAM

    .framebuffer (NOLOAD) : {
        . = ALIGN(8);
        KEEP(*(.framebuffer))
        KEEP(*(.framebuffer.*))
        . = ALIGN(8);
    } > AXI_SRAM

    .noncacheable (NOLOAD) : {
        . = ALIGN(8);
        KEEP(*(.noncacheable))  /* Legacy sections, kept for backwards compatibility */
        KEEP(*(.noncacheable.non_init))
        KEEP(*(.noncacheable.non_init.*))
        __noncacheable_bss_start__ = .;
        KEEP(*(.noncacheable.bss))
        KEEP(*(.noncacheable.bss.*))
        __noncacheable_bss_end__ = .;
        . = ALIGN(8);
    } > AXI_SRAM_NONCACHEABLE

    .ahb_sram (NOLOAD) : {
        KEEP(*(.ahb_sram))
        KEEP(*(.ahb_sram.*))
    } > AHB_SRAM

    .fast_ram (NOLOAD) : {
        . = ALIGN(8);
        KEEP(*(.fast_ram))  /* Legacy sections, kept for backwards compatibility */
        KEEP(*(.fast_ram.non_init))
        KEEP(*(.fast_ram.non_init.*))
        __fast_ram_bss_start__ = .;
        KEEP(*(.fast_ram.bss))
        KEEP(*(.fast_ram.bss.*))
        __fast_ram_bss_end__ = .;
        . = ALIGN(8);
    } > DLM

    .heap (NOLOAD) : {
        . = ALIGN(8);
        __heap_start__ = .;
        . += HEAP_SIZE;
        __heap_end__ = .;
    } > DLM

    .stack (NOLOAD) : {
        . = ALIGN(16);
        __stack_base__ = .;
        . += STACK_SIZE;
        . = ALIGN(16);
        PROVIDE (_stack = .);
        PROVIDE (_stack_safe = .);
        PROVIDE (_estack = .);
    } > DLM

    __noncacheable_start__ = ORIGIN(AXI_SRAM_NONCACHEABLE);
    __noncacheable_end__ = ORIGIN(AXI_SRAM_NONCACHEABLE) + LENGTH(AXI_SRAM_NONCACHEABLE);

    __fw_size__ = SIZEOF(.start) + SIZEOF(.vectors) + SIZEOF(.rel) + SIZEOF(.text) + SIZEOF(.data) + SIZEOF(.fast) + SIZEOF(.tdata) + SIZEOF(.noncacheable.init) + SIZEOF(.fast_ram.init) + SIZEOF(.eh_frame) + SIZEOF(.eh_frame_hdr);
    __last_addr__ = __fast_ram_init_load_addr__ + SIZEOF(.fast_ram.init);
    ASSERT(((__fw_size__ <= LENGTH(FLASH)) && (__last_addr__ <= (ORIGIN(FLASH) + LENGTH(FLASH)))), "******  FAILED! FLASH does not have enough space!  ******")
}
