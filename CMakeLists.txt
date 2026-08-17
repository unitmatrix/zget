cmake_minimum_required(VERSION 3.16)
project(zget VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)

option(ZGET_BUILD_FUZZERS "Build libFuzzer parser targets" OFF)
option(ZGET_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(ZGET_BUILD_REFERENCE_TESTS "Use Python zipfile as an integration oracle" ON)
option(ZGET_BUILD_LARGE_TESTS "Enable the generated 100k-entry test" OFF)

if(ZGET_ENABLE_SANITIZERS AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(CURL 7.85.0 REQUIRED)
find_package(ZLIB REQUIRED)

set(ZGET_LIBRARY_SOURCES
    src/zget.c src/util.c src/http/http.c src/zip/zip.c src/codec/extract.c)

add_library(libzget SHARED ${ZGET_LIBRARY_SOURCES})
add_library(libzget_static STATIC ${ZGET_LIBRARY_SOURCES})
add_library(Zget::libzget ALIAS libzget)
add_library(Zget::libzget_static ALIAS libzget_static)

foreach(target IN ITEMS libzget libzget_static)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME zget
        C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    target_compile_features(${target} PUBLIC c_std_99)
    target_compile_definitions(${target} PRIVATE ZGET_BUILDING)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE src)
    target_link_libraries(${target} PRIVATE CURL::libcurl ZLIB::ZLIB)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion)
    endif()
endforeach()

set_target_properties(libzget PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
target_compile_definitions(libzget PUBLIC ZGET_SHARED)

add_executable(zget cli/zget.c)
target_compile_features(zget PRIVATE c_std_99)
# Release archives contain only the CLI executable. Link project code statically
# so users do not also need a matching libzget shared library at runtime.
target_link_libraries(zget PRIVATE Zget::libzget_static)
if(UNIX)
    target_compile_definitions(zget PRIVATE _POSIX_C_SOURCE=200809L)
endif()

if(BUILD_TESTING OR ZGET_BUILD_FUZZERS)
    add_library(zget_parser_objects OBJECT src/util.c src/zip/zip.c)
    target_compile_features(zget_parser_objects PRIVATE c_std_99)
    target_include_directories(zget_parser_objects PRIVATE include src)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(zget_parser_objects PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion)
    endif()
    if(ZGET_BUILD_FUZZERS)
        target_compile_options(zget_parser_objects PRIVATE
            -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(zget_unit_tests tests/test_parsers.c
        $<TARGET_OBJECTS:zget_parser_objects>)
    target_include_directories(zget_unit_tests PRIVATE src)
    target_include_directories(zget_unit_tests PRIVATE include)
    add_test(NAME unit COMMAND zget_unit_tests)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND AND ZGET_BUILD_REFERENCE_TESTS)
        add_test(NAME integration_reference
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py
                    $<TARGET_FILE:zget>)
        set_tests_properties(integration_reference PROPERTIES TIMEOUT 90)
    endif()
    if(Python3_Interpreter_FOUND AND ZGET_BUILD_LARGE_TESTS)
        add_test(NAME integration_100k_entries
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/large_directory.py
                    $<TARGET_FILE:zget>)
        set_tests_properties(integration_100k_entries PROPERTIES TIMEOUT 300)
    endif()
endif()

if(ZGET_BUILD_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "ZGET_BUILD_FUZZERS requires Clang/libFuzzer")
    endif()
    include(CheckCSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
    check_c_source_compiles(
        "#include <stddef.h>\nint LLVMFuzzerTestOneInput(const unsigned char *p, size_t n) {(void)p; (void)n; return 0;}"
        ZGET_HAVE_LIBFUZZER)
    unset(CMAKE_REQUIRED_FLAGS)
    if(NOT ZGET_HAVE_LIBFUZZER)
        message(FATAL_ERROR "The selected Clang does not provide a libFuzzer runtime")
    endif()
    foreach(parser IN ITEMS eocd cd cd_stream zip64_extra local)
        add_executable(fuzz_${parser} fuzz/fuzz_${parser}.c
            $<TARGET_OBJECTS:zget_parser_objects>)
        target_include_directories(fuzz_${parser} PRIVATE include src)
        target_compile_options(fuzz_${parser} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz_${parser} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
endif()

install(TARGETS libzget libzget_static zget EXPORT ZgetTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/zget.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
# GNUInstallDirs has no license-directory variable. Keep this relative and
# overridable so DESTDIR and distro-specific policies continue to work.
set(ZGET_INSTALL_LICENSEDIR "${CMAKE_INSTALL_DATAROOTDIR}/licenses/${PROJECT_NAME}"
    CACHE PATH "Installation directory for license files")
install(FILES LICENSE DESTINATION ${ZGET_INSTALL_LICENSEDIR})
install(EXPORT ZgetTargets
    NAMESPACE Zget::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Zget)
configure_package_config_file(cmake/ZgetConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ZgetConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Zget)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/ZgetConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ZgetConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/ZgetConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Zget)
configure_file(cmake/libzget.pc.in libzget.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libzget.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
configure_file(docs/zget.1.in zget.1 @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/zget.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
