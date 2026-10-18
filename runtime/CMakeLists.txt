file(GLOB NJ_RUNTIME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

add_library(nj_runtime STATIC ${NJ_RUNTIME_SOURCES})
target_include_directories(nj_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
find_package(Threads REQUIRED)
target_link_libraries(nj_runtime PUBLIC Threads::Threads)
