# SPDX-License-Identifier: GPL-3.0-or-later
target_sources(corridorkey_core PRIVATE src/settings/schema.cpp)
if(BUILD_TESTING)
  target_sources(corridorkey_contract_tests PRIVATE tests/settings_test.cpp)
endif()
if(TARGET corridorkey AND BUILD_TESTING)
  add_executable(
    corridorkey_settings_obs_tests
    tests/settings_obs_test.cpp
    src/settings/obs_settings.cpp
    src/obs/properties.cpp
  )
  target_link_libraries(corridorkey_settings_obs_tests PRIVATE corridorkey_core OBS::libobs)
  target_include_directories(corridorkey_settings_obs_tests PRIVATE src)
  add_test(NAME settings_obs COMMAND corridorkey_settings_obs_tests "${CMAKE_CURRENT_SOURCE_DIR}/data/locale")
endif()
