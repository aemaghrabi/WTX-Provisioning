set(CMAKE_SYSTEM_NAME                   Generic)
set(CMAKE_SYSTEM_PROCESSOR              arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE       STATIC_LIBRARY)

if(WIN32)
  set(EXE_SUFFIX ".exe")
else()
  set(EXE_SUFFIX "")
endif()

# Windows: locate slt-installed tools under the current user's profile, so this file is not tied
# to one user name or to the per-machine Conan package hashes. Environment variables
# (ARM_GCC_DIR, POST_BUILD_EXE, NINJA_EXE_PATH) still take precedence.
if(WIN32)
  file(TO_CMAKE_PATH "$ENV{USERPROFILE}/.silabs/slt/installs" SLT_INSTALLS_DIR)
  set(SLT_GCC_VERSION "12.2.1")
endif()

if(DEFINED ENV{ARM_GCC_DIR})
  set(TOOLCHAIN_DIR "$ENV{ARM_GCC_DIR}/bin/")
elseif(WIN32)
  # Select the GCC package by version: several GCC versions may be installed side by side.
  file(GLOB SLT_GCC_VERSION_DIRS LIST_DIRECTORIES true
    "${SLT_INSTALLS_DIR}/conan/p/gcc-*/p/lib/gcc/arm-none-eabi/${SLT_GCC_VERSION}")
  if(NOT SLT_GCC_VERSION_DIRS)
    message(FATAL_ERROR
      "arm-none-eabi-gcc ${SLT_GCC_VERSION} not found under "
      "${SLT_INSTALLS_DIR}/conan/p/gcc-*/p. Install it with "
      "'slt install gcc-arm-none-eabi/${SLT_GCC_VERSION}' or set ARM_GCC_DIR.")
  endif()
  list(GET SLT_GCC_VERSION_DIRS 0 SLT_GCC_VERSION_DIR)
  string(REGEX REPLACE "/lib/gcc/arm-none-eabi/[^/]+$" "" SLT_GCC_ROOT "${SLT_GCC_VERSION_DIR}")
  set(TOOLCHAIN_DIR "${SLT_GCC_ROOT}/bin/")
elseif(APPLE)
  set(TOOLCHAIN_DIR "/Users/ahmedel-maghrabi/.silabs/slt/installs/conan/p/gcc-af360b79bab9e3/p//bin/")
else()
  set(TOOLCHAIN_DIR "/bin/")
endif()

if(DEFINED ENV{POST_BUILD_EXE})
  set(POST_BUILD_EXE "$ENV{POST_BUILD_EXE}")
elseif(WIN32)
  file(GLOB SLT_COMMANDER_EXES "${SLT_INSTALLS_DIR}/archive/*/commander.exe")
  if(SLT_COMMANDER_EXES)
    list(GET SLT_COMMANDER_EXES 0 POST_BUILD_EXE)
  else()
    set(POST_BUILD_EXE "")
  endif()
elseif(APPLE)
  set(POST_BUILD_EXE "/Users/ahmedel-maghrabi/.silabs/slt/installs/archive/Commander.app/Contents/MacOS/commander")
else()
  set(POST_BUILD_EXE "")
endif()

if(DEFINED ENV{NINJA_EXE_PATH})
  set(CMAKE_MAKE_PROGRAM "$ENV{NINJA_EXE_PATH}" CACHE FILEPATH "" FORCE)
elseif(WIN32)
  # Empty when not found: CMake then falls back to ninja on PATH.
  file(GLOB SLT_NINJA_EXES "${SLT_INSTALLS_DIR}/conan/p/ninja*/p/ninja.exe")
  if(SLT_NINJA_EXES)
    list(GET SLT_NINJA_EXES 0 NINJA_RUNTIME_PATH)
  else()
    set(NINJA_RUNTIME_PATH "")
  endif()
elseif(APPLE)
  set(NINJA_RUNTIME_PATH "/Users/ahmedel-maghrabi/.silabs/slt/installs/conan/p/ninja48deaaf744f20/p/ninja")
else()
  set(NINJA_RUNTIME_PATH "")
endif()
# Use default lookup mechanisms if the OS specific values are not set above
if (NINJA_RUNTIME_PATH)
	set(CMAKE_MAKE_PROGRAM ${NINJA_RUNTIME_PATH} CACHE FILEPATH "" FORCE)
endif()

set(TARGET_TRIPLET "arm-none-eabi-")
set(CMAKE_C_COMPILER    ${TOOLCHAIN_DIR}${TARGET_TRIPLET}gcc${EXE_SUFFIX})
set(CMAKE_CXX_COMPILER  ${TOOLCHAIN_DIR}${TARGET_TRIPLET}g++${EXE_SUFFIX})
set(CMAKE_ASM_COMPILER  ${TOOLCHAIN_DIR}${TARGET_TRIPLET}gcc${EXE_SUFFIX})
set(CMAKE_LINKER        ${TOOLCHAIN_DIR}${TARGET_TRIPLET}gcc${EXE_SUFFIX})
set(CMAKE_AR            ${TOOLCHAIN_DIR}${TARGET_TRIPLET}ar${EXE_SUFFIX})
set(CMAKE_SIZE_UTIL     ${TOOLCHAIN_DIR}${TARGET_TRIPLET}size${EXE_SUFFIX})
set(CMAKE_STRIP         ${TOOLCHAIN_DIR}${TARGET_TRIPLET}strip${EXE_SUFFIX})
set(CMAKE_OBJCOPY       ${TOOLCHAIN_DIR}${TARGET_TRIPLET}objcopy${EXE_SUFFIX})
set(CMAKE_OBJDUMP       ${TOOLCHAIN_DIR}${TARGET_TRIPLET}objdump${EXE_SUFFIX})
set(CMAKE_NM_UTIL       ${TOOLCHAIN_DIR}${TARGET_TRIPLET}gcc-nm${EXE_SUFFIX})
set(CMAKE_RANLIB        ${TOOLCHAIN_DIR}${TARGET_TRIPLET}gcc-ranlib${EXE_SUFFIX})
set(CMAKE_GCOV          ${TOOLCHAIN_DIR}${TARGET_TRIPLET}gcov${EXE_SUFFIX})

set(OBJCOPY_SREC_CMD    "-O;srec")
set(OBJCOPY_IHEX_CMD    "-O;ihex")
set(OBJCOPY_BIN_CMD     "-O;binary")

set(CMAKE_C_STANDARD_REQUIRED   OFF)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_C_EXTENSIONS          OFF)

set(CMAKE_C_FLAGS_RELEASE               "" CACHE STRING "")
set(CMAKE_CXX_FLAGS_RELEASE             "" CACHE STRING "")

# Response file support
SET(CMAKE_C_USE_RESPONSE_FILE_FOR_OBJECTS   1)
SET(CMAKE_CXX_USE_RESPONSE_FILE_FOR_OBJECTS 1)
SET(CMAKE_C_RESPONSE_FILE_LINK_FLAG         "@")
SET(CMAKE_CXX_RESPONSE_FILE_LINK_FLAG       "@")
SET(CMAKE_NINJA_FORCE_RESPONSE_FILE         1 CACHE INTERNAL "")


set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM   NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY   ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE   ONLY)

set(CMAKE_EXECUTABLE_SUFFIX     .out)
set(CMAKE_EXECUTABLE_SUFFIX_C   .out)
set(CMAKE_EXECUTABLE_SUFFIX_CXX .out)
