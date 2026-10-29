// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/ort_session.hpp"
#include "engine/providers/process.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <regex>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace corridorkey;
using namespace corridorkey::ort;
using nlohmann::json;
namespace {
double milliseconds(TimePoint start)
{
	return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::string redact(std::string text)
{
	text = std::regex_replace(text, std::regex(R"((?:[A-Za-z]:[\\/]|\\\\)[^\r\n\t ]*)"), "<path>");
	text = std::regex_replace(text, std::regex(R"(/[^\r\n\t ]+)"), "<path>");
	if (text.size() > 2048)
		text.resize(2048);
	return text;
}
struct Tensor {
	const OrtApi &api;
	OrtValue *value = nullptr;
	std::array<float, 4> fp32{};
	std::array<std::uint16_t, 4> fp16{};
	~Tensor()
	{
		if (value)
			api.ReleaseValue(value);
	}
};
json execute(const json &job)
{
	if (job.at("schema") != 1)
		throw std::runtime_error("Unsupported probe protocol");
	SessionConfig config;
	config.bundle = read_bundle(fs::u8path(job.at("bundle").get<std::string>()));
	config.graph = fs::u8path(job.at("graph").get<std::string>());
	config.obs_adapter.stable_id = job.value("adapter", "");
	config.require_adapter_match = job.value("require_adapter_match", true);
	config.prebuilt_context = job.value("prebuilt_context", false);
	config.precision = static_cast<Precision>(job.value("precision", static_cast<int>(Precision::fp32)));
	if (config.precision != Precision::fp32 && config.precision != Precision::fp16 &&
	    config.precision != Precision::bf16)
		throw std::runtime_error("Unsupported probe precision");
	config.cache_root = fs::u8path(job.value("cache", ""));
	config.profile_prefix = fs::u8path(job.value("profile", ""));
	config.options = job.value("options", decltype(config.options){});
	config.session_options = job.value("session_options", decltype(config.session_options){});
	config.cache.graph_hash = job.value("graph_hash", "");
	config.cache.shapes = job.value("shapes", decltype(config.cache.shapes){});
	config.cache.tile_batch = job.value("tile_batch", 0u);
	config.cache.manifest_hash = job.value("manifest_hash", "synthetic-fixture-v1");
	config.cache.weights_hash = job.value("weights_hash", "");
	config.cache.recipe_version = job.value("recipe_version", "1");
	const auto mode = job.value("mode", "probe");
	if (mode != "probe" && mode != "build")
		throw std::runtime_error("Unknown helper mode");
	config.diagnostic_only = mode == "probe";
	if (!config.cache_root.empty() && config.cache.weights_hash.empty())
		throw std::runtime_error("Persistent cache requires a verified aggregate weights hash");
	const auto start = Clock::now();
	Session session(config);
	const auto build_ms = milliseconds(start);
	json result{
		{"schema", 1},
		{"ok", true},
		{"backend", backend_name(config.bundle.backend)},
		{"mode", mode},
		{"api", session.runtime().api_version()},
		{"runtime", session.runtime().build()},
		{"provider_build", config.bundle.provider_build},
		{"adapter", session.device().adapter.stable_id},
		{"device", session.device().adapter.display_name},
		{"vendor", session.device().adapter.vendor_id},
		{"driver", session.device().driver},
		{"architecture", session.device().architecture},
		{"precision", static_cast<int>(config.precision)},
		{"session_build_ms", build_ms},
		{"gpu_partition_verified", false},
		{"advisory",
		 "This result validates only the supplied standard-op fixture; model and transport validation are separate. Unknown/newer runtime combinations are attempted."}};
	if (mode == "build") {
		result["advisory"] =
			"Static graph compilation only; numerical parity and first-run kernels remain unverified";
		session.finish_build();
		session.close();
		result["detail"] =
			"Session compilation completed; first-run JIT and model parity still require validation";
		return result;
	}
	if (session.inputs().size() != 1 || session.outputs().size() != 1 ||
	    session.inputs()[0].shape != std::vector<std::int64_t>{1, 1, 2, 2} ||
	    session.outputs()[0].shape != session.inputs()[0].shape)
		throw std::runtime_error("Probe requires the W01 standard-op Add fixture");
	const auto &rt = session.runtime();
	const auto &api = rt.api();
	const auto type = config.precision == Precision::fp32   ? ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT
			  : config.precision == Precision::fp16 ? ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16
								: ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16;
	if (session.inputs()[0].type != type || session.outputs()[0].type != type)
		throw std::runtime_error("Fixture dtype does not match requested precision");
	OrtMemoryInfo *memory = nullptr;
	rt.check(api.CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory));
	std::unique_ptr<OrtMemoryInfo, decltype(api.ReleaseMemoryInfo)> memory_guard(memory, api.ReleaseMemoryInfo);
	Tensor input{api}, output{api};
	input.fp32 = {1, 2, 3, 4};
	input.fp16 = config.precision == Precision::bf16 ? std::array<std::uint16_t, 4>{0x3f80, 0x4000, 0x4040, 0x4080}
							 : std::array<std::uint16_t, 4>{0x3c00, 0x4000, 0x4200, 0x4400};
	for (auto *tensor : {&input, &output}) {
		void *data = type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ? static_cast<void *>(tensor->fp32.data())
									 : tensor->fp16.data();
		const size_t bytes = type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ? 16 : 8;
		rt.check(api.CreateTensorWithDataAsOrtValue(memory, data, bytes, session.inputs()[0].shape.data(), 4,
							    type, &tensor->value));
	}
	std::array<std::pair<std::string, const OrtValue *>, 1> inputs{{{session.inputs()[0].name, input.value}}};
	std::array<std::pair<std::string, const OrtValue *>, 1> outputs{{{session.outputs()[0].name, output.value}}};
	const auto first = Clock::now();
	session.run_bound(inputs, outputs);
	result["first_run_ms"] = milliseconds(first);
	std::vector<double> timings;
	const int iterations = job.value("iterations", 5);
	if (iterations < 1 || iterations > 30)
		throw std::runtime_error("Probe iterations must be between 1 and 30");
	for (int i = 0; i < iterations; ++i) {
		const auto tick = Clock::now();
		session.run_bound(inputs, outputs);
		timings.push_back(milliseconds(tick));
	}
	if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
		if (output.fp32 != std::array<float, 4>{1, 2.25f, 3.5f, 5})
			throw std::runtime_error("Standard-op output failed exact numeric parity");
	} else {
		const auto expected = config.precision == Precision::bf16
					      ? std::array<std::uint16_t, 4>{0x3f80, 0x4010, 0x4060, 0x40a0}
					      : std::array<std::uint16_t, 4>{0x3c00, 0x4080, 0x4300, 0x4500};
		if (output.fp16 != expected)
			throw std::runtime_error("Standard-op output failed exact numeric parity");
	}
	std::sort(timings.begin(), timings.end());
	result["warm_median_ms"] = timings[timings.size() / 2];
	result["warm_samples_ms"] = timings;
	const auto partitions = session.finish_profile();
	result["partition_events"] = partitions;
	bool gpu = false;
	for (const auto &[provider, count] : partitions) {
		if (provider == "CPUExecutionProvider" && config.bundle.backend != Backend::cpu_diagnostic)
			throw std::runtime_error("Live backend unexpectedly executed a CPU partition");
		if (provider == config.bundle.provider_name ||
		    (config.bundle.backend == Backend::tensorrt && provider == "CUDAExecutionProvider"))
			gpu = count > 0 || gpu;
	}
	if (config.bundle.backend != Backend::cpu_diagnostic && !gpu)
		throw std::runtime_error("No requested GPU provider execution was observed");
	result["gpu_partition_verified"] = gpu && config.bundle.backend != Backend::cpu_diagnostic;
	result["live_precision_allowed"] = config.precision != Precision::bf16;
	session.close();
	return result;
}
int run(const fs::path &request, const fs::path &response)
{
	json result;
	std::string backend = "unknown";
	try {
		if (fs::file_size(request) > 1024 * 1024)
			throw std::runtime_error("Probe request too large");
		std::ifstream file(request);
		auto job = json::parse(file);
		backend = backend_name(read_bundle(fs::u8path(job.at("bundle").get<std::string>())).backend);
		result = execute(job);
	} catch (const std::exception &e) {
		result = {{"schema", 1}, {"ok", false}, {"backend", backend}, {"detail", redact(e.what())}};
	}
	try {
		const auto temporary = fs::path(response.native() + fs::path(".tmp").native());
		{
			std::ofstream out(temporary);
			out << result.dump(2) << '\n';
			if (!out)
				return 3;
		}
		fs::rename(temporary, response);
	} catch (...) {
		return 3;
	}
	return result.at("ok").get<bool>() ? 0 : 2;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
{
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	if (argc == 5 && std::wstring_view(argv[1]) == L"--supervise") {
		const auto result = run_helper(fs::absolute(argv[0]), fs::absolute(argv[2]), fs::absolute(argv[3]),
					       std::chrono::milliseconds(std::wcstol(argv[4], nullptr, 10)), {});
		if (result.timed_out || result.crashed || !result.started) {
			std::ofstream out{fs::path(argv[3])};
			out << json{{"schema", 1},
				    {"ok", false},
				    {"timed_out", result.timed_out},
				    {"process_crashed", result.crashed}}
					.dump(2);
			return 4;
		}
		return result.exit_code;
	}
	if (argc != 5 || std::wstring_view(argv[1]) != L"--request" || std::wstring_view(argv[3]) != L"--response")
		return 1;
	return run(argv[2], argv[4]);
}
#else
int main(int argc, char **argv)
{
	if (argc == 5 && std::string_view(argv[1]) == "--supervise") {
		const auto result = run_helper(fs::absolute(argv[0]), fs::absolute(argv[2]), fs::absolute(argv[3]),
					       std::chrono::milliseconds(std::strtol(argv[4], nullptr, 10)), {});
		if (result.timed_out || result.crashed || !result.started) {
			std::ofstream out{fs::path(argv[3])};
			out << json{{"schema", 1},
				    {"ok", false},
				    {"timed_out", result.timed_out},
				    {"process_crashed", result.crashed}}
					.dump(2);
			return 4;
		}
		return result.exit_code;
	}
	if (argc != 5 || std::string_view(argv[1]) != "--request" || std::string_view(argv[3]) != "--response")
		return 1;
	return run(argv[2], argv[4]);
}
#endif
