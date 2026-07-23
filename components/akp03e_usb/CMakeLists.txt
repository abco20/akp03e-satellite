idf_component_register(
    SRCS "akp03e_usb.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        usb
        freertos
        esp_timer
        driver
        akp03e_protocol
        akp03e_tx_sequence
        akp03e_usb_descriptor
        akp03e_usb_quirks
        surface_state
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
