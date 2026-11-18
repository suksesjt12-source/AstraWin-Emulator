cmake_minimum_required(VERSION 3.22.1)
project(AstraWinNative LANGUAGES CXX)

set(ASTRAWIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

add_library(astrawin_core STATIC
    ${ASTRAWIN_ROOT}/native/src/memory/guest_memory.cpp
    ${ASTRAWIN_ROOT}/native/src/cpu/x86_64/interpreter.cpp
    ${ASTRAWIN_ROOT}/native/src/cpu/x86/interpreter.cpp
    ${ASTRAWIN_ROOT}/native/src/pe/pe_parser.cpp
    ${ASTRAWIN_ROOT}/native/src/pe/pe_loader.cpp
    ${ASTRAWIN_ROOT}/native/src/win32/api_registry.cpp
    ${ASTRAWIN_ROOT}/native/src/win32/environment.cpp
    ${ASTRAWIN_ROOT}/native/src/process/guest_process.cpp
    ${ASTRAWIN_ROOT}/native/src/filesystem/guest_path_mapper.cpp
    ${ASTRAWIN_ROOT}/native/src/sync/guest_event.cpp
    ${ASTRAWIN_ROOT}/native/src/sync/guest_timer.cpp
    ${ASTRAWIN_ROOT}/native/src/graphics/headless_device.cpp
)

target_include_directories(astrawin_core PUBLIC
    ${ASTRAWIN_ROOT}/native/include
)

target_compile_features(astrawin_core PUBLIC cxx_std_20)
target_compile_options(astrawin_core PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(astrawin_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(ANDROID)
    add_library(astrawin_native SHARED
        ${ASTRAWIN_ROOT}/native/src/platform/android/jni_bridge.cpp
    )
    target_link_libraries(astrawin_native PRIVATE astrawin_core log)
    target_compile_options(astrawin_native PRIVATE -Wall -Wextra -Wpedantic)
else()
    find_package(Threads REQUIRED)
    enable_testing()
    add_executable(astrawin_native_tests
        ${ASTRAWIN_ROOT}/native/tests/native_tests.cpp
    )
    target_link_libraries(astrawin_native_tests PRIVATE astrawin_core Threads::Threads)
    add_test(NAME astrawin_native_tests COMMAND astrawin_native_tests)
endif()
