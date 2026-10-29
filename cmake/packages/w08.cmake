# SPDX-License-Identifier: GPL-3.0-or-later
if(TARGET corridorkey)
  target_sources(corridorkey PRIVATE src/gpu/post/reconstruction.cpp)
endif()
if(BUILD_TESTING)
  target_sources(corridorkey_contract_tests PRIVATE tests/reconstruction_test.cpp tests/post_reference.cpp)
endif()
if(TARGET corridorkey AND BUILD_TESTING)
  add_executable(
    corridorkey_post_probe
    tests/post_probe.cpp
    tests/post_gpu_test.cpp
    tests/post_offline.cpp
    tests/post_reference.cpp
    src/gpu/post/reconstruction.cpp
  )
  target_link_libraries(corridorkey_post_probe PRIVATE OBS::libobs corridorkey_core nlohmann_json::nlohmann_json)
  target_compile_features(corridorkey_post_probe PRIVATE cxx_std_20)
endif()
