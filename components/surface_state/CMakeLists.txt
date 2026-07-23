idf_component_register(
    SRCS "surface_state.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos akp03e_protocol
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
