# SPDX-License-Identifier: GPL-3.0-or-later
include(FetchContent)
FetchContent_Declare(
  ck_runtime_json
  URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3
  URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ck_runtime_json)
if(TARGET corridorkey)
  if(WIN32)
    set(_w06_license_dir "${CMAKE_PROJECT_NAME}/data/licenses/nlohmann-json")
  else()
    include(GNUInstallDirs)
    set(_w06_license_dir "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/${CMAKE_PROJECT_NAME}/licenses/nlohmann-json")
  endif()
  install(FILES "${ck_runtime_json_SOURCE_DIR}/LICENSE.MIT" DESTINATION "${_w06_license_dir}")
endif()
target_sources(
  corridorkey_core
  PRIVATE
    src/engine/providers/runtime.cpp
    src/engine/providers/cache.cpp
    src/engine/providers/devices.cpp
    src/engine/providers/probe.cpp
)
target_link_libraries(corridorkey_core PRIVATE nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})
if(WIN32)
  target_link_libraries(corridorkey_core PRIVATE bcrypt dxgi)
else()
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  target_link_libraries(corridorkey_core PRIVATE OpenSSL::Crypto)
endif()

if(CORRIDORKEY_FETCH_ONNXRUNTIME OR CORRIDORKEY_ORT_ROOT)
  include(${PROJECT_SOURCE_DIR}/cmake/dependencies/onnxruntime.cmake)
  add_library(
    corridorkey_ort
    STATIC
    src/engine/ort_session.cpp
    src/engine/providers/loader.cpp
    src/engine/providers/graph_preparation.cpp
  )
  target_compile_features(corridorkey_ort PUBLIC cxx_std_20)
  target_link_libraries(
    corridorkey_ort
    PUBLIC corridorkey_core CorridorKey::OnnxRuntime
    PRIVATE nlohmann_json::nlohmann_json
  )
  set_target_properties(corridorkey_ort PROPERTIES POSITION_INDEPENDENT_CODE ON)
  add_executable(corridorkey_backend_probe tools/backend_probe/main.cpp)
  target_link_libraries(corridorkey_backend_probe PRIVATE corridorkey_ort nlohmann_json::nlohmann_json)
  if(TARGET corridorkey)
    target_link_libraries(corridorkey PRIVATE corridorkey_ort)
    if(WIN32)
      install(TARGETS corridorkey_backend_probe RUNTIME DESTINATION "${CMAKE_PROJECT_NAME}/data/tools")
    else()
      install(TARGETS corridorkey_backend_probe RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/corridorkey")
    endif()
  endif()
endif()
if(BUILD_TESTING)
  add_executable(corridorkey_runtime_tests tests/runtime_test.cpp)
  target_link_libraries(
    corridorkey_runtime_tests
    PRIVATE corridorkey_core doctest::doctest nlohmann_json::nlohmann_json
  )
  add_test(NAME runtime COMMAND corridorkey_runtime_tests "--test-case-exclude=real*")
  set_tests_properties(runtime PROPERTIES TIMEOUT 30)
  if(TARGET corridorkey_ort)
    add_library(corridorkey_mock_ort SHARED tests/mock_ort_runtime.cpp)
    target_link_libraries(corridorkey_mock_ort PRIVATE CorridorKey::OnnxRuntime)
    target_link_libraries(corridorkey_runtime_tests PRIVATE corridorkey_ort)
    target_compile_definitions(
      corridorkey_runtime_tests
      PRIVATE CK_TEST_ORT CK_MOCK_RUNTIME="$<TARGET_FILE:corridorkey_mock_ort>"
    )
    add_dependencies(corridorkey_runtime_tests corridorkey_mock_ort)
  endif()
endif()
