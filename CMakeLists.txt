cmake_minimum_required(VERSION 3.24)

project(ConcurrentReadMostlyMap VERSION 0.1.0 LANGUAGES CXX)

option(READ_MOSTLY_BUILD_TESTS "Build tests" ON)

option(READ_MOSTLY_WARNINGS_AS_ERRORS "Treat library and test warnings as errors" ON)
option(READ_MOSTLY_ASAN_UBSAN "Enable address and undefined behavior sanitizers" OFF)
option(READ_MOSTLY_TSAN "Enable thread sanitizer" OFF)
if(READ_MOSTLY_ASAN_UBSAN AND READ_MOSTLY_TSAN)
    message(FATAL_ERROR "ASan and TSan require separate builds")
endif()

find_package(Threads REQUIRED)
add_library(read_mostly_map src/snapshot.cpp src/update_transaction.cpp src/snapshot_builder.cpp
    src/read_mostly_map.cpp)
target_link_libraries(read_mostly_map PUBLIC Threads::Threads)
add_library(read_mostly::map ALIAS read_mostly_map)

target_compile_features(read_mostly_map PUBLIC cxx_std_20)
set_target_properties(read_mostly_map PROPERTIES CXX_EXTENSIONS OFF EXPORT_NAME map)
target_include_directories(read_mostly_map
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

function(read_mostly_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(READ_MOSTLY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        if(READ_MOSTLY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
read_mostly_warnings(read_mostly_map)
if(READ_MOSTLY_ASAN_UBSAN OR READ_MOSTLY_TSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizer presets require GCC or Clang")
    endif()
    if(READ_MOSTLY_TSAN)
        set(sanitizer thread)
    else()
        set(sanitizer address,undefined)
    endif()
    target_compile_options(read_mostly_map PUBLIC -fsanitize=${sanitizer} -fno-omit-frame-pointer)
    target_link_options(read_mostly_map PUBLIC -fsanitize=${sanitizer})
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS read_mostly_map EXPORT ReadMostlyMapTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.hpp")
install(EXPORT ReadMostlyMapTargets NAMESPACE read_mostly:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReadMostlyMap)
configure_package_config_file(cmake/ReadMostlyMapConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ReadMostlyMapConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReadMostlyMap)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/ReadMostlyMapConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ReadMostlyMapConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ReadMostlyMapConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ReadMostlyMap)

if(READ_MOSTLY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
