# file-format: 1.0.0
if(CMAKE_BUILD_TYPE STREQUAL "debug_GCC_STM32C552CEU6")
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE main.c main.h)
  # USART3 (PB3/PB4) and SPI2 (PB12..PB15) added by hand: not part of the original
  # CubeMX2 selection, so they are not covered by the generated component
  # conditions in generated/hal/STM32_HAL_Driver_codegen.cmake and are listed here
  # instead.
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE generated/hal/mx_usart3.c generated/hal/mx_usart3.h)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE generated/hal/mx_spi2.c generated/hal/mx_spi2.h)
  # stm32c5xx_hal_spi.c itself is also not pulled in automatically for the same
  # reason (SPI was never part of the original component selection).
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE stm32c5xx_drivers/hal/stm32c5xx_hal_spi.c)
  # USB (PCD, device mode - CDC Virtual COM Port, PA11/PA12) added by hand for
  # the same reason as USART3/SPI2 above: not part of the original CubeMX2
  # selection, so not covered by generated/hal/STM32_HAL_Driver_codegen.cmake.
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE generated/hal/mx_usb.c generated/hal/mx_usb.h)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE stm32c5xx_drivers/hal/stm32c5xx_hal_pcd.c)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE stm32c5xx_drivers/hal/stm32c5xx_usb_drd_core.c)
  # usb_cdc.c/.h: hand-written minimal USB CDC-ACM (Virtual COM Port) device
  # class on top of stm32c5xx_hal_pcd.c - there is no ready-made USB Device
  # middleware pack for this MCU/toolchain yet (unlike the HAL drivers
  # themselves), so the class logic (descriptors, control requests, the
  # bulk/interrupt endpoints) is implemented directly for this project.
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE usb_cdc.c usb_cdc.h)
endif()
