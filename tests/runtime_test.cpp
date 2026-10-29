// SPDX-License-Identifier: GPL-3.0-or-later
#define DOCTEST_CONFIG_IMPLEMENT
#include "util/utf8_path.hpp"
#include <doctest/doctest.h>
#include "engine/providers/process.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <thread>
#ifdef CK_TEST_ORT
#include "engine/providers/loader.hpp"
#include "engine/ort_session.hpp"
#endif
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#endif
using namespace corridorkey;
using namespace corridorkey::ort;
using nlohmann::json;
namespace {
fs::path executable;
struct Temp {
	fs::path path = fs::current_path() / ("runtime-test-" + std::to_string(std::random_device{}()));
	Temp() { fs::create_directories(path); }
	~Temp()
	{
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};
void write(const fs::path &path, const json &j)
{
	std::ofstream out(path);
	out << j.dump();
}
struct FakeProbe : ProviderProbe {
	std::vector<Backend> calls;
	ProbeResult probe(const ProbeRequest &r, std::stop_token) override
	{
		calls.push_back(r.backend);
		ProbeResult result;
		result.provider.backend = r.backend;
		if (r.backend == Backend::cuda || r.backend == Backend::cpu_diagnostic) {
			result.provider.available = result.provider.tested = result.provider.matches_obs_adapter = true;
			result.provider.permits_live_inference = r.backend != Backend::cpu_diagnostic;
		} else
			result.failure = error(ErrorCode::backend_unavailable);
		return result;
	}
};
} // namespace
TEST_CASE("SHA-256 and canonical cache identity")
{
	CHECK(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CacheIdentity key;
	key.graph_hash = "graph";
	key.driver = "old";
	const auto before = key.canonical();
	key.driver = "new";
	CHECK(before != key.canonical());
	Temp temp;
	CacheLease first(temp.path, key);
	CacheLease concurrent(temp.path, key);
	CHECK(first.path() != concurrent.path());
	key.runtime_build = "999.0.0";
	CacheLease newer(temp.path, key);
	CHECK(first.path() != newer.path());
	CHECK(fs::exists(temp.path / sha256(key.canonical()) / "identity.json"));
}
TEST_CASE("bundle paths and contents are verified")
{
	Temp temp;
	std::ofstream(temp.path / "core") << "library";
	json j{{"schema", 1},
	       {"id", "test-v1"},
	       {"backend", "cpu"},
	       {"registration", "cpu"},
	       {"runtime", "core"},
	       {"runtime_build", "999"},
	       {"provider_build", "999"},
	       {"provider_name", "CPUExecutionProvider"},
	       {"files", {{"core", file_sha256(temp.path / "core")}}}};
	write(temp.path / "bundle.json", j);
	auto b = read_bundle(temp.path / "bundle.json");
	CHECK_NOTHROW(verify_bundle(b));
	std::ofstream(temp.path / "core") << "changed";
	CHECK_THROWS(verify_bundle(b));
	j["runtime"] = "../core";
	write(temp.path / "bundle.json", j);
	CHECK_THROWS(read_bundle(temp.path / "bundle.json"));
}
TEST_CASE("interrupted and mismatched caches rebuild without reusing partial engines")
{
	Temp temp;
	CacheIdentity key;
	key.graph_hash = "graph";
	fs::path path;
	{
		CacheLease lease(temp.path, key);
		path = lease.path();
		std::ofstream(path / "partial.engine") << "bad";
	}
	{
		CacheLease lease(temp.path, key);
		CHECK_FALSE(fs::exists(lease.path() / "partial.engine"));
		std::ofstream(lease.path() / "good.engine") << "good";
		lease.commit();
	}
	{
		CacheLease lease(temp.path, key);
		CHECK(fs::exists(lease.path() / "good.engine"));
		lease.commit();
	}
	std::ofstream(temp.path / sha256(key.canonical()) / "identity.json") << "mismatch";
	{
		CacheLease lease(temp.path, key);
		CHECK_FALSE(fs::exists(lease.path() / "good.engine"));
	}
}
TEST_CASE("auto selection excludes CPU and follows platform candidates")
{
	FakeProbe probe;
	ProbeRequest r;
	r.obs_adapter.vendor_id = 0x10de;
	auto result = select_provider(probe, Platform::windows, r, {});
	REQUIRE(result.selected);
	CHECK(result.selected->backend == Backend::cuda);
	CHECK(probe.calls == std::vector<Backend>{Backend::tensorrt_rtx, Backend::tensorrt, Backend::cuda});
	CHECK(candidates(Platform::linux_os, 0x1002, Backend::automatic) == std::vector<Backend>{Backend::migraphx});
	CHECK(candidates(Platform::windows, 0x1002, Backend::automatic) == std::vector<Backend>{Backend::directml});
	r.backend = Backend::cpu_diagnostic;
	CHECK(select_provider(probe, Platform::windows, r, {}).selected.has_value());
	CHECK_FALSE(adapter_matches({}, {}));
	CHECK_FALSE(adapter_matches({"luid:1", "same name", 0x10de}, {"luid:2", "same name", 0x10de}));
	CHECK(adapter_matches({"luid:1", "", 0x10de}, {"luid:1", "", 0x10de}));
	CHECK(adapter_matches({"pci:0000:0a:00.0", "", 0x10de}, {"pci:00000000:0A:00.0", "", 0x10de}));
	CHECK_FALSE(adapter_matches({"pci:garbage"}, {"pci:garbage"}));
}
TEST_CASE("helper timeout cancellation crash and malformed JSON are contained")
{
	Temp temp;
	const auto request = temp.path / "request with spaces.json", response = temp.path / "response.json";
	write(request, {{"test", "sleep"}});
	auto result = run_helper(executable, request, response, std::chrono::milliseconds(80), {});
	CHECK(result.started);
	CHECK(result.timed_out);
	CHECK_FALSE(result.crashed);
	std::stop_source stop;
	stop.request_stop();
	result = run_helper(executable, request, response, std::chrono::seconds(1), stop.get_token());
	CHECK(result.cancelled);
	CHECK_FALSE(result.started);
	std::stop_source during_run;
	std::jthread cancel_thread([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		during_run.request_stop();
	});
	result = run_helper(executable, request, response, std::chrono::seconds(3), during_run.get_token());
	CHECK(result.started);
	CHECK(result.cancelled);
	CHECK_FALSE(result.timed_out);
	write(request, {{"test", "crash"}});
	result = run_helper(executable, request, response, std::chrono::seconds(3), {});
	CHECK(result.started);
	CHECK(result.crashed);
	write(request, {{"test", "ok"}});
	result = run_helper(executable, request, response, std::chrono::seconds(3), {});
	CHECK(result.exit_code == 0);
	CHECK(fs::exists(response));
	ProbeConfig config{executable, temp.path / "scratch", temp.path / "fixture"};
	config.bundles[Backend::cuda] = temp.path / "bundle.json";
	DisposableProbe probe(config);
	ProbeRequest r;
	r.backend = Backend::cuda;
	CHECK(probe.probe(r, {}).failure); // Fake child returns malformed protocol; no availability promoted.
}
#ifdef CK_TEST_ORT
TEST_CASE("per-graph alternatives retain precision and never promote BF16 or CPU")
{
	FakeProbe probe;
	std::array<SessionConfig, 3> alternatives;
	alternatives[0].bundle.backend = Backend::tensorrt;
	alternatives[0].precision = Precision::fp16;
	alternatives[1].bundle.backend = Backend::cuda;
	alternatives[1].precision = Precision::bf16;
	alternatives[2].bundle.backend = Backend::cpu_diagnostic;
	alternatives[2].precision = Precision::fp32;
	auto result = prepare_graph(alternatives, probe, {});
	CHECK_FALSE(result.session);
	REQUIRE(result.attempts.size() == 3);
	CHECK(result.attempts[0].precision == Precision::fp16);
	CHECK(result.attempts[1].failure.code == ErrorCode::unsupported);
	CHECK(result.attempts[2].failure.code == ErrorCode::unsupported);
	CHECK(probe.calls == std::vector<Backend>{Backend::tensorrt});
}
TEST_CASE("real optional CPU bundle probes before in-process IOBinding")
{
	const char *bundle_path = std::getenv("CK_TEST_BUNDLE");
	const char *graph_path = std::getenv("CK_TEST_GRAPH");
	const char *helper_path = std::getenv("CK_TEST_HELPER");
	if (!bundle_path || !graph_path || !helper_path)
		return;
	Temp temp;
	ProbeConfig probe_config{fs::absolute(helper_path), temp.path / "scratch", fs::absolute(graph_path)};
	probe_config.bundles[Backend::cpu_diagnostic] = fs::absolute(bundle_path);
	DisposableProbe probe(probe_config);
	SessionConfig config;
	config.bundle = read_bundle(bundle_path);
	config.graph = fs::absolute(graph_path);
	REQUIRE(config.bundle.backend == Backend::cpu_diagnostic);
	CHECK_FALSE(compile_in_helper(config, {fs::absolute(helper_path), temp.path / "compile"}).failure);
	auto prepared = prepare_session(config, probe);
	REQUIRE_FALSE(prepared.probe.failure);
	REQUIRE(prepared.session);
	CHECK_FALSE(prepared.probe.provider.permits_live_inference);
	auto &session = *prepared.session;
	const auto &rt = session.runtime();
	const auto &api = rt.api();
	OrtMemoryInfo *memory = nullptr;
	rt.check(api.CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory));
	std::unique_ptr<OrtMemoryInfo, decltype(api.ReleaseMemoryInfo)> memory_guard(memory, api.ReleaseMemoryInfo);
	std::array<float, 4> input{1, 2, 3, 4}, output{};
	std::array<int64_t, 4> shape{1, 1, 2, 2};
	OrtValue *a = nullptr, *b = nullptr;
	rt.check(api.CreateTensorWithDataAsOrtValue(memory, input.data(), sizeof(input), shape.data(), 4,
						    ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &a));
	std::unique_ptr<OrtValue, decltype(api.ReleaseValue)> a_guard(a, api.ReleaseValue);
	rt.check(api.CreateTensorWithDataAsOrtValue(memory, output.data(), sizeof(output), shape.data(), 4,
						    ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &b));
	std::unique_ptr<OrtValue, decltype(api.ReleaseValue)> b_guard(b, api.ReleaseValue);
	std::array<std::pair<std::string, const OrtValue *>, 1> inputs{{{"input", a}}}, outputs{{{"output", b}}};
	session.run_bound(inputs, outputs);
	CHECK(output == std::array<float, 4>{1, 2.25f, 3.5f, 5});
	std::stop_source cancelled;
	cancelled.request_stop();
	CHECK_THROWS(session.run_bound(inputs, outputs, cancelled.get_token()));
	CHECK_NOTHROW(session.run_bound(inputs, outputs));
}
TEST_CASE("ORT negotiates older API without rejecting newer version text and detects collisions")
{
	Temp temp;
	fs::create_directories(temp.path / "one");
	fs::create_directories(temp.path / "two");
#ifdef _WIN32
	const char *name = "onnxruntime.dll";
#else
	const char *name = "libonnxruntime.so";
#endif
	fs::copy_file(CK_MOCK_RUNTIME, temp.path / "one" / name);
	fs::copy_file(CK_MOCK_RUNTIME, temp.path / "two" / name);
	Bundle b;
	b.root = fs::canonical(temp.path / "one");
	b.runtime = b.root / name;
	b.registration = Registration::cpu;
	b.file_hashes[name] = file_sha256(b.runtime);
	auto rt = Runtime::load(b);
	CHECK(rt->api_version() == 19);
	CHECK(rt->build() == "999.0.0");
	CHECK(Runtime::load(b) == rt);
	b.root = fs::canonical(temp.path / "two");
	b.runtime = b.root / name;
	CHECK_THROWS_WITH(Runtime::load(b), doctest::Contains("collision"));
}
#endif
int main(int argc, char **argv)
{
#ifdef _WIN32
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
	executable = fs::absolute(argv[0]);
	if (argc == 5 && std::string_view(argv[1]) == "--request") {
		std::ifstream in(corridorkey::utf8_path(argv[2]));
		auto request = json::parse(in);
		const auto mode = request.value("test", "ok");
		if (mode == "sleep")
			std::this_thread::sleep_for(std::chrono::seconds(10));
		if (mode == "crash") {
#ifdef _WIN32
			TerminateProcess(GetCurrentProcess(), 0xc0000005);
#else
			raise(SIGSEGV);
#endif
		}
		write(corridorkey::utf8_path(argv[4]), {{"bad_protocol", true}});
		return 0;
	}
	doctest::Context context(argc, argv);
	return context.run();
}
