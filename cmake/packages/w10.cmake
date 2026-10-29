# SPDX-License-Identifier: GPL-3.0-or-later
target_sources(corridorkey_core PRIVATE src/engine/providers/nvidia.cpp src/gpu/d3d11_cuda.cpp)
if(WIN32)
  target_link_libraries(corridorkey_core PUBLIC d3d11)
endif()
if(WIN32 AND BUILD_TESTING)
  add_executable(corridorkey_interop_test tests/nvidia_interop.cpp)
  target_link_libraries(corridorkey_interop_test PRIVATE corridorkey_core d3d11)
endif()
