# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)
project(${_name} VERSION ${_version} LANGUAGES C CXX)

option(ENABLE_FRONTEND_API "Use the OBS frontend API" ON)
option(ENABLE_QT "Use Qt for the model manager" ON)
option(CORRIDORKEY_FETCH_ONNXRUNTIME "Fetch/build the pinned optional inference runtime" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_20)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE CORRIDORKEY_VERSION="${PROJECT_VERSION}")

find_package(libobs 33.0 REQUIRED)
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

if(CORRIDORKEY_FETCH_ONNXRUNTIME)
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
