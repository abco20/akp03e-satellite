idf_component_register(
    SRCS "akp03e_usb_quirks.cpp"
    INCLUDE_DIRS "include"
    REQUIRES usb akp03e_usb_descriptor
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
