cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM and C languages
enable_language(C ASM)
# STM32CubeMX generated symbols (macros)
set(MX_Defines_Syms 
	FX_INCLUDE_USER_DEFINE_FILE 
	LX_INCLUDE_USER_DEFINE_FILE 
	USE_HAL_DRIVER 
	STM32U545xx
    $<$<CONFIG:Debug>:DEBUG>
)

# STM32CubeMX generated include paths
set(MX_Include_Dirs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../FileX/App
    ${CMAKE_CURRENT_SOURCE_DIR}/../../FileX/Target
    ${CMAKE_CURRENT_SOURCE_DIR}/../../LevelX/App
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Device/ST/STM32U5xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/ports/generic/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/CMSIS/Include
)

# STM32CubeMX generated application sources
set(MX_Application_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../FileX/Target/fx_stm32_sd_driver_glue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../FileX/App/app_filex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32u5xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/stm32u5xx_hal_msp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sysmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/syscalls.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../startup_stm32u545xx.s
)

# STM32 HAL/LL Drivers
set(STM32_Drivers_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/system_stm32u5xx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_cortex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_i2c_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_rcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_rcc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_flash_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_dma_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_exti.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_pwr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_pwr_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_gtzc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_icache.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_ospi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_ll_dlyb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_ll_sdmmc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_sd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_sd_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_mmc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_mmc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_sdio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_uart_ex.c
)

# Drivers Midllewares


set(LevelX_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_block_reclaim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_close.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_defragment.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_driver_block_erase.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_driver_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_driver_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_extended_cache_enable.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_initialize.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_logical_sector_find.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_next_block_to_erase_find.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_open.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_partial_defragment.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_physical_sector_allocate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_sector_mapping_cache_invalidate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_sector_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_sector_release.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_sector_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nor_flash_system_error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_256byte_ecc_check.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_256byte_ecc_compute.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_close.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_defragment.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_driver_block_erase.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_driver_block_erased_verify.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_driver_block_status_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_driver_block_status_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_driver_page_erased_verify.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_extended_cache_enable.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_initialize.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_open.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_page_ecc_check.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_page_ecc_compute.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_partial_defragment.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_sector_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_sector_release.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_sector_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_system_error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_block_allocate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_block_data_move.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_block_find.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_block_mapping_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_block_status_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_data_page_copy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_erase_count_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_format.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_free_block_list_add.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_mapped_block_list_add.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_mapped_block_list_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_mapped_block_list_remove.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_memory_initialize.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_metadata_allocate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_metadata_build.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_metadata_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_sectors_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_sectors_release.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/levelx/common/src/lx_nand_flash_sectors_write.c
)
set(FileX_Src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/drivers/fx_stm32_sd_driver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_attributes_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_attributes_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_create.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_default_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_default_get_copy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_default_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_delete.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_entry_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_entry_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_exFAT_entry_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_exFAT_entry_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_exFAT_free_search.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_exFAT_unicode_entry_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_first_entry_find.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_first_full_entry_find.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_free_search.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_information_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_local_path_clear.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_local_path_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_local_path_get_copy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_local_path_restore.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_local_path_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_long_name_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_long_name_get_extended.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_name_extract.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_name_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_next_entry_find.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_next_full_entry_find.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_rename.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_search.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_short_name_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_directory_short_name_get_extended.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_add_bitmap_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_add_checksum_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_add_dir_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_add_FAT_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_apply_logs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_calculate_checksum.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_cleanup_FAT_chain.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_create_log_file.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_enable.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_read_directory_sector.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_read_FAT.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_read_log_file.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_recover.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_reset_log_file.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_set_FAT_chain.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_transaction_end.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_transaction_fail.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_transaction_start.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_fault_tolerant_write_log_file.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_allocate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_attributes_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_attributes_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_best_effort_allocate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_close.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_create.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_date_time_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_delete.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_extended_allocate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_extended_best_effort_allocate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_extended_relative_seek.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_extended_seek.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_extended_truncate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_extended_truncate_release.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_open.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_relative_seek.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_rename.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_seek.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_truncate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_truncate_release.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_file_write_notify_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_abort.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_boot_info_extract.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_cache_invalidate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_check.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_check_FAT_chain_check.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_check_lost_cluster_check.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_close.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_close_notify_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_exFAT_format.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_extended_space_available.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_flush.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_format.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_format_oem_name_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_format_type_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_format_volume_id_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_open.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_open_notify_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_space_available.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_volume_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_volume_get_extended.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_volume_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_media_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_partition_offset_calculate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_system_date_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_system_date_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_system_initialize.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_system_time_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_system_time_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_system_timer_entry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_directory_create.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_directory_entry_change.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_directory_entry_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_directory_rename.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_directory_search.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_file_create.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_file_rename.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_length_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_length_get_extended.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_name_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_name_get_extended.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_short_name_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_unicode_short_name_get_extended.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_16_unsigned_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_16_unsigned_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_32_unsigned_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_32_unsigned_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_64_unsigned_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_64_unsigned_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_absolute_path_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_allocate_new_cluster.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_bitmap_cache_prepare.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_bitmap_cache_update.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_bitmap_flush.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_bitmap_free_cluster_find.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_bitmap_initialize.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_bitmap_start_sector_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_cluster_free.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_cluster_state_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_cluster_state_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_geometry_check.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_name_hash_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_size_calculate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_system_area_checksum_verify.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_system_area_checksum_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_system_area_format.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_system_sector_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_unicode_name_hash_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_exFAT_upcase_table.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_FAT_entry_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_FAT_entry_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_FAT_flush.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_FAT_map_flush.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_FAT_sector_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_logical_sector_cache_entry_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_logical_sector_flush.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_logical_sector_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_logical_sector_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_memory_copy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_memory_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_string_length_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fx_utility_token_length_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_attributes_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_attributes_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_create.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_default_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_default_get_copy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_default_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_delete.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_first_entry_find.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_first_full_entry_find.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_information_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_local_path_clear.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_local_path_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_local_path_get_copy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_local_path_restore.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_local_path_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_long_name_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_long_name_get_extended.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_name_test.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_next_entry_find.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_next_full_entry_find.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_rename.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_short_name_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_directory_short_name_get_extended.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_fault_tolerant_enable.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_allocate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_attributes_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_attributes_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_best_effort_allocate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_close.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_create.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_date_time_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_delete.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_extended_allocate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_extended_best_effort_allocate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_extended_relative_seek.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_extended_seek.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_extended_truncate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_extended_truncate_release.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_open.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_relative_seek.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_rename.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_seek.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_truncate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_truncate_release.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_file_write_notify_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_abort.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_cache_invalidate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_check.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_close.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_close_notify_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_exFAT_format.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_extended_space_available.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_flush.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_format.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_open.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_open_notify_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_read.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_space_available.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_volume_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_volume_get_extended.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_volume_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_media_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_system_date_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_system_date_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_system_time_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_system_time_set.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_unicode_directory_create.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_unicode_directory_rename.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_unicode_file_create.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_unicode_file_rename.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_unicode_name_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_unicode_name_get_extended.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_unicode_short_name_get.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Middlewares/ST/filex/common/src/fxe_unicode_short_name_get_extended.c
)

# Link directories setup
set(MX_LINK_DIRS

)
# Project static libraries
set(MX_LINK_LIBS 
    STM32_Drivers
    ${TOOLCHAIN_LINK_LIBRARIES}
    LevelX	FileX	
)
# Interface library for includes and symbols
add_library(stm32cubemx INTERFACE)
target_include_directories(stm32cubemx INTERFACE ${MX_Include_Dirs})
target_compile_definitions(stm32cubemx INTERFACE ${MX_Defines_Syms})

# Create STM32_Drivers static library
add_library(STM32_Drivers OBJECT)
target_sources(STM32_Drivers PRIVATE ${STM32_Drivers_Src})
target_link_libraries(STM32_Drivers PUBLIC stm32cubemx)


# Create LevelX static library
add_library(LevelX OBJECT)
target_sources(LevelX PRIVATE ${LevelX_Src})
target_link_libraries(LevelX PUBLIC stm32cubemx)

# Create FileX static library
add_library(FileX OBJECT)
target_sources(FileX PRIVATE ${FileX_Src})
target_link_libraries(FileX PUBLIC stm32cubemx)

# Add STM32CubeMX generated application sources to the project
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${MX_Application_Src})

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${MX_LINK_DIRS})

# Add libraries to the project
target_link_libraries(${CMAKE_PROJECT_NAME} ${MX_LINK_LIBS})

# Add the map file to the list of files to be removed with 'clean' target
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES ADDITIONAL_CLEAN_FILES ${CMAKE_PROJECT_NAME}.map)

# Validate that STM32CubeMX code is compatible with C standard
if((CMAKE_C_STANDARD EQUAL 90) OR (CMAKE_C_STANDARD EQUAL 99))
    message(ERROR "Generated code requires C11 or higher")
endif()
