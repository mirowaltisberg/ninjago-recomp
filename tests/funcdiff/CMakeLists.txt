# Function-level differential test: translated functions vs Unicorn on picked self-contained guest functions.
set(FD_OUT ${CMAKE_BINARY_DIR}/funcdiff)
file(GLOB NJ_RECOMP_PY CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/recomp/*.py)
add_custom_command(
  OUTPUT ${FD_OUT}/picks.txt ${FD_OUT}/only.txt
  COMMAND ${NJ_PYTHON} -m recomp.funcdiff_pick ${NJ_GUEST_SO} --out ${FD_OUT}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools
  DEPENDS ${NJ_RECOMP_PY}
  COMMENT "Picking funcdiff functions")
add_custom_command(
  OUTPUT ${FD_OUT}/gen/f_000.c ${FD_OUT}/gen/dispatch.c ${FD_OUT}/gen/recomp_decls.h
  COMMAND ${NJ_PYTHON} -m recomp.cli ${NJ_GUEST_SO} --base 0x100000 --out ${FD_OUT}/gen
          --only-file ${FD_OUT}/only.txt --funcs-per-file 1000000
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools
  DEPENDS ${FD_OUT}/only.txt ${NJ_RECOMP_PY}
  COMMENT "Translating funcdiff functions")
add_executable(funcdiff funcdiff_main.c ${FD_OUT}/gen/f_000.c ${FD_OUT}/gen/dispatch.c
               ${CMAKE_SOURCE_DIR}/runtime/backend/aot/aot_dispatch.c)
target_include_directories(funcdiff PRIVATE ${FD_OUT}/gen ${CMAKE_SOURCE_DIR}/runtime/backend/aot)
target_compile_definitions(funcdiff PRIVATE NJ_GUEST_SO_PATH="${NJ_GUEST_SO}" NJ_FUNCDIFF_PICKS="${FD_OUT}/picks.txt"
                           NJ_AOT_CHECKS=1)
target_link_libraries(funcdiff PRIVATE nj_backend_uc) # brings nj_runtime and Unicorn
add_test(NAME funcdiff COMMAND funcdiff)
set_tests_properties(funcdiff PROPERTIES SKIP_RETURN_CODE 77)
