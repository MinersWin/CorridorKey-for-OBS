// SPDX-License-Identifier: GPL-3.0-or-later
#include "ck1_ort.hpp"
#include "util/utf8_path.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <set>

namespace corridorkey::ck1 {
using ck2::RuntimeConfig;
namespace {
using Json = nlohmann::json;
class RecordedProbe final : public ProviderProbe {
public:
	explicit RecordedProbe(const RuntimeConfig &runtime) : probe_(runtime.probe), record_(runtime.record_probe) {}
	ProbeResult probe(const ProbeRequest &request, std::stop_token stop) override
	{
		// One preparation uses one adapter/bundle per backend. Reuse its precision test across stages.
		const auto key = std::pair{request.backend, request.precision};
		if (const auto found = results_.find(key); found != results_.end() && !stop.stop_requested())
			return found->second;
		auto result = probe_.probe(request, stop);
		if (record_)
			record_(result);
		if (!result.failure)
			results_.emplace(key, result);
		return result;
	}

private:
	ort::DisposableProbe probe_;
	std::function<void(const ProbeResult &)> record_;
	std::map<std::pair<Backend, Precision>, ProbeResult> results_;
};
} // namespace
static void verify_manifest(const std::filesystem::path &path, const std::string &name, Extent grid, bool modnet)
{
	if (std::filesystem::file_size(path) > 4 * 1024 * 1024)
		throw std::runtime_error("CK1 manifest too large");
	std::ifstream stream(path);
	const auto manifest = Json::parse(stream);
	if (manifest.at("graph") != name || manifest.at("H") != grid.height || manifest.at("W") != grid.width)
		throw std::runtime_error("CK1 graph identity differs from selected grid/screen");
	for (const auto &direction : {"inputs", "outputs"}) {
		const auto expected = std::string_view(direction) == "inputs" ? input_contract(grid, modnet)
									      : output_contract(grid, modnet);
		const auto &specs = manifest.at(direction);
		std::set<std::string> seen;
		if (!specs.is_array() || specs.size() != expected.size())
			throw std::runtime_error("CK1 manifest I/O count differs");
		for (const auto &spec : specs) {
			const auto tensor = spec.at("name").get<std::string>();
			if (!expected.contains(tensor) || !seen.insert(tensor).second ||
			    spec.at("dtype") != "float32" || spec.at("shape") != expected.at(tensor).shape)
				throw std::runtime_error("CK1 manifest differs from static FP32 contract");
		}
	}
}
Ck1Prepared prepare_ort(const PrepareRequest &request, const RuntimeConfig &runtime, ChromaHint chroma)
{
	if (!request.model || !request.model->lease || !request.settings || !Ck1Pipeline::supports(*request.settings))
		throw std::runtime_error("No supported, leased CK1 package is ready");
	const auto analysis_grid = request.settings->quality.grid;
	RecordedProbe probe(runtime);
	Ck1Prepared result;
	result.model = request.model;
	result.graphs.chroma = std::move(chroma);
	std::filesystem::path selected_core;
	std::ostringstream description;
	const auto key_name = graph_name(request.settings->quality.screen_color, analysis_grid);
	std::vector<std::string> names{key_name};
	if (request.settings->hint.source == HintSource::modnet)
		names.push_back("modnet_" + std::to_string(analysis_grid.width));
	for (const auto &name : names) {
		const bool modnet = name.starts_with("modnet_");
		const auto grid = analysis_grid;
		const auto preparing_at = Clock::now();
		if (runtime.info)
			runtime.info("Preparing " + name);
		const GraphArtifact *artifact = nullptr;
		for (const auto &candidate : request.model->graphs)
			if (candidate.role == name || candidate.graph_path.stem() == name) {
				artifact = &candidate;
				break;
			}
		if (!artifact)
			throw std::runtime_error("CK1 model is missing required graph " + name);
		auto metadata = artifact->graph_path;
		metadata.replace_extension(".json");
		verify_manifest(metadata, name, grid, modnet);
		std::vector<ort::SessionConfig> alternatives;
		for (const auto backend : {Backend::tensorrt, Backend::cuda, Backend::directml}) {
			if (request.settings->backend.backend != Backend::automatic &&
			    request.settings->backend.backend != backend)
				continue;
			if (!runtime.probe.bundles.contains(backend))
				continue;
			ort::SessionConfig config;
			config.bundle = ort::read_bundle(runtime.probe.bundles.at(backend));
			if (!selected_core.empty() &&
			    std::filesystem::weakly_canonical(config.bundle.runtime) != selected_core)
				continue;
			config.model_lease = request.model;
			config.graph = artifact->graph_path;
			config.precision = backend != Backend::tensorrt ? Precision::fp32 : Precision::fp16;
			config.obs_adapter = request.transport.adapter;
			config.cache_root = runtime.cache;
			config.cache.manifest_hash = ort::file_sha256(metadata);
			config.cache.graph_hash = artifact->graph_sha256;
			config.cache.weights_hash = artifact->weights_sha256;
			config.cache.recipe_version = artifact->recipe_version;
			for (const auto &[tensor_name, tensor] : input_contract(grid, modnet)) {
				(void)tensor_name;
				config.cache.shapes.insert(config.cache.shapes.end(), tensor.shape.begin(),
							   tensor.shape.end());
			}
			// CPU-bound DirectML inputs must be rebound after host writes; ORT may cache the upload.
			config.persistent_io = backend == Backend::tensorrt || backend == Backend::cuda;
			if (!runtime.profiles.empty())
				config.profile_prefix = runtime.profiles / name;
			if (backend == Backend::tensorrt && runtime.capture) {
				auto captured = config;
				captured.options["trt_cuda_graph_enable"] = "1";
				alternatives.push_back(std::move(captured));
			}
			if (backend == Backend::tensorrt)
				config.options["trt_cuda_graph_enable"] = "0";
			alternatives.push_back(std::move(config));
		}
		auto prepared = ort::prepare_graph(alternatives, probe, runtime.compiler, request.cancellation);
		if (!prepared.session) {
			std::string detail = "CK1 " + name + " unavailable; passthrough. ";
			for (const auto &attempt : prepared.attempts)
				detail += ort::backend_name(attempt.backend) + ": " + attempt.detail + "; ";
			throw std::runtime_error(detail);
		}
		for (const auto &config : alternatives)
			if (config.bundle.backend == prepared.provider.backend) {
				selected_core = std::filesystem::weakly_canonical(config.bundle.runtime);
				break;
			}
		if (!modnet)
			result.backend = prepared.provider.backend;
		description << name << ": " << ort::backend_name(prepared.provider.backend)
			    << (prepared.provider.backend != Backend::tensorrt ? " FP32; " : " FP16; ");
		const auto selected = alternatives.at(prepared.attempts.size() - 1);
		const bool captured = selected.options.contains("trt_cuda_graph_enable") &&
				      selected.options.at("trt_cuda_graph_enable") == "1";
		const bool device = prepared.provider.backend == Backend::tensorrt ||
				    prepared.provider.backend == Backend::cuda;
		if (runtime.info)
			runtime.info(
				name + " ready in " +
				std::to_string(
					std::chrono::duration<double, std::milli>(Clock::now() - preparing_at).count()) +
				" ms; capture=" + (captured ? "on" : "off"));
		if (captured)
			description << "CUDA graph capture enabled; ";
		for (const auto &attempt : prepared.attempts)
			if (attempt.failure && runtime.info)
				runtime.info(name +
					     ": preparation failed; trying the next capture/provider alternative: " +
					     attempt.detail);
		std::function<std::unique_ptr<ort::Session>()> recreate;
		std::function<std::unique_ptr<ort::Session>(const std::string &, std::stop_token)> fallback;
		if (captured) {
			recreate = [selected] {
				return std::make_unique<ort::Session>(selected);
			};
			fallback = [selected, runtime, name](const std::string &reason, std::stop_token stop) {
				if (runtime.info)
					runtime.info(
						name +
						": CUDA graph capture failed during inference; retrying without capture: " +
						reason);
				auto uncaptured = selected;
				uncaptured.options["trt_cuda_graph_enable"] = "0";
				RecordedProbe probe(runtime);
				auto prepared = ort::prepare_graph(std::span<const ort::SessionConfig>(&uncaptured, 1),
								   probe, runtime.compiler, stop);
				if (!prepared.session)
					throw std::runtime_error("Uncaptured TensorRT recovery failed");
				return std::move(prepared.session);
			};
		}
		auto graph = std::make_unique<ck2::OrtGraph>(std::move(prepared.session), input_contract(grid, modnet),
							     output_contract(grid, modnet), device, std::move(recreate),
							     std::move(fallback));
		graph->device_resident(runtime.interop && device &&
				       request.settings->hint.source != HintSource::chroma &&
				       (request.settings->backend.transport == TransportKind::automatic ||
					request.settings->backend.transport == TransportKind::interop));
		if (auto context = graph->device_context()) {
			try {
				context->ensure_kernels();
			} catch (const std::exception &e) {
				graph->device_resident(false);
				if (runtime.info)
					runtime.info(std::string("Device conversion unavailable; staged tensors: ") +
						     e.what());
			}
		}
		if (captured) {
			auto zeros = input_contract(grid, modnet);
			Inputs warm;
			for (const auto &[input_name, tensor] : zeros)
				warm.emplace(input_name, &tensor);
			graph->run(warm, request.cancellation);
			graph->run(warm, request.cancellation);
		}
		if (modnet)
			result.graphs.modnet = std::move(graph);
		else
			result.graphs.key = std::move(graph);
	}
	if (request.settings->hint.source == HintSource::ck2_stage1) {
		auto hint_request = request;
		auto settings = std::make_shared<Settings>(*request.settings);
		settings->quality.model = ModelFamily::ck2;
		settings->quality.grid = {512, 512};
		settings->quality.profile = Profile::custom;
		settings->quality.refiner_enabled = false;
		settings->quality.roi_enabled = false;
		settings->hint.ck2_mode = HintMode::off;
		hint_request.settings = settings;
		auto prepared = ck2::prepare_ort(hint_request, runtime);
		result.graphs.stage1 =
			std::make_unique<ck2::SequenceState>(Extent{512, 512}, false, std::move(prepared.graphs));
		description << "CK2 hint runs MoGe + RVM + Stage 1 at 512x512 on this capture; " << prepared.detail;
	}
	result.detail = "CK1 Experimental; " + description.str() +
			"Native reconstruction; edge refiner off. Hint and CK1 time are included in inference.";
	return result;
}
#ifdef CORRIDORKEY_CK2_DEV
std::shared_ptr<const ReadyModel> development_model(const std::filesystem::path &directory, const Settings &settings,
						    const std::filesystem::path &release_fixture)
{
	struct LocalLease final : ModelReadLease {};
	auto model = std::make_shared<ReadyModel>();
	model->key = {"ck1-local-validation", "unpublished"};
	model->lease = std::make_shared<LocalLease>();
	std::vector<std::string> names{graph_name(settings.quality.screen_color, settings.quality.grid)};
	if (settings.hint.source == HintSource::modnet)
		names.push_back("modnet_" + std::to_string(settings.quality.grid.width));
	Json release;
	if (!release_fixture.empty()) {
		std::ifstream input(release_fixture);
		release = Json::parse(input);
		model->key.revision = release.at("revision").get<std::string>();
	}
	std::string identity;
	for (const auto &name : names) {
		GraphArtifact a;
		a.role = name;
		a.graph_path = directory / (name + ".onnx");
		a.weights_path = directory / (name + ".weights");
		a.graph_sha256 = ort::file_sha256(a.graph_path);
		a.weights_sha256 = ort::file_sha256(a.weights_path);
		const auto metadata_hash = ort::file_sha256(directory / (name + ".json"));
		if (!release.is_null() && (release.at("sha256").at(name + ".onnx") != a.graph_sha256 ||
					   release.at("sha256").at(name + ".json") != metadata_hash))
			throw std::runtime_error("Local CK1 graph differs from revision-4 release");
		a.recipe_version = "export-json-local-validation";
		identity += metadata_hash;
		model->graphs.push_back(std::move(a));
	}
	if (settings.hint.source == HintSource::ck2_stage1) {
		auto hint = ck2::development_model(directory, {512, 512}, false);
		model->graphs.insert(model->graphs.end(), hint->graphs.begin(), hint->graphs.end());
		identity += hint->manifest_sha256;
	}
	model->manifest_sha256 = ort::sha256(identity);
	return model;
}
#endif
} // namespace corridorkey::ck1
