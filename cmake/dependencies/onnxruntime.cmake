# SPDX-License-Identifier: GPL-3.0-or-later
include_guard(GLOBAL)
# Build pins are reproducibility anchors. Runtime support is negotiated by API.
set(CORRIDORKEY_ORT_VERSION "1.31.1")
if(CORRIDORKEY_ORT_ROOT)
  find_path(
    _ort_include
    NAMES onnxruntime_c_api.h
    PATHS "${CORRIDORKEY_ORT_ROOT}"
    PATH_SUFFIXES include include/onnxruntime/core/session
    NO_DEFAULT_PATH
    REQUIRED
  )
else()
  include(FetchContent)
  FetchContent_Declare(
    corridorkey_ort_headers
    URL https://github.com/microsoft/onnxruntime/archive/refs/tags/v1.31.1.tar.gz
    URL_HASH SHA256=92ffbf8b97872fd1e73c9eebdf3319d9a3b24919ae438d27db903b72ee08939e
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR
    unused-header-only
  )
  FetchContent_MakeAvailable(corridorkey_ort_headers)
  set(_ort_include "${corridorkey_ort_headers_SOURCE_DIR}/include/onnxruntime/core/session")
endif()
# Never link the import library: missing ORT must not prevent OBS loading.
add_library(CorridorKey::OnnxRuntime INTERFACE IMPORTED GLOBAL)
set_target_properties(CorridorKey::OnnxRuntime PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_ort_include}")
