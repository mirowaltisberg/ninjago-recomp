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

# Headless boot harness (Unicorn backend)
add_executable(nj_headless_uc harness/headless_main.c harness/boot.c)
target_include_directories(nj_headless_uc PRIVATE harness)
target_link_libraries(nj_headless_uc PRIVATE nj_backend_uc nj_headless)

# AOT: the whole guest library recompiled to C (gen/ from the .so), and the headless harness on it.
if(EXISTS ${NJ_GUEST_SO})
  set(NJ_GEN ${CMAKE_BINARY_DIR}/gen)
  set(NJ_GEN_FILES 64)
  set(NJ_GEN_SOURCES ${NJ_GEN}/dispatch.c)
  math(EXPR _last "${NJ_GEN_FILES} - 1")
  foreach(i RANGE ${_last})
    string(LENGTH "${i}" _len)
    if(_len EQUAL 1)
      set(_n "00${i}")
    elseif(_len EQUAL 2)
      set(_n "0${i}")
    else()
      set(_n "${i}")
    endif()
    list(APPEND NJ_GEN_SOURCES ${NJ_GEN}/f_${_n}.c)
  endforeach()
  file(GLOB NJ_RECOMP_PY CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/recomp/*.py)
  add_custom_command(
    OUTPUT ${NJ_GEN_SOURCES} ${NJ_GEN}/recomp_decls.h
    COMMAND ${CMAKE_SOURCE_DIR}/tools/.venv/bin/python -m recomp.cli ${NJ_GUEST_SO} --base 0x100000 --out ${NJ_GEN}
            --files ${NJ_GEN_FILES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools
    DEPENDS ${NJ_RECOMP_PY} ${NJ_GUEST_SO}
    COMMENT "Recompiling the guest library")
  add_library(nj_guest_aot STATIC ${NJ_GEN_SOURCES})
  target_include_directories(nj_guest_aot PUBLIC ${NJ_GEN} ${CMAKE_SOURCE_DIR}/runtime/backend/aot)
  target_link_libraries(nj_guest_aot PUBLIC nj_runtime)
  target_compile_options(nj_guest_aot PRIVATE -Wno-unused-variable -Wno-unused-but-set-variable
                         $<$<CONFIG:Debug>:-O1>)
  target_compile_definitions(nj_guest_aot PRIVATE $<$<CONFIG:Debug>:NJ_AOT_CHECKS=1>)

  add_library(nj_backend_aot STATIC runtime/backend/aot/aot_backend.c runtime/backend/aot/aot_dispatch.c)
  target_include_directories(nj_backend_aot PUBLIC ${CMAKE_SOURCE_DIR}/runtime/backend/aot)
  target_link_libraries(nj_backend_aot PUBLIC nj_runtime nj_guest_aot)

  add_executable(nj_headless_aot harness/headless_main.c harness/boot.c)
  target_include_directories(nj_headless_aot PRIVATE harness)
  target_link_libraries(nj_headless_aot PRIVATE nj_backend_aot nj_guest_aot nj_headless)

  add_executable(test_aot_divide tests/c/test_aot_divide.c)
  target_link_libraries(test_aot_divide PRIVATE nj_test_main nj_backend_aot nj_guest_aot nj_headless)
  add_test(NAME test_aot_divide COMMAND test_aot_divide)
  set_tests_properties(test_aot_divide PROPERTIES SKIP_RETURN_CODE 77)
endif()
