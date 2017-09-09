cmake_minimum_required(VERSION 2.8)

project(Demo)

#Preferably add your code in code subdirectory.
# If you want to add code from some other directory, add the _relative_ path below
file(GLOB SRCS
               code/*.c
               Libraries/usr/gdisp_lld_ILI9341.c
               Libraries/usr/ginput_lld_mouse.c
               Libraries/usr/ParTest.c
               Libraries/usr/port.c
               Libraries/usr/timertest.c
               Libraries/usr/system_stm32f4xx.c
               Libraries/usr/ESPL_functions.c
               Libraries/usr/startup_stm32f429_439xx.S
)

# If you need more sources from the FreeRTOS library, add them below
file(GLOB RTOS_SRCS
                    Libraries/FreeRTOS/FreeRTOS/Source/list.c
                    Libraries/FreeRTOS/FreeRTOS/Source/queue.c
                    Libraries/FreeRTOS/FreeRTOS/Source/tasks.c
                    Libraries/FreeRTOS/FreeRTOS/Source/timers.c
                    Libraries/FreeRTOS/FreeRTOS/Source/portable/MemMang/heap_1.c
                    Libraries/FreeRTOS/FreeRTOS/Demo/Common/Minimal/flash.c
)

# If you need more sources from the STMPeripheralDrivers, add them below
file(GLOB PERIPH_SRCS
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/misc.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_dma.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_dma2d.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_fmc.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_i2c.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_ltdc.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_gpio.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_rcc.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_spi.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_usart.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_adc.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_tim.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_exti.c
        Libraries/STM32F4/Libraries/STM32F4xx_StdPeriph_Driver/src/stm32f4xx_syscfg.c
)

file(GLOB UTILITIES_SRCS
        Libraries/STM32F4/Utilities/STM32_EVAL/STM32F429I-Discovery/stm32f429i_discovery_ioe.c
        Libraries/STM32F4/Utilities/STM32_EVAL/STM32F429I-Discovery/stm32f429i_discovery_lcd.c
        Libraries/STM32F4/Utilities/STM32_EVAL/STM32F429I-Discovery/stm32f429i_discovery_sdram.c
)

# If you need more sources from the ugfx library, add them below
file(GLOB UGFX_SRCS
                    Libraries/ugfx/src/gfx.c
                    Libraries/ugfx/src/gdriver/gdriver.c
                    Libraries/ugfx/src/gdisp/gdisp_fonts.c
                    Libraries/ugfx/src/gdisp/gdisp.c
                    Libraries/ugfx/src/gdisp/mcufont/mf_encoding.c
                    Libraries/ugfx/src/gdisp/mcufont/mf_font.c
                    Libraries/ugfx/src/gdisp/mcufont/mf_justify.c
                    Libraries/ugfx/src/gdisp/mcufont/mf_scaledfont.c
                    Libraries/ugfx/src/gdisp/mcufont/mf_rlefont.c
                    Libraries/ugfx/src/gevent/gevent.c
                    Libraries/ugfx/src/ginput/ginput.c
                    Libraries/ugfx/src/ginput/ginput_mouse.c
                    Libraries/ugfx/src/gos/gos_freertos.c
                    Libraries/ugfx/src/gtimer/gtimer.c
                    Libraries/ugfx/src/gwin/gwin_console.c
                    Libraries/ugfx/src/gwin/gwin.c
                    Libraries/ugfx/src/gwin/gwin_wm.c
)

if(NOT TOOLCHAINDIR)
    set(TOOLCHAINDIR /DIST/lehre/lab_espl/tech/Software/ESPToolchain/esptoolchain.cmake)
    message("No TOOLCHAINDIR provided. Using default: " ${TOOLCHAINDIR})
endif(NOT TOOLCHAINDIR)
include(${TOOLCHAINDIR})

set(CONFIG_HDRS ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/usr
                ${CMAKE_CURRENT_SOURCE_DIR}/include)

enable_language(ASM)

include_directories(code)
include_directories(${CONFIG_HDRS})

add_library(usrlib OBJECT ${SRCS})
add_library(rtoslib OBJECT ${RTOS_SRCS})
add_library(stmperipheralslib OBJECT ${PERIPH_SRCS})
add_library(stmutilitieslib OBJECT ${UTILITIES_SRCS})
add_library(ugfxlib OBJECT ${UGFX_SRCS})

add_executable(${PROJECT_NAME}.elf $<TARGET_OBJECTS:usrlib> $<TARGET_OBJECTS:rtoslib> $<TARGET_OBJECTS:stmperipheralslib> $<TARGET_OBJECTS:stmutilitieslib> $<TARGET_OBJECTS:ugfxlib>)

add_custom_target(${PROJECT_NAME}.bin
                  COMMAND ${ARM_OBJCOPY} -O binary ${PROJECT_NAME}.elf ${PROJECT_NAME}.bin
                  COMMAND ${ARM_OBJCOPY} -O ihex ${PROJECT_NAME}.elf ${PROJECT_NAME}.hex
                  COMMAND ${ARM_OBJCOPY} -h -S -D ${PROJECT_NAME}.elf > ${PROJECT_NAME}.lst
                  COMMAND ${ARM_SIZE} ${PROJECT_NAME}.elf
)
add_dependencies(${PROJECT_NAME}.bin ${PROJECT_NAME}.elf)

add_custom_target(flash
                  COMMAND ${ST_FLASH} write ${PROJECT_NAME}.bin 0x8000000
)
add_dependencies(flash ${PROJECT_NAME}.bin)
