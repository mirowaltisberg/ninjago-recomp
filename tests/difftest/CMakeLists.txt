# Instruction-level differential test: lifted C vs Unicorn on real instructions of the guest library.
set(NJ_PYTHON "${CMAKE_SOURCE_DIR}/tools/.venv/bin/python" CACHE FILEPATH "Python with the recompiler requirements")
set(DT_OUT ${CMAKE_BINARY_DIR}/difftest)
file(GLOB NJ_RECOMP_PY CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/recomp/*.py)
add_custom_command(
  OUTPUT ${DT_OUT}/cases.c ${DT_OUT}/cases.h
  COMMAND ${NJ_PYTHON} -m recomp.difftest_gen ${NJ_GUEST_SO} --out ${DT_OUT} --per-shape 4 --base 0x100000
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools
  DEPENDS ${NJ_RECOMP_PY}
  COMMENT "Generating instruction difftest cases")
add_executable(difftest difftest_main.c ${DT_OUT}/cases.c)
target_include_directories(difftest PRIVATE ${DT_OUT} ${CMAKE_SOURCE_DIR}/runtime/backend/aot)
target_compile_definitions(difftest PRIVATE NJ_GUEST_SO_PATH="${NJ_GUEST_SO}")
target_link_libraries(difftest PRIVATE nj_backend_uc) # brings nj_runtime and Unicorn
add_test(NAME difftest COMMAND difftest)
set_tests_properties(difftest PROPERTIES SKIP_RETURN_CODE 77)
