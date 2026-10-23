set(component_sources
        "main.cpp"
        "LedChannel.cpp"
        "Button.cpp"
        "ButtonDebouncer.cpp"
        "ButtonGesture.cpp"
        "ChannelState.cpp"
        "LocalControlConfig.cpp"
        "LocalControlDiscovery.cpp"
        "DutyTransition.cpp"
        "MqttCommandParser.cpp"
        "MqttService.cpp"
        "MqttDiscoveryPayload.cpp"
        "DebugLog.cpp"
        "DebugDiscovery.cpp"
        "OtaService.cpp"
        "LocalOtaService.cpp"
        "PirInput.cpp"
        "I2cBus.cpp"
        "LcdDashboard.cpp"
        "LcdDisplay.cpp"
        "LcdService.cpp"
        "StatusIndicator.cpp"
        "StatusLed.cpp"
        "TaskWatchdog.cpp"
        "LedController.cpp"
        "WifiService.cpp"
        "ServiceConsole.cpp"
)

set(component_private_requires
        app_update
        esp_timer
        console
        esp_driver_uart
        esp_event
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_ledc
        esp_netif
        esp_http_server
        esp_https_ota
        esp_partition
        esp_system
        esp_wifi
        nvs_flash
        mbedtls
)

if(CONFIG_LED_CONTROLLER_AT24C08 OR CONFIG_LED_CONTROLLER_DS18B20)
    list(APPEND component_sources
        "TemperatureModel.cpp"
    )
endif()

if(CONFIG_LED_CONTROLLER_DS1307_NVRAM)
    list(APPEND component_sources "Ds1307Nvram.cpp" "LocalConfigRecord.cpp")
endif()

if(CONFIG_LED_CONTROLLER_AT24C08)
    list(APPEND component_sources
        "At24c08.cpp"
        "SecureSettingsRecord.cpp"
        "SecureSettingsCodec.cpp"
        "EncryptedSettingsStore.cpp"
        "PersistentSettings.cpp"
        "DeviceKeyProvider.cpp"
        "SettingsService.cpp"
        "WebPassword.cpp"
        "WebDiagnosticsService.cpp"
        "DeviceTlsIdentity.cpp"
    )
    list(APPEND component_private_requires
        esp_timer
        efuse
        mbedtls
        esp_https_server
        espressif__cjson
        espressif__mdns
    )
endif()

if(CONFIG_LED_CONTROLLER_IR_NEC)
    list(APPEND component_sources
        "NecDecoder.cpp"
        "IrRemoteMapper.cpp"
        "IrReceiverService.cpp"
    )
    list(APPEND component_private_requires
        esp_driver_rmt
    )
endif()

if(CONFIG_LED_CONTROLLER_INA228)
    list(APPEND component_sources
        "Ina228.cpp"
    )
endif()

if(CONFIG_LED_CONTROLLER_DS18B20)
    list(APPEND component_sources
        "Ds18b20Service.cpp"
    )
    list(APPEND component_private_requires
        espressif__onewire_bus
    )
endif()

idf_component_register(
    SRCS ${component_sources}
    INCLUDE_DIRS "."
    PRIV_REQUIRES ${component_private_requires}
)
