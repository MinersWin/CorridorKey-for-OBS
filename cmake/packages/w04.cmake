# SPDX-License-Identifier: GPL-3.0-or-later
find_package(nlohmann_json 3.11 QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
  include(FetchContent)
  FetchContent_Declare(
    ck_json
    URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3
    URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  set(JSON_Install OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(ck_json)
endif()
if(WIN32)
  set(ZLIB_USE_STATIC_LIBS ON)
endif()
find_package(ZLIB QUIET)
set(_ck_zlib_target ZLIB::ZLIB)
if(NOT ZLIB_FOUND AND WIN32)
  include(FetchContent)
  FetchContent_Declare(
    ck_zlib
    URL https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz https://zlib.net/zlib-1.3.2.tar.gz
    URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(ck_zlib)
  set(_ck_zlib_target ZLIB::ZLIBSTATIC)
elseif(NOT ZLIB_FOUND)
  find_package(ZLIB REQUIRED)
endif()
target_sources(
  corridorkey_core
  PRIVATE
    src/models/manifest.cpp
    src/models/licenses.cpp
    src/models/cache.cpp
    src/models/download.cpp
    src/models/catalog.cpp
)
target_link_libraries(corridorkey_core PUBLIC nlohmann_json::nlohmann_json PRIVATE ${_ck_zlib_target})
if(WIN32)
  target_link_libraries(corridorkey_core PRIVATE bcrypt winhttp)
else()
  find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
  find_package(CURL 7.85 REQUIRED)
  target_link_libraries(corridorkey_core PRIVATE OpenSSL::Crypto CURL::libcurl)
endif()
if(TARGET corridorkey)
  target_sources(corridorkey PRIVATE src/models/cache_obs.cpp)
endif()
if(BUILD_TESTING)
  add_executable(corridorkey_catalogue_verify tests/models/catalogue_verify.cpp)
  target_link_libraries(corridorkey_catalogue_verify PRIVATE corridorkey_core)
  add_executable(corridorkey_models_tests tests/models/models_test.cpp)
  target_link_libraries(corridorkey_models_tests PRIVATE corridorkey_core doctest::doctest)
  target_compile_definitions(corridorkey_models_tests PRIVATE CK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME models COMMAND corridorkey_models_tests)
  set_tests_properties(models PROPERTIES TIMEOUT 60)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  option(
    CORRIDORKEY_TEST_CATALOGUE_SIGNING
    "Run signing integration tests (requires tools/requirements-signing.txt)"
    OFF
  )
  if(CORRIDORKEY_TEST_CATALOGUE_SIGNING)
    add_test(
      NAME catalogue_signing
      COMMAND
        "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/models/catalogue_signing_test.py"
        "$<TARGET_FILE:corridorkey_catalogue_verify>" "${CMAKE_CURRENT_BINARY_DIR}"
    )
    set_tests_properties(catalogue_signing PROPERTIES TIMEOUT 120)
  endif()
  add_test(
    NAME models_http
    COMMAND
      "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/models/http_fixture.py"
      "$<TARGET_FILE:corridorkey_models_tests>" "${CMAKE_CURRENT_BINARY_DIR}/http-fixture"
  )
  set_tests_properties(models_http PROPERTIES TIMEOUT 120)
  add_test(
    NAME models_signature
    COMMAND
      corridorkey_models_tests --verify-catalogue "${CMAKE_SOURCE_DIR}/tests/models/fixtures/catalogue-v1.json"
      "${CMAKE_SOURCE_DIR}/tests/models/fixtures/signature-v1.json"
  )
endif()
