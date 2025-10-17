cmake_minimum_required(VERSION 3.16)

# Make CMake's try-compile only build a static object (no link => no _write/_sbrk needed)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# --- Bootstrap the Pico SDK BEFORE project() ---
include(FetchContent)
if(NOT DEFINED ENV{PICO_SDK_PATH})
  message(STATUS "PICO_SDK_PATH not set; fetching pico-sdk 2.1.0...")
  FetchContent_Declare(
    pico_sdk
    GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk.git
    GIT_TAG 2.1.0
  )
  FetchContent_MakeAvailable(pico_sdk)
  set(PICO_SDK_PATH "${pico_sdk_SOURCE_DIR}")
else()
  set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Tell the SDK we target Pico 2 W (ARM core); you can override with -DPICO_PLATFORM=...
if(NOT DEFINED PICO_PLATFORM)
  set(PICO_PLATFORM rp2350-arm-s)
endif()

project(pico2w_csync_step1 C CXX ASM)
pico_sdk_init()

add_executable(pico2w_csync_step1
  main.c
)

target_link_libraries(pico2w_csync_step1
  pico_stdlib
  hardware_pio
  hardware_clocks
)

# Make sure the target links to an .elf, not a suffix-less binary
set_target_properties(pico2w_csync_step1 PROPERTIES SUFFIX ".elf")

# USB if TinyUSB is present; otherwise UART
if (EXISTS "${PICO_SDK_PATH}/lib/tinyusb")
  pico_enable_stdio_usb(pico2w_csync_step1 1)
  pico_enable_stdio_uart(pico2w_csync_step1 0)
else()
  pico_enable_stdio_usb(pico2w_csync_step1 0)
  pico_enable_stdio_uart(pico2w_csync_step1 1)
endif()

pico_add_extra_outputs(pico2w_csync_step1)
