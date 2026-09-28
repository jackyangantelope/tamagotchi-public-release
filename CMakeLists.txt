cmake_minimum_required(VERSION 3.20)

set(PICO_BOARD adafruit_fruit_jam CACHE STRING "Adafruit Fruit Jam" FORCE)
set(PICO_PSRAM_CS_PIN 47 CACHE STRING "Fruit Jam PSRAM chip select" FORCE)
set(PICO_PSRAM_SIZE_BYTES 0x800000 CACHE STRING "Fruit Jam PSRAM size" FORCE)
set(PICO_DEFAULT_UART 0 CACHE STRING "Diagnostic UART" FORCE)
set(PICO_DEFAULT_UART_TX_PIN 44 CACHE STRING "Diagnostic UART TX" FORCE)
set(PICO_DEFAULT_UART_RX_PIN 45 CACHE STRING "Diagnostic UART RX" FORCE)

if(NOT DEFINED PICO_SDK_PATH)
    message(FATAL_ERROR "Set PICO_SDK_PATH to Pico SDK 2.3.0 or newer")
endif()
set(PICO_SHARED_PATH "${CMAKE_CURRENT_LIST_DIR}/deps/pico_shared" CACHE PATH "pico_shared checkout")
if(NOT DEFINED PICO_PIO_USB_PATH)
    message(FATAL_ERROR "Set PICO_PIO_USB_PATH to a Pico-PIO-USB checkout")
endif()

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
project(picoTamagotchi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

set(TAMALIB_DIR "${CMAKE_CURRENT_LIST_DIR}/deps/tamalib")
add_library(tamalib_core STATIC
    "${TAMALIB_DIR}/cpu.c"
    "${TAMALIB_DIR}/hw.c"
    "${TAMALIB_DIR}/tamalib.c")
target_include_directories(tamalib_core PUBLIC
    "${TAMALIB_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/deps")

add_library(tama_port STATIC "${CMAKE_CURRENT_LIST_DIR}/platform/tama/tama_port.c")
target_include_directories(tama_port PUBLIC "${CMAKE_CURRENT_LIST_DIR}/platform/tama")
target_link_libraries(tama_port PUBLIC tamalib_core)

set(JACK_WAVE1_HDMI_AUDIO ON)
set(JACK_WAVE1_HDMI_DRIVER_ROOT "${PICO_SHARED_PATH}/drivers/pico_hdmi")
set(JACK_WAVE1_HDMI_ADAPTER "${CMAKE_CURRENT_LIST_DIR}/platform/wave1/hdmi/tama_hdmi.c")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/platform/wave1" "${CMAKE_BINARY_DIR}/wave1")
target_compile_definitions(jack_wave1_runtime PRIVATE JACK_WAVE1_QUIET_INPUT_LOG=1)

option(BUILD_FOR_BOOTLOADER "Link to the resident loader application partition" ON)
add_executable(picoTamagotchi "${CMAKE_CURRENT_LIST_DIR}/app/playable_main.cpp")
target_include_directories(picoTamagotchi PRIVATE "${CMAKE_CURRENT_LIST_DIR}/app")
pico_set_program_name(picoTamagotchi "picoTamagotchi")
pico_set_program_version(picoTamagotchi "0.4.0-fruit-jam-candidate")
pico_enable_stdio_uart(picoTamagotchi 1)
pico_enable_stdio_usb(picoTamagotchi 0)
target_compile_definitions(picoTamagotchi PRIVATE
    HW_CONFIG=8 PICO_STACK_SIZE=4096 PICO_CORE1_STACK_SIZE=4096
    JACK_WAVE1_PERF_INTERVAL_US=5000000u JACK_WAVE1_QUIET_INPUT_LOG=1)
target_compile_options(picoTamagotchi PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(picoTamagotchi PRIVATE pico_stdlib tama_port jack_wave1_runtime)
target_link_options(picoTamagotchi PRIVATE -Wl,--gc-sections -Wl,--print-memory-usage)
if(BUILD_FOR_BOOTLOADER)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/LoaderPartition.cmake")
    tama_link_for_loader(picoTamagotchi)
endif()
pico_add_extra_outputs(picoTamagotchi)
