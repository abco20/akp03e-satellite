idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        akp03e_usb
        companion_client
        device_config
        surface_state
        wifi_manager
        nvs_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
