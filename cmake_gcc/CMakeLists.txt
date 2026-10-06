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
    ../src/app/src/cli.c
    ../src/app/src/cli_show.c
    ../src/app/src/cli_value.c
    ../src/app/src/xbee_bridge.c
    ../src/app/src/xbee_bridge_escape.c
    ../src/app/src/xbee_bringup.c
    ../src/app/src/xbee_dump.c
    ../src/app/src/xbee_dump_format.c
    ../src/app/src/xbee_provision.c
    ../src/app/src/xbee_provision_table.c
    ../src/app/src/sn_burner.c
    ../src/utils/src/app_log.c
    ../src/utils/src/byte_util.c
    ../src/utils/src/device_sn.c
    ../src/utils/src/line_edit.c
    ../src/utils/src/ring_buffer.c
    ../src/drivers/src/console_uart.c
    ../src/drivers/src/xbee_power.c
    ../src/drivers/src/xbee_reset.c
    ../src/drivers/src/xbee_sleep.c
    ../src/drivers/src/xbee_uart.c
    ../src/services/src/cli_parser.c
    ../src/services/src/nvm_store.c
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
    # board will not talk at all. XBEE_APP_DUMP additionally reads and logs
    # every readable AT parameter, and also writes nothing. XBEE_APP_BRIDGE
    # forwards bytes between VCOM and the module and interprets none of them,
    # so a tool such as XCTU can reach the module through this board; the
    # console build reaches the same bridge from its "bridge" command, so this
    # value is for the case where the console must not be in the way at all.
    #
    # Chosen at configure time with -DXBEE_APP_SELECT=PROVISION, BRINGUP, DUMP,
    # CLI or BRIDGE; without it the build is CLI, as before. tools/provision.py
    # configures the provisioning build this way, in its own build directory.
    # An unknown value stops the build at the #error in app.c.
    #
    # Keep this line identical in BOTH this block and the slc one below: app.c
    # is compiled in the generated slc library and does not inherit these
    # definitions, so a difference selects one application's init and another's
    # process.
    XBEE_APP=XBEE_APP_$<IF:$<STREQUAL:${XBEE_APP_SELECT},>,CLI,${XBEE_APP_SELECT}>

    # Device serial number written by the provisioning build (src/app/inc/
    # sn_burner.h). SN_BURNER_SEQUENCE is the NNNNN part, five digits, and is
    # required by that build; SN_BURNER_BUILD_YYWW is the build host's ISO year
    # and week. Both are given with -D at configure time, normally by
    # tools/provision.py, and are left out entirely when not given. The test is
    # for an empty string rather than $<BOOL:...>, so that 00000 is a value.
    # Both blocks again, because app.c checks SN_BURNER_SEQUENCE.
    $<$<NOT:$<STREQUAL:${SN_BURNER_SEQUENCE},>>:SN_BURNER_SEQUENCE=${SN_BURNER_SEQUENCE}>
    $<$<NOT:$<STREQUAL:${SN_BURNER_BUILD_YYWW},>>:SN_BURNER_BUILD_YYWW=${SN_BURNER_BUILD_YYWW}>

    # Compile the human-readable AT command names, which the parameter dump
    # prints beside each command.
    #
    # This is given to every target on purpose. The macro does not only add
    # strings: the name member of xbee_at_entry_t is declared inside its #if, so
    # two translation units built with different values of it disagree about the
    # size of that struct, and a table entry read across that boundary is
    # silently misinterpreted. There is no warning for it, so the only safe
    # setting is one setting.
    XBEE_AT_TABLE_NAMES=1
)

# app.c is compiled in the generated slc OBJECT library, which does not inherit
# the definitions above, so the application selection is given to it as well.
target_compile_definitions(slc PUBLIC
    XBEE_APP=XBEE_APP_$<IF:$<STREQUAL:${XBEE_APP_SELECT},>,CLI,${XBEE_APP_SELECT}>
    $<$<NOT:$<STREQUAL:${SN_BURNER_SEQUENCE},>>:SN_BURNER_SEQUENCE=${SN_BURNER_SEQUENCE}>
    $<$<NOT:$<STREQUAL:${SN_BURNER_BUILD_YYWW},>>:SN_BURNER_BUILD_YYWW=${SN_BURNER_BUILD_YYWW}>
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