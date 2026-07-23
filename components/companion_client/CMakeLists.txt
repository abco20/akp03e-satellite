idf_component_register(
    SRCS "companion_client.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        companion_protocol
        jpeg_encoder
        surface_state
        wifi_manager
        lwip
        freertos
        esp_timer
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
