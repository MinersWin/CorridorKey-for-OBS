# SPDX-License-Identifier: GPL-3.0-or-later
include_guard(GLOBAL)

if(CORRIDORKEY_ORT_ROOT)
  find_path(
    _ort_include
    NAMES onnxruntime_c_api.h
    PATHS "${CORRIDORKEY_ORT_ROOT}"
    PATH_SUFFIXES include include/onnxruntime/core/session
    NO_DEFAULT_PATH
    REQUIRED
  )
  find_library(
    _ort_library
    NAMES onnxruntime
    PATHS "${CORRIDORKEY_ORT_ROOT}"
    PATH_SUFFIXES lib lib64
    NO_DEFAULT_PATH
    REQUIRED
  )
  add_library(CorridorKey::OnnxRuntime UNKNOWN IMPORTED GLOBAL)
  set_target_properties(
    CorridorKey::OnnxRuntime
    PROPERTIES IMPORTED_LOCATION "${_ort_library}" INTERFACE_INCLUDE_DIRECTORIES "${_ort_include}"
  )
  return()
endif()

# These pins select reproducible downloads, not the versions accepted at runtime.
# The engine must negotiate ORT's versioned C API and inspect available EPs;
# unsupported EPs fall back rather than rejecting a driver or version string.
set(CORRIDORKEY_ORT_VERSION "1.31.1")
set(_ort_release "https://github.com/microsoft/onnxruntime/releases/download/v${CORRIDORKEY_ORT_VERSION}")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The pinned ONNX Runtime scaffold supports x86_64 only")
endif()

# EP strategy: prefer TensorRT-RTX / TensorRT / CUDA on NVIDIA and MIGraphX on
# supported AMD systems; DirectML is the Windows fallback, CPU is for debugging.
# Providers must later be selected at runtime, with explicit capability checks.
# ORT's official GitHub releases do not ship a DirectML binary archive. Build the
# pinned release source with DirectML rather than mislabeling the Windows CPU zip.
# Vendor EP runtimes and drivers are user-installed, never downloaded silently.
if(OS_WINDOWS)
  include(ExternalProject)
  set(_ort_source "${CMAKE_BINARY_DIR}/_deps/ort-source")
  set(_ort_build "${CMAKE_BINARY_DIR}/_deps/ort-build")
  file(MAKE_DIRECTORY "${_ort_source}/include/onnxruntime/core/session")
  ExternalProject_Add(
    corridorkey-onnxruntime
    URL "https://github.com/microsoft/onnxruntime/archive/refs/tags/v${CORRIDORKEY_ORT_VERSION}.tar.gz"
    URL_HASH SHA256=92ffbf8b97872fd1e73c9eebdf3319d9a3b24919ae438d27db903b72ee08939e
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_DIR "${_ort_source}"
    SOURCE_SUBDIR cmake
    BINARY_DIR "${_ort_build}"
    CMAKE_ARGS
      -Donnxruntime_USE_DML=ON -Donnxruntime_BUILD_SHARED_LIB=ON -Donnxruntime_BUILD_UNIT_TESTS=OFF
      -Donnxruntime_BUILD_BENCHMARKS=OFF -Donnxruntime_DEV_MODE=OFF -DCMAKE_BUILD_TYPE=Release
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release --target onnxruntime --parallel 4
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${_ort_build}/Release/onnxruntime.dll" "${_ort_build}/Release/onnxruntime.lib"
  )
  add_library(CorridorKey::OnnxRuntime SHARED IMPORTED GLOBAL)
  set_target_properties(
    CorridorKey::OnnxRuntime
    PROPERTIES
      IMPORTED_LOCATION "${_ort_build}/Release/onnxruntime.dll"
      IMPORTED_IMPLIB "${_ort_build}/Release/onnxruntime.lib"
      INTERFACE_INCLUDE_DIRECTORIES "${_ort_source}/include/onnxruntime/core/session"
  )
  add_dependencies(CorridorKey::OnnxRuntime corridorkey-onnxruntime)
elseif(OS_LINUX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  set(CORRIDORKEY_ORT_LINUX_PROVIDER "CPU" CACHE STRING "Pinned runtime variant: CPU, CUDA12 or CUDA13")
  set_property(CACHE CORRIDORKEY_ORT_LINUX_PROVIDER PROPERTY STRINGS CPU CUDA12 CUDA13)
  if(CORRIDORKEY_ORT_LINUX_PROVIDER STREQUAL "CPU")
    set(_ort_archive "onnxruntime-linux-x64-${CORRIDORKEY_ORT_VERSION}.tgz")
    set(_ort_sha256 "c849e36a1ef3e22d4ff217b9d1ce4a3cf5b9467b975610913ae959ad5b16bdc9")
  elseif(CORRIDORKEY_ORT_LINUX_PROVIDER STREQUAL "CUDA12")
    set(_ort_archive "onnxruntime-linux-x64-gpu_cuda12-${CORRIDORKEY_ORT_VERSION}.tgz")
    set(_ort_sha256 "57cd51f677eca19a269cdba31b0c16c15d73a8feea41f170aa8b750447ab1f70")
  elseif(CORRIDORKEY_ORT_LINUX_PROVIDER STREQUAL "CUDA13")
    set(_ort_archive "onnxruntime-linux-x64-gpu_cuda13-${CORRIDORKEY_ORT_VERSION}.tgz")
    set(_ort_sha256 "b2146862e691f7e338f5aff9c7a9a5567c2ca57cf01442a1adb3a13bf62d9095")
  else()
    message(FATAL_ERROR "CORRIDORKEY_ORT_LINUX_PROVIDER must be CPU, CUDA12 or CUDA13")
  endif()
  include(FetchContent)
  FetchContent_Declare(
    corridorkey_ort
    URL "${_ort_release}/${_ort_archive}"
    URL_HASH "SHA256=${_ort_sha256}"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
  )
  FetchContent_MakeAvailable(corridorkey_ort)
  add_library(CorridorKey::OnnxRuntime SHARED IMPORTED GLOBAL)
  set_target_properties(
    CorridorKey::OnnxRuntime
    PROPERTIES
      IMPORTED_LOCATION "${corridorkey_ort_SOURCE_DIR}/lib/libonnxruntime.so"
      INTERFACE_INCLUDE_DIRECTORIES "${corridorkey_ort_SOURCE_DIR}/include"
  )
else()
  message(FATAL_ERROR "The optional ONNX Runtime scaffold supports Windows x64 and Linux x86_64 only")
endif()

# The engine will link CorridorKey::OnnxRuntime when implemented. No runtime is
# linked or redistributed by the pass-through filter, even with fetching enabled.
