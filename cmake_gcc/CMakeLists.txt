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
    ../src/utils/src/app_log.c
)

target_include_directories(xbee_provision PUBLIC
    # Add additional include paths here
    ../src/utils/inc
)

target_compile_definitions(xbee_provision PUBLIC
    # Add additional macros here
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