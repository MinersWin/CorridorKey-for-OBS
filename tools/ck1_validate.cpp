// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/ck1_ort.hpp"
#include "models/tensor_storage.hpp"
#include "util/utf8_path.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

using namespace corridorkey;
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace {
ck2::Tensor npy(const fs::path &path)
{
	models::storage::File file(path);
	const auto info = models::storage::read_npy(file);
	if (info.dtype != "float32")
		throw std::runtime_error("Validation inputs must be FP32 NPY");
	ck2::Tensor value(std::vector<std::int64_t>(info.shape.begin(), info.shape.end()));
	file.read(info.offset,
		  std::span<char>(reinterpret_cast<char *>(value.values.data()), value.values.size() * sizeof(float)));
	return value;
}
ck2::Tensor reference(const fs::path &archive, const std::string &name, const fs::path &scratch)
{
	models::storage::File file(archive);
	models::storage::Npz zip(file);
	fs::create_directories(scratch);
	const auto path = scratch / (name + ".npy");
	{
		std::ofstream output(path, std::ios::binary);
		zip.extract(zip.at(name),
			    [&](std::span<const char> bytes) {
				    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			    },
			    {});
		if (!output)
			throw std::runtime_error("Cannot extract validation reference");
	}
	auto tensor = npy(path);
	fs::remove(path);
	return tensor;
}
Json compare(const ck2::Tensor &resident, const ck2::Tensor &expected)
{
	const auto actual = ck2::download_tensor(resident);
	ck2::check_tensor(actual, expected);
	double maximum = 0, sum = 0;
	for (std::size_t i = 0; i < actual.values.size(); ++i) {
		const auto difference = std::abs(static_cast<double>(actual.values[i]) - expected.values[i]);
		maximum = std::max(maximum, difference);
		sum += difference;
	}
	return {{"max_abs", maximum},
		{"mean_abs", sum / static_cast<double>(actual.values.size())},
		{"elements", actual.values.size()}};
}
std::unique_ptr<ck2::OrtGraph> graph(const ck2::DevelopmentConfig &config, const std::string &name, Extent grid,
				     Backend backend, Json &report)
{
	const bool modnet = name.starts_with("modnet_");
	const auto path = config.models / (name + ".onnx");

	ort::SessionConfig session;
	session.bundle = ort::read_bundle(config.runtime.probe.bundles.at(backend));
	session.graph = path;
	session.obs_adapter = config.adapter;
	session.precision = backend == Backend::tensorrt ? Precision::fp16 : Precision::fp32;
	session.cache_root = config.runtime.cache;
	session.cache.manifest_hash = ort::file_sha256(config.models / (name + ".json"));
	session.cache.weights_hash = ort::file_sha256(config.models / (name + ".weights"));
	session.cache.graph_hash = ort::file_sha256(path);
	session.cache.recipe_version = "export-json-local-validation";
	session.persistent_io = backend == Backend::tensorrt || backend == Backend::cuda;
	const bool captured = backend == Backend::tensorrt && config.runtime.capture;
	if (backend == Backend::tensorrt)
		session.options["trt_cuda_graph_enable"] = captured ? "1" : "0";
	for (const auto &[tensor_name, tensor] : ck1::input_contract(grid, modnet)) {
		(void)tensor_name;
		session.cache.shapes.insert(session.cache.shapes.end(), tensor.shape.begin(), tensor.shape.end());
	}
	if (!config.runtime.profiles.empty())
		session.profile_prefix = config.runtime.profiles / name;
	const auto start = Clock::now();
	auto native = std::make_unique<ort::Session>(session);
	report["graphs"][name] = {{"prepare_ms",
				   std::chrono::duration<double, std::milli>(Clock::now() - start).count()},
				  {"graph_sha256", session.cache.graph_hash},
				  {"weights_sha256", session.cache.weights_hash},
				  {"runtime", native->runtime().build()},
				  {"backend", ort::backend_name(backend)},
				  {"precision", session.precision == Precision::fp16 ? "fp16" : "fp32"},
				  {"capture", captured}};
	auto graph = std::make_unique<ck2::OrtGraph>(std::move(native), ck1::input_contract(grid, modnet),
						     ck1::output_contract(grid, modnet),
						     backend == Backend::tensorrt || backend == Backend::cuda);
	graph->device_resident(config.runtime.interop);
	report["graphs"][name]["resident"] = bool(graph->device_context());
	return graph;
}
} // namespace
int main(int argc, char **argv)
{
	if (argc != 7) {
		std::cerr
			<< "Usage: ck1_validate CONFIG parity|chain|ck2hint SIZE ck1green|ck1blue|modnet REPORT.json BACKEND\n";
		return 2;
	}
	Json report;
	try {
		const auto config = ck2::read_development_config(utf8_path(argv[1]));
		const auto dimension = static_cast<unsigned>(std::stoul(argv[3]));
		const Extent grid{dimension, dimension};
		const std::string kind = argv[4], name = kind + "_" + std::to_string(dimension);
		const std::string mode = argv[2];
		if ((mode != "parity" && mode != "chain" && mode != "ck2hint") ||
		    (kind != "ck1green" && kind != "ck1blue" && kind != "modnet") ||
		    (mode != "parity" && kind == "modnet"))
			throw std::invalid_argument("Unknown parity graph");
		(void)ck1::graph_name(ScreenColor::green, grid);
		report["graph"] = name;
		report["grid"] = {dimension, dimension};
		report["mode"] = mode;
		auto runner = mode == "ck2hint" ? nullptr
						: graph(config, name, grid, ort::parse_backend(argv[6]), report);
		auto *key_profile = runner.get();
		std::unique_ptr<ck1::SequenceState> sequence;
		ck2::OrtGraph *hint_profile = nullptr;
		InputFrame frame;
		if (mode != "parity") {
			ck1::Graphs graphs;
			if (mode == "chain") {
				graphs.key = std::move(runner);
				auto hint = graph(config, "modnet_" + std::to_string(dimension), grid,
						  ort::parse_backend(argv[6]), report);
				hint_profile = hint.get();
				graphs.modnet = std::move(hint);
			}
			auto settings = std::make_shared<Settings>();
			settings->quality.model = ModelFamily::ck1;
			settings->quality.grid = grid;
			settings->quality.screen_color = kind == "ck1blue" ? ScreenColor::blue : ScreenColor::green;
			settings->quality.refiner_enabled = false;
			settings->quality.roi_enabled = false;
			settings->hint.source = mode == "ck2hint" ? HintSource::ck2_stage1 : HintSource::modnet;
			settings->quality.guide_mode = GuideMode::rgb_guided;
			settings->quality.profile = Profile::custom;
			settings->backend.backend = ort::parse_backend(argv[6]);
			if (mode == "ck2hint") {
				PrepareRequest request;
				request.instance = request.generation = 1;
				request.settings = settings;
				request.transport.adapter = config.adapter;
				request.model =
					ck1::development_model(config.models, *settings, config.release_fixture);
				auto prepared = ck1::prepare_ort(request, config.runtime);
				key_profile = dynamic_cast<ck2::OrtGraph *>(prepared.graphs.key.get());
				report["detail"] = prepared.detail;
				report["reference"] =
					"The exporter reference uses MODNet. CK2 Stage-1 hint execution has no matching chained reference.";
				graphs = std::move(prepared.graphs);
			}
			frame.settings = settings;
			frame.identity.instance = 1;
			frame.identity.generation = 1;
			frame.transform.source_size = grid;
			frame.analysis = ck2::packed_image(reference(config.models / (name + ".ref.npz"), "in_rgb",
								     config.runtime.compiler.scratch),
							   PixelFormat::rgb_f32);
			if (auto context = key_profile->device_context()) {
				auto rgb = ck2::planar_rgb(frame.analysis);
				rgb.device = context->allocate(rgb.values.size() * sizeof(float));
				rgb.device->upload(rgb.values.data(), rgb.values.size() * sizeof(float));
				rgb.values.clear();
				frame.analysis = ck2::packed_image(rgb, PixelFormat::rgb_f32);
			}
			frame.original = frame.analysis;
			sequence = std::make_unique<ck1::SequenceState>(*settings, std::move(graphs));
		}
		auto inputs = ck1::input_contract(grid, kind == "modnet");
		ck2::Inputs bindings;
		for (auto &[key, value] : inputs) {
			value = reference(config.models / (name + ".ref.npz"), "in_" + key,
					  config.runtime.compiler.scratch);
			if (runner) {
				if (auto context = runner->device_context()) {
					value.device = context->allocate(value.values.size() * sizeof(float));
					value.device->upload(value.values.data(), value.values.size() * sizeof(float));
					value.values.clear();
				}
			}
			bindings[key] = &value;
		}
		ck2::Tensors outputs;
		std::vector<double> times;
		for (unsigned i = 0; i < 21; ++i) {
			const auto start = Clock::now();
			if (sequence) {
				frame.identity.sequence = i + 1;
				frame.identity.video_tick = i + 1;
				frame.identity.captured_at = Clock::now();
				auto result = sequence->process(frame);
				report["hint_ms"].push_back(result.timings.stage1_ms);
				report["key_ms"].push_back(result.timings.stage2_ms);
				outputs = {{"alpha", std::move(result.alpha)}, {"fg", std::move(result.foreground)}};
			} else
				outputs = runner->run(bindings, {});
			times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
		}
		report["run_ms"] = times;
		report["resident_outputs"] = bool(outputs.begin()->second.device);
		for (const auto &[key, value] : outputs) {
			if (mode == "ck2hint") {
				const auto host = ck2::download_tensor(value);
				const auto [lo, hi] = std::minmax_element(host.values.begin(), host.values.end());
				report["outputs"][key] = {{"min", *lo}, {"max", *hi}, {"elements", host.values.size()}};
			} else
				report["outputs"][key] =
					compare(value, reference(config.models / (name + ".ref.npz"), "out_" + key,
								 config.runtime.compiler.scratch));
		}
		report["partitions"][name] = key_profile->finish_profile();
		if (hint_profile)
			report["partitions"]["modnet_" + std::to_string(dimension)] = hint_profile->finish_profile();
	} catch (const std::exception &e) {
		report["error"] = e.what();
	}
	std::ofstream file(utf8_path(argv[5]));
	file << report.dump(2) << '\n';
	std::cout << report.dump(2) << '\n';
	return report.contains("error") ? 1 : 0;
}
