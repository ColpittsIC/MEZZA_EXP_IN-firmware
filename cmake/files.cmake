# file-format: 1.0.0
if(CMAKE_BUILD_TYPE STREQUAL "debug_GCC_STM32C552CEU6")
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE main.c main.h)
  # USART3 (PB3/PB4) added by hand: not part of the original CubeMX2 selection,
  # so it is not covered by the generated component conditions in
  # generated/hal/STM32_HAL_Driver_codegen.cmake and is listed here instead.
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE generated/hal/mx_usart3.c generated/hal/mx_usart3.h)
endif()
