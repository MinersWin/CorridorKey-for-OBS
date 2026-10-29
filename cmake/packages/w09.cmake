# SPDX-License-Identifier: GPL-3.0-or-later
option(CORRIDORKEY_CK2_DEV "Enable local CK2 artifact override and validation harness" OFF)
target_sources(
  corridorkey_core
  PRIVATE src/engine/ck2_graph.cpp src/engine/temporal_state.cpp src/engine/ck2_pipeline.cpp
)
if(TARGET corridorkey_ort)
  target_sources(corridorkey_ort PRIVATE src/engine/ck2_ort.cpp src/engine/ck2_bindings.cpp)
  if(TARGET corridorkey AND ENABLE_QT AND ENABLE_FRONTEND_API)
    target_compile_definitions(corridorkey PRIVATE CORRIDORKEY_CK2_RUNTIME)
  endif()
  if(CORRIDORKEY_CK2_DEV)
    target_compile_definitions(corridorkey_ort PUBLIC CORRIDORKEY_CK2_DEV)
    add_executable(corridorkey_ck2_validate tools/ck2_validate.cpp)
    target_link_libraries(corridorkey_ck2_validate PRIVATE corridorkey_ort nlohmann_json::nlohmann_json)
  endif()
endif()
if(BUILD_TESTING)
  target_sources(corridorkey_contract_tests PRIVATE tests/ck2_test.cpp)
endif()
