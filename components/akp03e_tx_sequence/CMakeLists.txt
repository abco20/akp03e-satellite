idf_component_register(
    SRCS "akp03e_tx_sequence.cpp"
    INCLUDE_DIRS "include"
    REQUIRES akp03e_protocol
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
