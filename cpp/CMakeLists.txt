cmake_minimum_required(VERSION 3.20)
project(DriftglassCore VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)

add_library(driftglass_core
    src/core.cpp
    src/schema_validation.cpp
    src/reports.cpp
    src/redaction.cpp
    src/storage.cpp
    src/html_report.cpp
    src/capture.cpp
    src/capabilities.cpp
)
target_include_directories(driftglass_core PUBLIC include)
target_link_libraries(driftglass_core PUBLIC nlohmann_json::nlohmann_json OpenSSL::Crypto SQLite3::SQLite3)
target_compile_features(driftglass_core PUBLIC cxx_std_20)
target_compile_definitions(driftglass_core PUBLIC DRIFTGLASS_SCHEMA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../schemas")

if(MSVC)
    target_compile_options(driftglass_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(driftglass_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(driftglass-core src/main.cpp)
target_link_libraries(driftglass-core PRIVATE driftglass_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(driftglass-core-tests tests/core_tests.cpp)
    target_link_libraries(driftglass-core-tests PRIVATE driftglass_core)
    add_test(NAME driftglass-core-tests COMMAND driftglass-core-tests)
    add_test(NAME driftglass-core-capabilities COMMAND driftglass-core capabilities)
    add_test(
        NAME driftglass-core-history-export
        COMMAND ${CMAKE_COMMAND}
            -DDRIFTGLASS_CLI=$<TARGET_FILE:driftglass-core>
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/history-export-smoke
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/history_export_smoke.cmake
    )
endif()

install(TARGETS driftglass-core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../schemas/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/driftglass/schemas"
    FILES_MATCHING PATTERN "*.schema.json"
)

set(CPACK_PACKAGE_NAME "driftglass")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Local-first application update behaviour auditor")
set(CPACK_PACKAGE_FILE_NAME "driftglass-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "TGZ")
include(CPack)
