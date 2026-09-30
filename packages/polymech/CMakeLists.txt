add_library(polymech STATIC
    src/polymech.cpp
)

target_include_directories(polymech
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(polymech PUBLIC postgres logger)
