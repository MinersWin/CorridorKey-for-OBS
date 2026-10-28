# SPDX-License-Identifier: GPL-3.0-or-later
include(FetchContent)
FetchContent_Declare(
  doctest
  URL https://codeload.github.com/doctest/doctest/tar.gz/refs/tags/v2.4.11
  URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)
add_executable(corridorkey_contract_tests contracts_test.cpp)
target_link_libraries(corridorkey_contract_tests PRIVATE corridorkey_core doctest::doctest)
add_test(NAME contracts COMMAND corridorkey_contract_tests)
set_tests_properties(contracts PROPERTIES TIMEOUT 30)
