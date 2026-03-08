cmake_minimum_required(VERSION 3.16)
project(hms_nvr VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -pthread")

# Find required packages
find_package(jsoncpp REQUIRED)
find_package(CURL REQUIRED)
find_package(yaml-cpp REQUIRED)

# Find Paho MQTT C++ library
find_library(PAHO_MQTTPP3_LIB paho-mqttpp3)
find_library(PAHO_MQTT3AS_LIB paho-mqtt3as)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Collect source files
file(GLOB_RECURSE SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME}
    jsoncpp_lib
    ${PAHO_MQTTPP3_LIB}
    ${PAHO_MQTT3AS_LIB}
    CURL::libcurl
    yaml-cpp
    pthread
    ssl
    crypto
)

# Install target
install(TARGETS ${PROJECT_NAME} DESTINATION /usr/local/bin)

# Tests
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    file(GLOB_RECURSE TEST_SOURCES ${CMAKE_SOURCE_DIR}/tests/*.cpp)
    file(GLOB_RECURSE LIB_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
    list(FILTER LIB_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

    add_executable(run_tests ${TEST_SOURCES} ${LIB_SOURCES})
    target_link_libraries(run_tests
        GTest::GTest GTest::Main
        jsoncpp_lib
        ${PAHO_MQTTPP3_LIB}
        ${PAHO_MQTT3AS_LIB}
        CURL::libcurl
        yaml-cpp
        pthread ssl crypto
    )
    add_test(NAME hms_nvr_tests COMMAND run_tests)
endif()

# Print configuration
message(STATUS "==================== HMS-NVR Build Configuration ====================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Paho MQTT C++: ${PAHO_MQTTPP3_LIB}")
message(STATUS "CURL: ${CURL_VERSION_STRING}")
message(STATUS "=====================================================================")
