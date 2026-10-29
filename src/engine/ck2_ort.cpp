// SPDX-License-Identifier: GPL-3.0-or-later
#include "ck2_ort.hpp"
#include "util/utf8_path.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

namespace corridorkey::ck2 {
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
void verify_specs(const std::vector<ort::TensorSpec> &specs, const Tensors &expected)
{
	if (specs.size() != expected.size())
		throw std::runtime_error("Graph I/O count differs from export contract");
	for (const auto &spec : specs)
		if (!expected.contains(spec.name) || spec.type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
		    spec.shape != expected.at(spec.name).shape)
			throw std::runtime_error(
				"Graph requires the verified static FP32 I/O contract; EP precision is independent");
}
} // namespace
void verify_manifest(const std::filesystem::path &path, Stage stage, Extent grid)
{
	if (std::filesystem::file_size(path) > 4 * 1024 * 1024)
		throw std::runtime_error("CK2 graph manifest too large");
	std::ifstream stream(path);
	const auto manifest = Json::parse(stream);
	if (manifest.at("graph") != graph_name(stage, grid) ||
	    (stage != Stage::refiner && (manifest.at("H") != grid.height || manifest.at("W") != grid.width)))
		throw std::runtime_error("CK2 graph identity differs from requested profile");
	auto check = [&](const char *name, const Tensors &expected) {
		const auto &specs = manifest.at(name);
		if (!specs.is_array() || specs.size() != expected.size())
			throw std::runtime_error("CK2 manifest I/O count mismatch");
		std::map<std::string, bool> seen;
		for (const auto &spec : specs) {
			const auto tensor = spec.at("name").get<std::string>();
			if (!expected.contains(tensor) || seen[tensor] || spec.at("dtype") != "float32" ||
			    spec.at("shape") != expected.at(tensor).shape)
				throw std::runtime_error("CK2 manifest I/O differs from supported export");
			seen[tensor] = true;
		}
	};
	check("inputs", input_contract(stage, grid));
	check("outputs", output_contract(stage, grid));
}
OrtGraph::OrtGraph(std::unique_ptr<ort::Session> session, Stage stage, Extent grid, bool device,
		   std::function<std::unique_ptr<ort::Session>()> recreate,
		   std::function<std::unique_ptr<ort::Session>(const std::string &, std::stop_token)> fallback)
	: OrtGraph(std::move(session), input_contract(stage, grid), output_contract(stage, grid), device,
		   std::move(recreate), std::move(fallback))
{
}
OrtGraph::OrtGraph(std::unique_ptr<ort::Session> session, Tensors inputs, Tensors outputs, bool device,
		   std::function<std::unique_ptr<ort::Session>()> recreate,
		   std::function<std::unique_ptr<ort::Session>(const std::string &, std::stop_token)> fallback)
	: session_(std::move(session)),
	  recreate_(std::move(recreate)),
	  fallback_(std::move(fallback)),
	  device_(device),
	  inputs_(std::move(inputs)),
	  outputs_(std::move(outputs))
{
	if (!session_)
		throw std::invalid_argument("Missing ORT session");
	verify_specs(session_->inputs(), inputs_);
	verify_specs(session_->outputs(), outputs_);
	bindings_ = std::make_unique<PersistentBindings>(*session_, device_);
}
Tensors OrtGraph::run(const Inputs &inputs, std::stop_token stop)
{
	if (!session_ || !bindings_)
		throw std::runtime_error("Graph session recovery failed; prepare the pipeline again");
	if (inputs.size() != inputs_.size())
		throw std::invalid_argument("Missing graph input");
	for (const auto &[name, expected] : inputs_) {
		if (!inputs.contains(name) || !inputs.at(name))
			throw std::invalid_argument("Missing graph tensor");
		check_tensor(*inputs.at(name), expected);
	}
	auto execute = [&] {
		for (const auto &[name, tensor] : inputs)
			bindings_->set(name, *tensor);
		bindings_->run(stop);
		Tensors outputs;
		for (const auto &[name, spec] : outputs_) {
			if (resident_)
				outputs.emplace(name, bindings_->device_output(name));
			else {
				auto tensor = spec;
				bindings_->read(name, tensor.values);
				outputs.emplace(name, std::move(tensor));
			}
		}
		return outputs;
	};
	try {
		if (recapture_) {
			bindings_.reset();
			session_->close();
			session_.reset();
			session_ = recreate_();
			bindings_ = std::make_unique<PersistentBindings>(*session_, device_);
			recapture_ = false;
		}
		return execute();
	} catch (const std::exception &e) {
		if (!fallback_ || stop.stop_requested())
			throw;
		bindings_.reset();
		if (session_)
			session_->close();
		session_.reset();
		auto fallback = std::move(fallback_);
		recreate_ = {};
		session_ = fallback(e.what(), stop);
		bindings_ = std::make_unique<PersistentBindings>(*session_, device_);
		recapture_ = false;
		return execute();
	}
}
void OrtGraph::reset_transform()
{
	// Capture contains fixed tensor addresses and static graph shapes, not crop coordinates.
	// Overwriting inputs for a different sampled ROI does not require session recreation.
}
std::map<std::string, std::size_t> OrtGraph::finish_profile()
{
	return session_->finish_profile();
}
Ck2Prepared prepare_ort(const PrepareRequest &request, const RuntimeConfig &runtime)
{
	if (!request.model || !request.model->lease)
		throw std::runtime_error("No verified, leased CK2 model is ready");
	const auto analysis_grid = request.settings->quality.grid;
	const bool quality = request.settings->quality.profile == Profile::quality;
	RecordedProbe probe(runtime);
	Ck2Prepared result;
	result.model = request.model;
	std::filesystem::path selected_core;
	std::ostringstream description;
	// Stage graphs decide whether the pipeline can run before loading foundations.
	for (auto stage : {Stage::stage1, Stage::stage2, Stage::rvm, Stage::moge, Stage::refiner}) {
		if (stage == Stage::stage2 && !quality)
			continue;
		if (stage == Stage::refiner && !request.settings->quality.refiner_enabled)
			continue;
		const auto grid = stage == Stage::refiner ? Extent{request.settings->quality.refiner_max_tiles, 52}
							  : analysis_grid;
		const auto name = graph_name(stage, grid);
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
			throw std::runtime_error("CK2 model is missing required graph " + name);
		auto metadata = artifact->graph_path;
		metadata.replace_extension(".json");
		verify_manifest(metadata, stage, grid);
		const bool foundation = stage == Stage::rvm || stage == Stage::moge;
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
			config.precision = foundation || backend != Backend::tensorrt ? Precision::fp32
										      : Precision::fp16;
			config.obs_adapter = request.transport.adapter;
			config.cache_root = runtime.cache;
			config.cache.manifest_hash = ort::file_sha256(metadata);
			config.cache.graph_hash = artifact->graph_sha256;
			config.cache.weights_hash = artifact->weights_sha256;
			config.cache.recipe_version = artifact->recipe_version;
			for (const auto &[tensor_name, tensor] : input_contract(stage, grid)) {
				(void)tensor_name;
				config.cache.shapes.insert(config.cache.shapes.end(), tensor.shape.begin(),
							   tensor.shape.end());
			}
			// CPU-bound DirectML inputs must be rebound after host writes; ORT may cache the upload.
			config.persistent_io = backend == Backend::tensorrt || backend == Backend::cuda;
			if (!runtime.profiles.empty())
				config.profile_prefix = runtime.profiles / name;
			if (backend == Backend::tensorrt && stage != Stage::moge && runtime.capture) {
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
			std::string detail = "CK2 " + name + " unavailable; passthrough. ";
			for (const auto &attempt : prepared.attempts)
				detail += ort::backend_name(attempt.backend) + ": " + attempt.detail + "; ";
			throw std::runtime_error(detail);
		}
		for (const auto &config : alternatives)
			if (config.bundle.backend == prepared.provider.backend) {
				selected_core = std::filesystem::weakly_canonical(config.bundle.runtime);
				break;
			}
		if (stage == Stage::stage1)
			result.backend = prepared.provider.backend;
		description << name << ": " << ort::backend_name(prepared.provider.backend)
			    << (foundation || prepared.provider.backend != Backend::tensorrt ? " FP32; " : " FP16; ");
		const auto selected = alternatives.at(prepared.attempts.size() - 1);
		const bool captured = selected.options.contains("trt_cuda_graph_enable") &&
				      selected.options.at("trt_cuda_graph_enable") == "1";
		const bool device = prepared.provider.backend == Backend::tensorrt ||
				    prepared.provider.backend == Backend::cuda;
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
		auto graph = std::make_unique<OrtGraph>(std::move(prepared.session), stage, grid, device,
							std::move(recreate), std::move(fallback));
		graph->device_resident(runtime.interop && device &&
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
			// Helper capture belongs to another process. Warm these fixed bindings before
			// live graphics transfers can overlap the first TensorRT capture in OBS.
			auto zeros = input_contract(stage, grid);
			Inputs warm;
			for (const auto &[input_name, tensor] : zeros)
				warm.emplace(input_name, &tensor);
			graph->run(warm, request.cancellation);
			graph->run(warm, request.cancellation);
		}
		if (runtime.info)
			runtime.info(
				name + " ready in " +
				std::to_string(
					std::chrono::duration<double, std::milli>(Clock::now() - preparing_at).count()) +
				" ms; capture=" + (captured ? "on" : "off"));
		result.graphs.emplace(stage, std::move(graph));
	}
	result.detail = description.str() + (request.settings->quality.refiner_enabled
						     ? "Native edge refinement."
						     : "Native reconstruction; edge refinement off.");
	return result;
}
#ifdef CORRIDORKEY_CK2_DEV
DevelopmentConfig read_development_config(const std::filesystem::path &path)
{
	std::ifstream input(path);
	const auto j = Json::parse(input);
	DevelopmentConfig result;
	result.models = utf8_path(j.at("models").get<std::string>());
	result.runtime.capture = j.value("capture", true);
	result.runtime.interop = j.value("interop", true);
	if (j.contains("release_fixture"))
		result.release_fixture = utf8_path(j.at("release_fixture").get<std::string>());
	result.runtime.cache = utf8_path(j.at("cache").get<std::string>());
	result.runtime.probe.helper = result.runtime.compiler.helper = utf8_path(j.at("helper").get<std::string>());
	result.runtime.probe.scratch = result.runtime.compiler.scratch = utf8_path(j.at("scratch").get<std::string>());
	result.runtime.probe.fixture_fp32 = utf8_path(j.at("fixture_fp32").get<std::string>());
	result.runtime.probe.fixture_fp16 = utf8_path(j.at("fixture_fp16").get<std::string>());
	result.runtime.compiler.timeout = std::chrono::milliseconds(j.value("build_timeout_ms", 120000));
	if (j.contains("profiles"))
		result.runtime.profiles = utf8_path(j.at("profiles").get<std::string>());
	for (const auto &[backend, descriptor] : j.at("bundles").items())
		result.runtime.probe.bundles.emplace(ort::parse_backend(backend),
						     utf8_path(descriptor.get<std::string>()));
	result.adapter.stable_id = j.at("adapter").get<std::string>();
	return result;
}
std::shared_ptr<const ReadyModel> development_model(const std::filesystem::path &directory, Extent grid, bool quality,
						    const std::filesystem::path &release_fixture,
						    unsigned refiner_budget)
{
	struct LocalLease final : ModelReadLease {};
	auto model = std::make_shared<ReadyModel>();
	model->key = {"ck2-local-validation", "unpublished"};
	model->lease = std::make_shared<LocalLease>();
	Json release;
	if (!release_fixture.empty()) {
		std::ifstream file(release_fixture);
		release = Json::parse(file);
		model->key.revision = release.at("revision").get<std::string>();
	}
	std::string identity;
	for (auto stage : {Stage::moge, Stage::rvm, Stage::stage1, Stage::stage2, Stage::refiner}) {
		if (stage == Stage::stage2 && !quality)
			continue;
		if (stage == Stage::refiner && !refiner_budget)
			continue;
		const auto name = graph_name(stage, stage == Stage::refiner ? Extent{refiner_budget, 52} : grid);
		GraphArtifact artifact;
		artifact.role = name;
		artifact.graph_path = directory / (name + ".onnx");
		artifact.weights_path = directory / (name + ".weights");
		artifact.graph_sha256 = ort::file_sha256(artifact.graph_path);
		artifact.weights_sha256 = ort::file_sha256(artifact.weights_path);
		if (!release.is_null() &&
		    (release.at("sha256").at(name + ".onnx") != artifact.graph_sha256 ||
		     release.at("sha256").at(name + ".json") != ort::file_sha256(directory / (name + ".json"))))
			throw std::runtime_error("Local graph or manifest differs from the pinned release fixture");
		artifact.recipe_version = "export-json-local-validation";
		identity += ort::file_sha256(directory / (name + ".json"));
		model->graphs.push_back(std::move(artifact));
	}
	model->manifest_sha256 = ort::sha256(identity);
	return model;
}
#endif
} // namespace corridorkey::ck2
