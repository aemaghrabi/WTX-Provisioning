# Host-side unit tests for the hardware-independent modules of xbee_provision.
#
# These build with the host compiler, never with the ARM toolchain, and are not
# part of the firmware image. Only modules that depend on nothing beyond
# sl_status.h and the C standard library can be tested here.
#
# Usage:
#   cmake -S test -B test/build
#   cmake --build test/build
#   ctest --test-dir test/build --output-on-failure
#
# SDK_PATH points at the Simplicity SDK, for sl_status.h. It is detected from
# the generated cmake_gcc/xbee_provision.cmake when that file exists, and can
# always be overridden with -DSDK_PATH=/path/to/sdk.

cmake_minimum_required(VERSION "3.25")

project(
    xbee_provision_tests
    VERSION 1.0
    LANGUAGES C
)

set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

# Locate the SDK for sl_status.h.
if(NOT DEFINED SDK_PATH OR SDK_PATH STREQUAL "")
    set(_generated_cmake "${PROJECT_ROOT}/cmake_gcc/xbee_provision.cmake")
    if(EXISTS "${_generated_cmake}")
        file(STRINGS "${_generated_cmake}" _sdk_line REGEX "^set\\(SDK_PATH ")
        if(_sdk_line)
            string(REGEX REPLACE "^set\\(SDK_PATH +\"([^\"]+)\".*$" "\\1" SDK_PATH "${_sdk_line}")
        endif()
    endif()
endif()

if(NOT SDK_PATH OR NOT EXISTS "${SDK_PATH}/platform/common/inc/sl_status.h")
    message(FATAL_ERROR
        "sl_status.h not found. Generate the project in Simplicity Studio once, "
        "or pass -DSDK_PATH=/path/to/simplicity_sdk. Tried: '${SDK_PATH}'")
endif()

message(STATUS "Using SDK headers from ${SDK_PATH}")

enable_testing()

# One executable per test file. Each links only the modules it exercises, so a
# test never drags in an unrelated dependency.
function(add_module_test name)
    add_executable(${name} ${name}.c ${ARGN})
    target_include_directories(${name} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}"
        "${PROJECT_ROOT}/src/utils/inc"
        "${PROJECT_ROOT}/src/drivers/inc"
        "${PROJECT_ROOT}/src/services/inc"
        "${SDK_PATH}/platform/common/inc"
        "${SDK_PATH}/platform/service/sleeptimer/inc"
    )
    target_compile_options(${name} PRIVATE
        -std=c11 -Wall -Wextra -Werror -g -O1
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_module_test(test_ring_buffer "${PROJECT_ROOT}/src/utils/src/ring_buffer.c")
add_module_test(test_byte_util "${PROJECT_ROOT}/src/utils/src/byte_util.c")
add_module_test(test_xbee_frame
    "${PROJECT_ROOT}/src/services/src/xbee_frame.c"
    "${PROJECT_ROOT}/src/utils/src/byte_util.c")
add_module_test(test_xbee_at_table
    "${PROJECT_ROOT}/src/services/src/xbee_at_table.c"
    "${PROJECT_ROOT}/src/utils/src/byte_util.c")

# The provisioning configuration is checked against the command table, so a
# value the module would reject fails here rather than on the board.
add_module_test(test_xbee_provision_table
    "${PROJECT_ROOT}/src/app/src/xbee_provision_table.c"
    "${PROJECT_ROOT}/src/services/src/xbee_at_table.c"
    "${PROJECT_ROOT}/src/utils/src/byte_util.c")
target_include_directories(test_xbee_provision_table PRIVATE
    "${PROJECT_ROOT}/src/app/inc"
    "${PROJECT_ROOT}/src/app/src"
)

# The transports need a clock and a serial link. fake_platform.c stands in for
# the sleeptimer and for src/drivers/src/xbee_uart.c, so the request tracking
# and the Command mode state machine can be driven deterministically.
add_module_test(test_xbee_api
    "${PROJECT_ROOT}/src/services/src/xbee_api.c"
    "${PROJECT_ROOT}/src/services/src/xbee_frame.c"
    "${PROJECT_ROOT}/src/utils/src/byte_util.c"
    fake_platform.c)
add_module_test(test_xbee_cmd_mode
    "${PROJECT_ROOT}/src/services/src/xbee_cmd_mode.c"
    "${PROJECT_ROOT}/src/utils/src/byte_util.c"
    fake_platform.c)

# The facade decides which transport carries a request and when. Its sequencing
# is where the defects that reached hardware lived, so it is driven here against
# a simulated module, with fake_drivers.c standing in for the control lines and
# the log.
add_module_test(test_xbee_facade
    "${PROJECT_ROOT}/src/services/src/xbee.c"
    "${PROJECT_ROOT}/src/services/src/xbee_api.c"
    "${PROJECT_ROOT}/src/services/src/xbee_at_table.c"
    "${PROJECT_ROOT}/src/services/src/xbee_cmd_mode.c"
    "${PROJECT_ROOT}/src/services/src/xbee_frame.c"
    "${PROJECT_ROOT}/src/utils/src/byte_util.c"
    fake_platform.c
    fake_drivers.c)
