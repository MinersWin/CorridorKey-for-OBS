# SPDX-License-Identifier: GPL-3.0-or-later
target_sources(corridorkey_core PRIVATE src/packaging/runtime_install.cpp)
add_executable(corridorkey-runtime tools/runtime-install.cpp)
target_link_libraries(corridorkey-runtime PRIVATE corridorkey_core)
file(SHA256 "${PROJECT_SOURCE_DIR}/data/runtimes/runtime-recipes-v1.json" _w19_catalogue_hash)
set_property(
  DIRECTORY
  APPEND
  PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/data/runtimes/runtime-recipes-v1.json"
)
target_compile_definitions(corridorkey-runtime PRIVATE CK_RUNTIME_CATALOGUE_SHA256="${_w19_catalogue_hash}")
if(TARGET corridorkey AND ENABLE_QT AND ENABLE_FRONTEND_API)
  target_sources(corridorkey PRIVATE src/packaging/runtime_dialog.cpp)
  target_compile_definitions(corridorkey PRIVATE CK_RUNTIME_CATALOGUE_SHA256="${_w19_catalogue_hash}")
  if(NOT WIN32)
    target_compile_definitions(
      corridorkey
      PRIVATE CK_RUNTIME_PROBE_HELPER="${CMAKE_INSTALL_FULL_LIBEXECDIR}/corridorkey/corridorkey_backend_probe"
    )
  endif()
endif()
if(TARGET corridorkey)
  file(
    CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/packaging-build.json"
    CONTENT
      "{\n  \"zlib\": \"${ZLIB_VERSION_STRING}\",\n  \"nlohmann_json\": \"${nlohmann_json_VERSION}\",\n  \"qt\": \"${Qt6_VERSION}\",\n  \"obs_sdk_lane\": \"${CORRIDORKEY_OBS_SDK}\",\n  \"ort_headers\": \"${CORRIDORKEY_ORT_VERSION}\"\n}\n"
  )
  if(WIN32)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/packaging-build.json" DESTINATION "${CMAKE_PROJECT_NAME}/data")
    install(TARGETS corridorkey-runtime RUNTIME DESTINATION "${CMAKE_PROJECT_NAME}/data/tools")
  else()
    include(GNUInstallDirs)
    install(
      FILES "${CMAKE_CURRENT_BINARY_DIR}/packaging-build.json"
      DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/${CMAKE_PROJECT_NAME}"
    )
    install(TARGETS corridorkey-runtime RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/corridorkey")
  endif()
endif()
if(BUILD_TESTING)
  add_executable(corridorkey_packaging_tests tests/packaging_test.cpp)
  target_link_libraries(corridorkey_packaging_tests PRIVATE corridorkey_core doctest::doctest)
  add_test(NAME packaging COMMAND corridorkey_packaging_tests)
  add_test(
    NAME packaging_zip
    COMMAND
      "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/packaging_zip_fixture.py"
      "$<TARGET_FILE:corridorkey_packaging_tests>" "${CMAKE_CURRENT_BINARY_DIR}/packaging-fixtures"
  )
  add_test(
    NAME packaging_inventory
    COMMAND
      "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/packaging_inventory_test.py" "${CMAKE_CURRENT_BINARY_DIR}"
  )
endif()
