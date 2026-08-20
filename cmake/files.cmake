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
endif()
