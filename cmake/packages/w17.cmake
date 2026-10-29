# SPDX-License-Identifier: GPL-3.0-or-later
target_sources(corridorkey_core PRIVATE src/telemetry/diagnostics.cpp src/telemetry/host_policy.cpp)
target_link_libraries(corridorkey_core PRIVATE nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})
if(TARGET corridorkey)
  find_program(
    _w17_git
    git
    HINTS "$ENV{LOCALAPPDATA}/Microsoft/WinGet/Packages/Git.MinGit_Microsoft.Winget.Source_8wekyb3d8bbwe/cmd"
  )
  set(_w17_commit "unknown")
  if(_w17_git)
    execute_process(
      COMMAND "${_w17_git}" describe --always --dirty
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE _w17_commit
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  endif()
  target_compile_definitions(corridorkey PRIVATE CORRIDORKEY_BUILD_COMMIT="${_w17_commit}")
  if(ENABLE_QT AND ENABLE_FRONTEND_API)
    target_sources(corridorkey PRIVATE src/telemetry/diagnostics_obs.cpp)
    if(CORRIDORKEY_MANAGER_FIXTURES)
      target_sources(corridorkey PRIVATE tests/ui/obs_diagnostics_fixture.cpp)
    endif()
  endif()
endif()
if(BUILD_TESTING)
  add_executable(corridorkey_diagnostics_tests tests/diagnostics_test.cpp)
  target_link_libraries(
    corridorkey_diagnostics_tests
    PRIVATE corridorkey_core doctest::doctest nlohmann_json::nlohmann_json
  )
  add_test(NAME diagnostics COMMAND corridorkey_diagnostics_tests)
  set_tests_properties(diagnostics PROPERTIES TIMEOUT 30)
endif()
