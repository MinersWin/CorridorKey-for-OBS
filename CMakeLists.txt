# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)
project(${_name} VERSION ${_version} LANGUAGES C CXX)

option(ENABLE_FRONTEND_API "Use the OBS frontend API" ON)
option(ENABLE_QT "Use Qt for the model manager" ON)
option(CORRIDORKEY_FETCH_ONNXRUNTIME "Fetch/build the pinned optional inference runtime" OFF)
option(CORRIDORKEY_USE_BUNDLED_SDK "Bootstrap the selected OBS SDK on Windows" ON)
set(CORRIDORKEY_OBS_SDK "baseline" CACHE STRING "Reproducible SDK download set: baseline or current")
set_property(CACHE CORRIDORKEY_OBS_SDK PROPERTY STRINGS baseline current)
set(CORRIDORKEY_ORT_ROOT "" CACHE PATH "Use an existing ONNX Runtime SDK instead of downloading one")
set(CORRIDORKEY_WINDOWS_LAYOUT "MODERN" CACHE STRING "Windows install layout: MODERN (OBS 33+) or LEGACY (OBS 30.2-32)")
set_property(CACHE CORRIDORKEY_WINDOWS_LAYOUT PROPERTY STRINGS MODERN LEGACY)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_20)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CORRIDORKEY_VERSION="${PROJECT_VERSION}")

# OBS's SameMajorVersion package check would reject newer SDKs if a version were
# passed to find_package. Use the documented API baseline without an upper bound.
find_package(libobs REQUIRED)
if(DEFINED libobs_VERSION AND libobs_VERSION VERSION_LESS 30.2)
  message(FATAL_ERROR "Use OBS SDK 30.2 or newer; runtime compatibility is checked by capabilities")
endif()
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

if(CORRIDORKEY_FETCH_ONNXRUNTIME OR CORRIDORKEY_ORT_ROOT)
  include(cmake/dependencies/onnxruntime.cmake)
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.cpp src/filter.cpp src/filter.hpp)
set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(OS_WINDOWS)
  set(_license_destination "${_name}/data/licenses")
else()
  set(_license_destination "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/${_name}/licenses")
endif()
install(FILES LICENSE LICENSE-EXCEPTION.md NOTICE THIRD_PARTY_NOTICES.md DESTINATION "${_license_destination}")
