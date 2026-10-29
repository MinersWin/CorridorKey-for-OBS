# SPDX-License-Identifier: GPL-3.0-or-later
target_sources(corridorkey_core PRIVATE src/runtime/scheduler.cpp)
if(BUILD_TESTING)
  target_sources(corridorkey_contract_tests PRIVATE tests/scheduler_test.cpp)
endif()
