add_library(postgres STATIC
    src/postgres.cpp
)

target_include_directories(postgres
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(postgres
    PUBLIC logger http json
)
