# SPDX-License-Identifier: GPL-3.0-or-later
target_sources(
  corridorkey_core
  PRIVATE src/ui/manager_model.cpp src/ui/manager_controller.cpp src/models/activation.cpp
)
if(TARGET corridorkey AND ENABLE_QT AND ENABLE_FRONTEND_API)
  target_sources(
    corridorkey
    PRIVATE src/ui/model_manager.cpp src/ui/license_dialog.cpp src/ui/backend_test.cpp src/models/installation.cpp
  )
  target_compile_definitions(corridorkey PRIVATE CORRIDORKEY_MODEL_MANAGER)
  set(CORRIDORKEY_CATALOGUE_PUBLIC_KEY "" CACHE STRING "Release P-256 public point (128 lowercase hex digits)")
  if(CORRIDORKEY_CATALOGUE_PUBLIC_KEY)
    string(LENGTH "${CORRIDORKEY_CATALOGUE_PUBLIC_KEY}" _w07_key_length)
    if(NOT _w07_key_length EQUAL 128 OR NOT CORRIDORKEY_CATALOGUE_PUBLIC_KEY MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "CORRIDORKEY_CATALOGUE_PUBLIC_KEY must be a P-256 x||y public point")
    endif()
  endif()
  target_compile_definitions(corridorkey PRIVATE CORRIDORKEY_CATALOGUE_PUBLIC_KEY="${CORRIDORKEY_CATALOGUE_PUBLIC_KEY}")
  file(SHA256 "${PROJECT_SOURCE_DIR}/data/models/catalogue-v1.json" _w09b_catalogue_hash)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/data/models/catalogue-v1.json")
  target_compile_definitions(corridorkey PRIVATE CORRIDORKEY_BUNDLED_CATALOGUE_SHA256="${_w09b_catalogue_hash}")
  if(NOT WIN32)
    include(GNUInstallDirs)
    target_compile_definitions(
      corridorkey
      PRIVATE CORRIDORKEY_PROBE_HELPER="${CMAKE_INSTALL_FULL_LIBEXECDIR}/corridorkey/corridorkey_backend_probe"
    )
  endif()
  option(CORRIDORKEY_MANAGER_FIXTURES "Enable local model-manager OBS validation fixtures (not for release)" OFF)
  if(CORRIDORKEY_MANAGER_FIXTURES)
    target_sources(corridorkey PRIVATE tests/ui/obs_manager_fixture.cpp)
    target_compile_definitions(corridorkey PRIVATE CORRIDORKEY_MANAGER_FIXTURES)
  endif()
endif()
if(BUILD_TESTING)
  add_executable(corridorkey_manager_tests tests/manager_test.cpp)
  target_link_libraries(corridorkey_manager_tests PRIVATE corridorkey_core doctest::doctest)
  target_compile_definitions(corridorkey_manager_tests PRIVATE CK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME manager COMMAND corridorkey_manager_tests)
  add_test(
    NAME manager_fixture
    COMMAND corridorkey_models_tests --write-fixture "${CMAKE_CURRENT_BINARY_DIR}/manager-fixture"
  )
  set_tests_properties(manager_fixture PROPERTIES FIXTURES_SETUP manager_data)
  set_tests_properties(manager PROPERTIES FIXTURES_REQUIRED manager_data)
  set_tests_properties(manager PROPERTIES TIMEOUT 60)
endif()
