cmake_minimum_required(VERSION 3.16)

project(led_controller_host_tests LANGUAGES C CXX)

enable_testing()

set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(CJSON_ROOT "${PROJECT_ROOT}/managed_components/espressif__cjson/cJSON")

if(NOT EXISTS "${CJSON_ROOT}/cJSON.c")
    message(FATAL_ERROR "Brak managed component espressif/cjson; najpierw zbuduj firmware ESP32")
endif()

add_library(cjson_host STATIC "${CJSON_ROOT}/cJSON.c")
target_include_directories(cjson_host PUBLIC "${CJSON_ROOT}")

add_executable(
    led_controller_host_tests
    "test_main.cpp"
    "${PROJECT_ROOT}/src/ButtonDebouncer.cpp"
    "${PROJECT_ROOT}/src/ButtonGesture.cpp"
    "${PROJECT_ROOT}/src/ChannelState.cpp"
    "${PROJECT_ROOT}/src/LocalControlConfig.cpp"
    "${PROJECT_ROOT}/src/LocalControlDiscovery.cpp"
    "${PROJECT_ROOT}/src/DebugDiscovery.cpp"
    "${PROJECT_ROOT}/src/MqttDiscoveryPayload.cpp"
    "${PROJECT_ROOT}/src/LocalConfigRecord.cpp"
    "${PROJECT_ROOT}/src/DutyTransition.cpp"
    "${PROJECT_ROOT}/src/IrRemoteMapper.cpp"
    "${PROJECT_ROOT}/src/LcdDashboard.cpp"
    "${PROJECT_ROOT}/src/MqttCommandParser.cpp"
    "${PROJECT_ROOT}/src/NecDecoder.cpp"
    "${PROJECT_ROOT}/src/PersistentSettings.cpp"
    "${PROJECT_ROOT}/src/SecureSettingsRecord.cpp"
    "${PROJECT_ROOT}/src/StatusIndicator.cpp"
    "${PROJECT_ROOT}/src/TemperatureModel.cpp"
)

target_include_directories(led_controller_host_tests PRIVATE "${PROJECT_ROOT}/src")
target_link_libraries(led_controller_host_tests PRIVATE cjson_host)
target_compile_definitions(led_controller_host_tests PRIVATE CONFIG_LED_CONTROLLER_AT24C08=1 CONFIG_LED_CONTROLLER_INA228=1 CONFIG_LED_CONTROLLER_DS18B20=1)
target_compile_features(led_controller_host_tests PRIVATE cxx_std_17)
set_target_properties(led_controller_host_tests PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(led_controller_host_tests PRIVATE /W4 /WX /permissive- /EHs-c- /GR- /utf-8)
else()
    target_compile_options(
        led_controller_host_tests
        PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti
    )
endif()

add_test(NAME led_controller_host_tests COMMAND led_controller_host_tests)

add_executable(led_controller_adapter_tests test_adapters.cpp
    "${PROJECT_ROOT}/src/DebugLog.cpp"
    "${PROJECT_ROOT}/src/LcdDisplay.cpp" "${PROJECT_ROOT}/src/LedChannel.cpp"
    "${PROJECT_ROOT}/src/ChannelState.cpp" "${PROJECT_ROOT}/src/DutyTransition.cpp")
target_include_directories(led_controller_adapter_tests PRIVATE stubs "${PROJECT_ROOT}/src")
target_compile_features(led_controller_adapter_tests PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(led_controller_adapter_tests PRIVATE /W4 /WX /permissive- /EHs-c- /GR- /utf-8)
else()
    target_compile_options(led_controller_adapter_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
endif()
add_test(NAME led_controller_adapter_tests COMMAND led_controller_adapter_tests)

find_package(Threads REQUIRED)
target_link_libraries(led_controller_adapter_tests PRIVATE Threads::Threads)
