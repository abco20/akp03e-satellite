idf_component_register(
    SRCS "jpeg_encoder.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_new_jpeg mbedtls akp03e_protocol image_transform surface_state
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
