// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ck2_pipeline.hpp"
#include "ort_session.hpp"
#include "ck2_bindings.hpp"

namespace corridorkey::ck2 {
class OrtGraph final : public Graph {
public:
	OrtGraph(std::unique_ptr<ort::Session>, Stage, Extent, bool cuda_device = false,
		 std::function<std::unique_ptr<ort::Session>()> recreate = {},
		 std::function<std::unique_ptr<ort::Session>(const std::string &, std::stop_token)> fallback = {});
	Tensors run(const Inputs &, std::stop_token) override;
	std::map<std::string, std::size_t> finish_profile();
	void reset_transform() override;

private:
	std::unique_ptr<ort::Session> session_;
	std::unique_ptr<PersistentBindings> bindings_;
	std::function<std::unique_ptr<ort::Session>()> recreate_;
	std::function<std::unique_ptr<ort::Session>(const std::string &, std::stop_token)> fallback_;
	bool device_ = false;
	bool recapture_ = false;
	Tensors inputs_, outputs_;
};
void verify_manifest(const std::filesystem::path &, Stage, Extent);
struct RuntimeConfig {
	ort::ProbeConfig probe;
	ort::CompilerConfig compiler;
	std::filesystem::path cache, profiles;
	bool capture = true;
	std::function<void(const std::string &)> info;
	std::function<void(const ProbeResult &)> record_probe;
};
Ck2Prepared prepare_ort(const PrepareRequest &, const RuntimeConfig &);
#ifdef CORRIDORKEY_CK2_DEV
struct DevelopmentConfig {
	RuntimeConfig runtime;
	std::filesystem::path models;
	std::filesystem::path release_fixture;
	AdapterIdentity adapter;
};
DevelopmentConfig read_development_config(const std::filesystem::path &);
std::shared_ptr<const ReadyModel> development_model(const std::filesystem::path &, Extent, bool quality,
						    const std::filesystem::path &release_fixture = {});
#endif
} // namespace corridorkey::ck2
