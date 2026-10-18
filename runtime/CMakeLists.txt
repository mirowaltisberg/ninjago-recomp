file(GLOB NJ_RUNTIME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

set(NJ_DLMALLOC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dlmalloc/malloc.c)
set_source_files_properties(${NJ_DLMALLOC} PROPERTIES
  COMPILE_OPTIONS "-w"
  COMPILE_DEFINITIONS "ONLY_MSPACES=1;MSPACES=1;USE_LOCKS=1;HAVE_MMAP=0;HAVE_MORECORE=0;FOOTERS=0;INSECURE=0")

add_library(nj_runtime STATIC ${NJ_RUNTIME_SOURCES} ${NJ_DLMALLOC})
target_include_directories(nj_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
find_package(Threads REQUIRED)
target_link_libraries(nj_runtime PUBLIC Threads::Threads "-framework CoreFoundation")

# CPU backends (exactly one is linked per executable).
find_package(PkgConfig REQUIRED)
pkg_check_modules(UNICORN REQUIRED IMPORTED_TARGET unicorn)
add_library(nj_backend_uc STATIC backend/unicorn/uc_backend.c)
target_link_libraries(nj_backend_uc PUBLIC nj_runtime PkgConfig::UNICORN)

# Khronos headers for the macOS/headless build (iOS uses the OpenGLES framework headers).
target_include_directories(nj_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/khronos)

# Headless platform: stub GL/EGL/audio for macOS boot tests.
file(GLOB NJ_HEADLESS_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/headless/*.c)
add_library(nj_headless STATIC ${NJ_HEADLESS_SOURCES})
target_link_libraries(nj_headless PUBLIC nj_runtime)
