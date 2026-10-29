// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/ck2_ort.hpp"
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
Json compare(const ck2::Tensor &actual, const ck2::Tensor &expected)
{
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
std::unique_ptr<ck2::OrtGraph> graph(const ck2::DevelopmentConfig &config, ck2::Stage stage, Extent grid,
				     Backend backend, Json &report)
{
	const auto name = ck2::graph_name(stage, grid);
	const auto path = config.models / (name + ".onnx");
	ck2::verify_manifest(config.models / (name + ".json"), stage, grid);
	ort::SessionConfig session;
	session.bundle = ort::read_bundle(config.runtime.probe.bundles.at(backend));
	session.graph = path;
	session.obs_adapter = config.adapter;
	session.precision = (stage == ck2::Stage::moge || stage == ck2::Stage::rvm || backend != Backend::tensorrt)
				    ? Precision::fp32
				    : Precision::fp16;
	session.cache_root = config.runtime.cache;
	session.cache.manifest_hash = ort::file_sha256(config.models / (name + ".json"));
	session.cache.weights_hash = ort::file_sha256(config.models / (name + ".weights"));
	session.cache.graph_hash = ort::file_sha256(path);
	session.cache.recipe_version = "export-json-local-validation";
	session.persistent_io = backend == Backend::tensorrt || backend == Backend::cuda;
	const bool captured = backend == Backend::tensorrt && stage != ck2::Stage::moge && config.runtime.capture;
	if (backend == Backend::tensorrt)
		session.options["trt_cuda_graph_enable"] = captured ? "1" : "0";
	for (const auto &[tensor_name, tensor] : ck2::input_contract(stage, grid)) {
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
	return std::make_unique<ck2::OrtGraph>(std::move(native), stage, grid,
					       backend == Backend::tensorrt || backend == Backend::cuda);
}
ck2::Stage parse_stage(const std::string &name)
{
	if (name == "moge")
		return ck2::Stage::moge;
	if (name == "rvm")
		return ck2::Stage::rvm;
	if (name == "stage1nomask")
		return ck2::Stage::stage1;
	if (name == "stage2nomask")
		return ck2::Stage::stage2;
	throw std::runtime_error("Unknown validation stage");
}
} // namespace
int main(int argc, char **argv)
{
	if (argc < 7) {
		std::cerr
			<< "Usage: ck2_validate CONFIG parity|chain|clip HEIGHT STAGE|INPUT.npy OUTPUT.json BACKEND [quality]\n";
		return 2;
	}
	Json report;
	try {
		const auto config = ck2::read_development_config(utf8_path(argv[1]));
		const std::string mode = argv[2];
		const Extent grid{512, static_cast<unsigned>(std::stoul(argv[3]))};
		const auto backend = ort::parse_backend(argv[6]);
		const bool quality = argc > 7 && std::string(argv[7]) == "quality";
		report["mode"] = mode;
		report["grid"] = {grid.height, grid.width};
		if (mode == "parity") {
			const auto stage = parse_stage(argv[4]);
			const auto name = ck2::graph_name(stage, grid);
			auto runner = graph(config, stage, grid, backend, report);
			auto inputs = ck2::input_contract(stage, grid);
			ck2::Inputs bindings;
			for (auto &[tensor_name, tensor] : inputs) {
				tensor = reference(config.models / (name + ".ref.npz"), "in_" + tensor_name,
						   config.runtime.compiler.scratch);
				bindings[tensor_name] = &tensor;
			}
			ck2::Tensors outputs;
			std::vector<double> times;
			for (int iteration = 0; iteration < 6; ++iteration) {
				const auto start = Clock::now();
				outputs = runner->run(bindings, {});
				times.push_back(
					std::chrono::duration<double, std::milli>(Clock::now() - start).count());
			}
			report["run_ms"] = times;
			for (const auto &[tensor_name, tensor] : outputs)
				report["outputs"][tensor_name] = compare(
					tensor, reference(config.models / (name + ".ref.npz"), "out_" + tensor_name,
							  config.runtime.compiler.scratch));
			report["partitions"] = runner->finish_profile();
		} else if (mode == "chain" || mode == "clip") {
			ck2::Graphs runners;
			for (auto stage : {ck2::Stage::moge, ck2::Stage::rvm, ck2::Stage::stage1, ck2::Stage::stage2})
				if (stage != ck2::Stage::stage2 || quality)
					runners.emplace(stage, graph(config, stage, grid, backend, report));
			std::vector<ck2::OrtGraph *> profiles;
			for (auto &[stage, runner] : runners) {
				(void)stage;
				profiles.push_back(static_cast<ck2::OrtGraph *>(runner.get()));
			}
			ck2::SequenceState sequence(grid, quality, std::move(runners));
			const auto source_name = ck2::graph_name(ck2::Stage::stage1, grid);
			auto source = mode == "chain" ? reference(config.models / (source_name + ".ref.npz"), "in_rgb",
								  config.runtime.compiler.scratch)
						      : npy(utf8_path(argv[4]));
			if (source.shape.size() != 4 || source.shape[1] != 3 || source.shape[2] != grid.height ||
			    source.shape[3] != grid.width)
				throw std::runtime_error("Clip must be NCHW FP32 sRGB at the selected grid");
			const auto frames = mode == "chain" ? 3 : source.shape[0];
			FramePool pool;
			auto settings = std::make_shared<Settings>();
			settings->quality.grid = grid;
			settings->quality.profile = quality              ? Profile::quality
						    : grid.height == 256 ? Profile::facecam
									 : Profile::balanced;
			settings->quality.roi_enabled =
				false; // Grid-only clip fixture has no native image for recropping.
			ck2::LowResolutionResult decoded;
			std::ofstream alpha_file(utf8_path(std::string(argv[5]) + ".alpha.f32"), std::ios::binary);
			std::ofstream foreground_file(utf8_path(std::string(argv[5]) + ".fg.f32"), std::ios::binary);
			for (std::int64_t i = 0; i < frames; ++i) {
				ck2::Tensor rgb({1, 3, grid.height, grid.width});
				const auto offset = mode == "chain" ? 0
								    : static_cast<std::size_t>(i) * rgb.values.size();
				std::copy_n(source.values.begin() + static_cast<std::ptrdiff_t>(offset),
					    rgb.values.size(), rgb.values.begin());
				InputFrame frame;
				frame.lease = std::move(*pool.try_acquire());
				frame.lease.transition(FrameState::ready);
				frame.identity = {1, pool.generation(), static_cast<Sequence>(i + 1),
						  static_cast<std::uint64_t>(i + 1),
						  TimePoint{} + std::chrono::milliseconds(i * 16)};
				frame.settings = settings;
				frame.analysis = ck2::packed_image(rgb, PixelFormat::rgb_f32);
				frame.original = frame.analysis;
				frame.transform.source_size = grid;
				const auto start = Clock::now();
				decoded = sequence.process(frame);
				const auto &t = decoded.timings;
				report["frames"].push_back(
					{{"index", i},
					 {"total_ms",
					  std::chrono::duration<double, std::milli>(Clock::now() - start).count()},
					 {"moge_ms", t.moge_ms},
					 {"rvm_ms", t.rvm_ms},
					 {"stage1_ms", t.stage1_ms},
					 {"stage2_ms", t.stage2_ms},
					 {"moge_refreshed", t.moge_refreshed}});
				alpha_file.write(
					reinterpret_cast<const char *>(decoded.alpha.values.data()),
					static_cast<std::streamsize>(decoded.alpha.values.size() * sizeof(float)));
				foreground_file.write(
					reinterpret_cast<const char *>(decoded.foreground.values.data()),
					static_cast<std::streamsize>(decoded.foreground.values.size() * sizeof(float)));
				if (!alpha_file || !foreground_file)
					throw std::runtime_error("Cannot write decoded clip outputs");
			}
			if (mode == "chain") {
				bool comparable = true;
				if (quality) {
					const auto baton = reference(config.models / (source_name + ".ref.npz"),
								     "out_baton", config.runtime.compiler.scratch);
					const auto expected_baton =
						reference(config.models / (ck2::graph_name(ck2::Stage::stage2, grid) +
									   ".ref.npz"),
							  "in_baton", config.runtime.compiler.scratch);
					const auto difference = compare(baton, expected_baton);
					comparable = difference.at("max_abs").get<double>() == 0;
					report["reference_baton_difference"] = difference;
				}
				report["reference_chain_compatible"] = comparable;
				if (comparable) {
					const auto expected =
						config.models /
						(ck2::graph_name(quality ? ck2::Stage::stage2 : ck2::Stage::stage1,
								 grid) +
						 ".ref.npz");
					report["outputs"]["alpha"] =
						compare(decoded.alpha, reference(expected, "out_alpha",
										 config.runtime.compiler.scratch));
					report["outputs"]["fg"] =
						compare(decoded.foreground,
							reference(expected, "out_fg", config.runtime.compiler.scratch));
				} else {
					report["reference_chain_warning"] =
						"Stage 2 reference input baton differs from Stage 1 nomask reference output; standalone graph parity is valid, chained output comparison is not.";
				}
			}
			for (auto *profile : profiles)
				report["partitions"].push_back(profile->finish_profile());
		} else
			throw std::runtime_error("Unknown validation mode");
		report["ok"] = true;
	} catch (const std::exception &e) {
		report["ok"] = false;
		report["error"] = e.what();
	}
	std::ofstream output(utf8_path(argv[5]));
	output << report.dump(2) << '\n';
	std::cout << report.dump(2) << '\n';
	return report.value("ok", false) ? 0 : 1;
}
