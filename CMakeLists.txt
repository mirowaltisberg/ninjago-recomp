cmake_minimum_required(VERSION 3.28)
project(ninjago_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

# Game inputs (personal copies, git-ignored). Tests needing them skip when absent.
set(NJ_GUEST_SO "${CMAKE_SOURCE_DIR}/input/extracted/lib/armeabi-v7a/libLEGO_M1.so" CACHE FILEPATH "Guest library")
set(NJ_GUEST_OBB "${CMAKE_SOURCE_DIR}/input/extracted/Android/Obb/com.lego.ninjago.toe/main.1057.com.lego.ninjago.toe.obb" CACHE FILEPATH "Guest OBB")

enable_testing()
add_subdirectory(runtime)
add_subdirectory(tests)
