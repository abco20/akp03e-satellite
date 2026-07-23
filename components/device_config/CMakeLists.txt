idf_component_register(
    SRCS
        "device_config.cpp"
        "serial_config_console.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        nvs_flash
        esp_system
        freertos
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
