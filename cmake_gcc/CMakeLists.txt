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

add_executable(xbee_provision
    # Add additional sources here
    ../src/app/src/xbee_bringup.c
    ../src/app/src/xbee_dump.c
    ../src/app/src/xbee_provision.c
    ../src/app/src/xbee_provision_table.c
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
    # Which application runs. XBEE_APP_BRINGUP only detects the module and
    # reports what it is, writing nothing, which is the build to use when a
    # board will not talk at all.
    XBEE_APP=XBEE_APP_PROVISION
    # Compile the human-readable AT command names, so the parameter dump can
    # print "CH (Operating Channel)" rather than "CH". Roughly 2 kB of flash.
    #
    # This macro changes the layout of xbee_at_entry_t: it adds a name member
    # under an #if (src/services/inc/xbee_at_table.h). It must therefore be
    # defined on both targets below or on neither, or the two would disagree
    # about the size of the structure without any diagnostic.
    XBEE_AT_TABLE_NAMES=1
)

# app.c is compiled in the generated slc OBJECT library, which does not inherit
# the definitions above, so the application selection is given to it as well.
target_compile_definitions(slc PUBLIC
    XBEE_APP=XBEE_APP_PROVISION
    XBEE_AT_TABLE_NAMES=1
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