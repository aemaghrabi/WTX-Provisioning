# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(
	xbee_provision
	VERSION 1.0
	LANGUAGES C CXX ASM
)

# Include the definition of the slc_xbee_provision target,
# which contains the content of the SLC project
include(xbee_provision.cmake)

# --- Application build options ----------------------------------------------
# Application-owned block. The cache variables below feed the "Add additional
# sources here" and "Add additional macros here" sections further down; they
# have to be declared before add_executable(), which is why they sit here. They
# read nothing from, and write nothing to, the generated xbee_provision.cmake.

# Which application the image runs and contains. PROVISION configures the
# module and verifies it; BRINGUP only detects the module and reports what it
# is, writing nothing, which is the build to use when a board will not talk at
# all. The unselected application is not compiled into the image.
#
# Set it with the "provision" preset (cmake_gcc/CMakeUserPresets.json) or with
# -DXBEE_APP_SELECT=<value>. The default reproduces the historical build.
set(XBEE_APP_SELECT "PROVISION" CACHE STRING
    "Which application runs: PROVISION or BRINGUP")
set_property(CACHE XBEE_APP_SELECT PROPERTY STRINGS PROVISION BRINGUP)

# STRINGS is a hint for the CMake GUI and enforces nothing, so the value is
# checked here. Failing at configure time beats a confusing compile error.
if(NOT XBEE_APP_SELECT STREQUAL "PROVISION"
   AND NOT XBEE_APP_SELECT STREQUAL "BRINGUP")
    message(FATAL_ERROR
        "XBEE_APP_SELECT is \"${XBEE_APP_SELECT}\". "
        "It must be PROVISION or BRINGUP.")
endif()

# Compile-time floor of the app_log module. Calls below it are removed from the
# build. The names are the level names from src/utils/inc/app_log.h; INFO is
# also what src/utils/inc/app_log_config.h compiles by default, so the default
# here pins current behaviour rather than changing it.
set(XBEE_LOG_LEVEL "INFO" CACHE STRING
    "Lowest log level compiled in: VERBOSE, INFO, WARNING, ERROR or NONE")
set_property(CACHE XBEE_LOG_LEVEL PROPERTY STRINGS
    VERBOSE INFO WARNING ERROR NONE)

set(xbee_log_levels VERBOSE INFO WARNING ERROR NONE)
if(NOT XBEE_LOG_LEVEL IN_LIST xbee_log_levels)
    string(JOIN ", " xbee_log_levels_text ${xbee_log_levels})
    message(FATAL_ERROR
        "XBEE_LOG_LEVEL is \"${XBEE_LOG_LEVEL}\". "
        "It must be one of: ${xbee_log_levels_text}.")
endif()

# Pass the APP_LOG_LEVEL_* macro name, not the integer it expands to, so the
# build holds no second copy of the numbering that could drift from app_log.h.
# This is safe because app_log_config.h, which defines APP_LOG_LEVEL_MIN under
# an #ifndef, is included from app_log.h alone and below the level definitions.
set(xbee_app_defines
    XBEE_APP=XBEE_APP_${XBEE_APP_SELECT}
    APP_LOG_LEVEL_MIN=APP_LOG_LEVEL_${XBEE_LOG_LEVEL}
    # Compile the human-readable AT command names, so the parameter dump can
    # print "CH (Operating Channel)" rather than "CH". Measured at 4008 bytes
    # of flash (docs/history/2026-09-19-xbee-parameter-dump.md).
    #
    # This macro changes the layout of xbee_at_entry_t: it adds a name member
    # under an #if (src/services/inc/xbee_at_table.h). It must therefore be
    # defined on both targets below or on neither, or the two would disagree
    # about the size of the structure without any diagnostic.
    XBEE_AT_TABLE_NAMES=1
)

# Only the selected application is compiled, so the image carries no code for
# the other one. xbee_dump.c is outside the choice on purpose: it is the shared
# boot parameter dump and both applications call it.
if(XBEE_APP_SELECT STREQUAL "BRINGUP")
    set(xbee_app_sources
        ../src/app/src/xbee_bringup.c
    )
else()
    set(xbee_app_sources
        ../src/app/src/xbee_provision.c
        ../src/app/src/xbee_provision_table.c
    )
endif()

message(STATUS
    "xbee_provision: application ${XBEE_APP_SELECT}, log level ${XBEE_LOG_LEVEL}")

# The order of the sources below is the link order, which decides where the
# linker places each function and constant. Changing it changes every address
# in the image without changing its behaviour or its size, so keep xbee_dump.c
# ahead of the selected application: that reproduces the historical order with
# the unselected application removed, and makes the default image byte for byte
# what it was before the selection became a cache variable.
add_executable(xbee_provision
    # Add additional sources here
    ../src/app/src/xbee_dump.c
    ${xbee_app_sources}
    ../src/utils/src/app_log.c
    ../src/utils/src/byte_util.c
    ../src/utils/src/ring_buffer.c
    ../src/drivers/src/xbee_power.c
    ../src/drivers/src/xbee_reset.c
    ../src/drivers/src/xbee_sleep.c
    ../src/drivers/src/xbee_uart.c
    ../src/services/src/xbee.c
    ../src/services/src/xbee_api.c
    ../src/services/src/xbee_at_table.c
    ../src/services/src/xbee_cmd_mode.c
    ../src/services/src/xbee_frame.c
)

target_include_directories(xbee_provision PUBLIC
    # Add additional include paths here
    ../src/utils/inc
    ../src/drivers/inc
    ../src/services/inc
    ../src/app/inc
)

# app.c is compiled in the generated slc OBJECT library, which does not see
# the include paths above. Expose the application layer headers to it as well.
target_include_directories(slc PUBLIC
    ../src/app/inc
    ../src/utils/inc
)

target_compile_definitions(xbee_provision PUBLIC
    # Add additional macros here
    ${xbee_app_defines}
)

# app.c is compiled in the generated slc OBJECT library, which does not inherit
# the definitions above, so the same set is given to it as well. The two must
# not diverge: see the note on XBEE_AT_TABLE_NAMES where xbee_app_defines is
# built.
target_compile_definitions(slc PUBLIC
    ${xbee_app_defines}
)

target_compile_options(xbee_provision PUBLIC
    # Set additional compiler flags here
)

target_link_options(xbee_provision PUBLIC
    # Set additional linker flags here
)

# Link with the content defined in the SLC project
target_link_libraries(xbee_provision PRIVATE
    slc
)

# Include managed project content if available
include(xbee_provision_project.cmake OPTIONAL RESULT_VARIABLE managed_project)
if(managed_project)
    message(STATUS "Using managed project content from ${managed_project}")
endif()

# Force the gcc linker command
set_target_properties(xbee_provision PROPERTIES LINKER_LANGUAGE C)

# Create .bin, .hex and .s37 artifacts after building the project
add_custom_command(TARGET xbee_provision
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:xbee_provision>" "$<TARGET_FILE_DIR:xbee_provision>/$<TARGET_FILE_BASE_NAME:xbee_provision>.s37"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:xbee_provision>" "$<TARGET_FILE_DIR:xbee_provision>/$<TARGET_FILE_BASE_NAME:xbee_provision>.hex"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:xbee_provision>" "$<TARGET_FILE_DIR:xbee_provision>/$<TARGET_FILE_BASE_NAME:xbee_provision>.bin" 
)

# Run post-build pipeline to perform additional post-processing
if(post_build_command)
add_custom_command(TARGET xbee_provision
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)
endif()