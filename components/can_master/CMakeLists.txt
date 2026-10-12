idf_component_register(SRCS "CanMaster.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES driver freertos)

# Отключаем превращение предупреждений препроцессора (#warning) в критические ошибки
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error=cpp")
