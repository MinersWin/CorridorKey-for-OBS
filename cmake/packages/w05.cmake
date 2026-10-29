# SPDX-License-Identifier: GPL-3.0-or-later
target_sources(
  corridorkey_core
  PRIVATE src/models/binder.cpp src/models/safetensors.cpp src/models/npy.cpp src/models/zip_storage.cpp
)
if(BUILD_TESTING)
  add_executable(corridorkey_binder_api_tests tests/models/binder_api_test.cpp)
  target_link_libraries(corridorkey_binder_api_tests PRIVATE corridorkey_core doctest::doctest)
  add_test(NAME binder_api COMMAND corridorkey_binder_api_tests)
  add_executable(corridorkey_bind_weights tools/bind-weights.cpp)
  target_link_libraries(corridorkey_bind_weights PRIVATE corridorkey_core)
  add_test(
    NAME binder
    COMMAND
      "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/models/binder_test.py"
      "$<TARGET_FILE:corridorkey_bind_weights>" "${CMAKE_CURRENT_BINARY_DIR}/binder-fixtures"
      "$<TARGET_FILE:corridorkey_binder_api_tests>"
  )
  set_tests_properties(binder PROPERTIES TIMEOUT 120)
  add_test(
    NAME binder_local_parity
    COMMAND
      "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/check-bound-weights.py"
      "$<TARGET_FILE:corridorkey_bind_weights>" --output-dir "${CMAKE_CURRENT_BINARY_DIR}/binder-parity"
  )
  set_tests_properties(
    binder_local_parity
    PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800 LABELS "local-models"
  )
endif()
