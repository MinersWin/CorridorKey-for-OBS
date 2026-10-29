// SPDX-License-Identifier: GPL-3.0-or-later
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "telemetry/diagnostics.hpp"
#include "telemetry/host_policy.hpp"
#include <nlohmann/json.hpp>
#include <limits>

using namespace corridorkey;
namespace diag = corridorkey::diagnostics;
namespace {
void *present()
{
	static int value;
	return &value;
}
nlohmann::json payload(const std::string &report)
{
	const auto begin = report.find("```json\n") + 8;
	return nlohmann::json::parse(report.substr(begin, report.rfind("\n```") - begin));
}
struct Probe : ProviderProbe {
	bool none = false, mismatch_backend = false;
	ProbeResult probe(const ProbeRequest &r, std::stop_token) override
	{
		ProbeResult result;
		auto &p = result.provider;
		p.backend = mismatch_backend && r.backend != Backend::directml ? Backend::cpu_diagnostic : r.backend;
		p.available = p.tested = p.permits_live_inference = !none;
		p.matches_obs_adapter = mismatch_backend || r.backend == Backend::directml;
		p.adapter = r.obs_adapter;
		return result;
	}
};
} // namespace
TEST_CASE("unknown OBS versions load; absent optional telemetry uses configured FPS")
{
	for (auto version : {0x1e020000u, 0x1f000000u, 0x20000000u, 0x21000000u, 0x63000000u}) {
		auto result = diag::assess_host(version, "999.0.0-future", [](const char *name) {
			return std::string_view(name) == "obs_get_active_fps" ? nullptr : present();
		});
		CHECK(result.load);
		CHECK_FALSE(result.active_fps);
		CHECK(result.unreviewed == (version == 0x63000000u));
	}
	CHECK(diag::assess_host(0x21000000, "future-string", [](const char *) { return present(); }).active_fps);
	CHECK(diag::frame_rate(60, 1, {}) == 60);
	CHECK(diag::frame_rate(60, 1, 59.9) == doctest::Approx(59.9));
	CHECK(diag::frame_rate(60, 1, std::numeric_limits<double>::infinity()) == 60);
	CHECK(diag::frame_rate(60, 0, {}) == 0);
}
TEST_CASE("missing required libobs ABI refuses with the missing symbol and recovery")
{
	const auto result = diag::assess_host(0x21000000, "33.0.0", [](const char *name) {
		return std::string_view(name) == "obs_register_source_s" ? nullptr : present();
	});
	CHECK_FALSE(result.load);
	CHECK(result.message.find("required libobs ABI") != std::string::npos);
	CHECK(result.message.find("obs_register_source_s") != std::string::npos);
	CHECK(result.message.find("Install") != std::string::npos);
}
TEST_CASE("wrong adapters and provider identities fall back safely without automatic CPU")
{
	Probe probe;
	ProbeRequest request;
	request.obs_adapter = {"luid:00000001:00000002", "not copied", 0x10de, 0};
	auto selection = ort::select_provider(probe, ort::Platform::windows, request, {});
	REQUIRE(selection.selected);
	CHECK(selection.selected->backend == Backend::directml);
	CHECK(selection.attempts.size() > 1);
	probe.mismatch_backend = true;
	selection = ort::select_provider(probe, ort::Platform::windows, request, {});
	REQUIRE(selection.selected);
	CHECK(selection.selected->backend == Backend::directml);
	probe.none = true;
	probe.mismatch_backend = false;
	selection = ort::select_provider(probe, ort::Platform::windows, request, {});
	CHECK_FALSE(selection.selected);
	for (const auto &attempt : selection.attempts)
		CHECK(attempt.provider.backend != Backend::cpu_diagnostic);
}
TEST_CASE("report contains verified identities, W15 values and only allowlisted settings")
{
	ui::ManagerSnapshot manager;
	ui::ModelRow row;
	row.key = {"fixture", "revision1"};
	row.name = "private scene";
	row.status.state = ModelState::ready;
	row.status.failure = {ErrorCode::io_failure, "private error hf_PrivateCredential"};
	row.manifest.emplace();
	row.manifest->digest = std::string(64, 'a');
	models::Graph graph;
	graph.id = "stage1";
	graph.graph = "graph-file";
	graph.weights_sha256 = std::string(64, 'b');
	row.manifest->graphs.push_back(graph);
	row.manifest->artifacts["graph-file"].sha256 = std::string(64, 'c');
	row.manifest->artifacts["graph-file"].revision = "graph-rev";
	manager.rows.push_back(row);
	manager.notice = "do not copy this notice";
	diag::Environment env;
	env.plugin = "0.1.0";
	env.commit = "123abcd";
	env.os = "Windows 11";
	env.kernel = "winnt 10.0.26200";
	ort::Device device;
	device.adapter.stable_id = "luid:00000001:00000002";
	device.driver = "32.0.15.1234";
	env.devices.push_back(device);
	auto settings = std::make_shared<Settings>();
	settings->context.audio_source_id = "private microphone";
	settings->hint.mask_source_id = "private source";
	settings->backend.device_id = "private device";
	settings->quality.model_revision = "C:\\Users\\Alice\\model";
	auto status = std::make_shared<Status>();
	status->backend = Backend::directml;
	status->diagnostic_detail = "private error hf_PrivateCredential";
	status->age_median_ms = 24;
	status->age_p95_ms = 38;
	status->inference_median_ms = std::numeric_limits<double>::infinity();
	env.instances.push_back({settings, status, {1920, 1080}});
	const auto report = diag::report(manager, env);
	for (const auto *private_text : {"private scene", "private error", "PrivateCredential", "private microphone",
					 "private source", "private device", "Alice", "do not copy this notice"})
		CHECK(report.find(private_text) == std::string::npos);
	const auto json = payload(report);
	CHECK(json["commit"] == "123abcd");
	CHECK(json["gpus"][0]["adapter"] == device.adapter.stable_id);
	CHECK(json["models"][0]["manifest_sha256"] == row.manifest->digest);
	CHECK(json["models"][0]["graphs"][0]["sha256"] == std::string(64, 'c'));
	CHECK(json["instances"][0]["timing"]["age_p95_ms"] == 38);
	CHECK(json["instances"][0]["timing"]["inference_median_ms"].is_null());
	CHECK(json["instances"][0]["settings"]["profile"] == "balanced");
	CHECK_FALSE(json["instances"][0]["settings"].contains("audio_source_id"));
	status->backend = Backend::fake;
	CHECK(payload(diag::report(manager, env))["instances"][0]["backend"] == "fake (synthetic)");
}
TEST_CASE("home paths and credentials redact before display or copy")
{
	for (const auto *path : {"C:\\Users\\Alice\\cache", "c:/users/Alice/cache", "/home/alice/cache",
				 "/Users/alice/cache", "C:\\Users\\Alice Smith\\cache"}) {
		const auto redacted = diag::redact(path);
		CHECK(redacted.find("<profile>") != std::string::npos);
		CHECK(redacted.find("Alice") == std::string::npos);
		CHECK(redacted.find("alice") == std::string::npos);
	}
	for (const auto *secret :
	     {"hf_ExampleSecret", "ghp_ExampleSecret", "github_pat_ExampleSecret", "Bearer ExampleSecret",
	      "token=ExampleSecret", "api_key: ExampleSecret", "Authorization: Bearer ExampleSecret"})
		CHECK(diag::redact(secret).find("ExampleSecret") == std::string::npos);
}
TEST_CASE("bundle and probe reports retain versions and hashes without paths, options or raw errors")
{
	diag::Environment env;
	diag::Provider provider;
	provider.backend = Backend::directml;
	provider.bundle.emplace();
	provider.bundle->id = "dml-v1";
	provider.bundle->root = "C:/Users/Alice/runtime";
	provider.bundle->options["token"] = "private credential";
	provider.bundle->file_hashes["private-library-path"] = std::string(64, 'd');
	provider.probe.emplace();
	provider.probe->provider.backend = Backend::directml;
	provider.probe->provider.runtime_build = "999.0.0";
	provider.probe->provider.provider_build = "1.15.4";
	provider.probe->negotiated_api = 31;
	provider.probe->failure = {ErrorCode::backend_unavailable, "private raw error"};
	provider.probe->diagnostic_detail = "private helper JSON";
	env.providers.push_back(provider);
	const auto report = diag::report({}, env);
	for (const auto *secret :
	     {"Alice", "private credential", "private raw error", "private helper", "private-library"})
		CHECK(report.find(secret) == std::string::npos);
	const auto j = payload(report)["providers"][0];
	CHECK(j["bundle"] == "dml-v1");
	CHECK(j["ort_core"] == "999.0.0");
	CHECK(j["ort_api"] == 31);
	CHECK(j["file_hashes"][0] == std::string(64, 'd'));
	CHECK(j["error"] == std::string(error_id(ErrorCode::backend_unavailable)));
	CHECK_FALSE(j["tested"].get<bool>());
}
TEST_CASE("recent errors omit arbitrary actions and registry teardown removes callbacks")
{
	diag::record_error(ErrorCode::out_of_memory);
	const auto json = payload(diag::report({}, {}));
	CHECK(json["recent_errors"].back()["code"] == "CK-E006");
	int owner = 0;
	diag::register_instance(&owner, [] { return diag::Instance{}; });
	CHECK(diag::instances().size() == 1);
	diag::unregister_instance(&owner);
	CHECK(diag::instances().empty());
	ProbeResult result;
	result.provider.backend = Backend::directml;
	result.failure = {ErrorCode::io_failure, "private path"};
	result.diagnostic_detail = "private path";
	diag::record_probe(result);
	const auto saved = diag::latest_probe(Backend::directml);
	REQUIRE(saved);
	CHECK(saved->diagnostic_detail.empty());
	CHECK(saved->failure.action.empty());
}
