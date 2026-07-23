idf_component_register(
    SRCS "akp03e_usb_descriptor.cpp"
    INCLUDE_DIRS "include"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
