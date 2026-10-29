// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ck1_pipeline.hpp"
#include "ck2_ort.hpp"

namespace corridorkey::ck1 {
Ck1Prepared prepare_ort(const PrepareRequest &, const ck2::RuntimeConfig &, ChromaHint = {});
#ifdef CORRIDORKEY_CK2_DEV
std::shared_ptr<const ReadyModel> development_model(const std::filesystem::path &, const Settings &,
						    const std::filesystem::path &);
#endif
} // namespace corridorkey::ck1
