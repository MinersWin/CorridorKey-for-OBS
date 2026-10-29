// SPDX-License-Identifier: GPL-3.0-or-later
#include "ort_session.hpp"
#include "util/utf8_path.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#ifdef _WIN32
#define NOMINMAX
#include <d3d12.h>
#include <wrl/client.h>
#endif

namespace corridorkey::ort {
namespace {
template<class T> using OrtOwned = std::unique_ptr<T, std::function<void(T *)>>;
struct OptionArrays {
	std::vector<const char *> keys, values;
	explicit OptionArrays(const std::map<std::string, std::string> &options)
	{
		for (const auto &[k, v] : options) {
			keys.push_back(k.c_str());
			values.push_back(v.c_str());
		}
	}
};
AdapterIdentity ep_adapter(const OrtApi &api, const OrtEpDevice *ep)
{
	const auto *device = api.EpDevice_Device(ep);
	AdapterIdentity identity;
	identity.vendor_id = api.HardwareDevice_VendorId(device);
	identity.device_id = api.HardwareDevice_DeviceId(device);
	const auto *metadata = api.HardwareDevice_Metadata(device);
	if (!metadata)
		return identity;
	if (const auto *luid = api.GetKeyValue(metadata, "LUID")) {
		try {
			const auto value = std::stoull(luid);
			std::ostringstream text;
			text << "luid:" << std::hex << std::setfill('0') << std::setw(8) << (value >> 32) << ':'
			     << std::setw(8) << (value & 0xffffffff);
			identity.stable_id = text.str();
		} catch (...) {
			throw std::runtime_error("EP returned an invalid adapter LUID");
		}
	} else if (const auto *pci = api.GetKeyValue(metadata, "pci_bus_id"))
		identity.stable_id = "pci:" + std::string(pci);
	if (const auto *name = api.GetKeyValue(metadata, "name"))
		identity.display_name = name;
	return identity;
}
// Stable prefix of the documented OrtDmlApi, fetched from the loaded core.
struct DmlApiPrefix {
	OrtStatus *(ORT_API_CALL *append)(OrtSessionOptions *, int);
	OrtStatus *(ORT_API_CALL *append_device)(OrtSessionOptions *, IDMLDevice *, ID3D12CommandQueue *);
};
} // namespace
struct Session::Impl {
	std::shared_ptr<const ReadyModel> model_lease;
	std::shared_ptr<Runtime> runtime;
	std::unique_ptr<CacheLease> cache;
	OrtSession *session = nullptr;
	OrtIoBinding *binding = nullptr;
	OrtRunOptions *run_options = nullptr;
	std::vector<TensorSpec> inputs, outputs;
	Device device;
	bool profiling = false, cache_ready = false;
	bool persistent_io = false, capture = false;
	std::map<std::string, std::size_t> profile_result;
	std::vector<std::pair<std::string, const OrtValue *>> bound_inputs, bound_outputs;
	std::mutex mutex;
	void close()
	{
		if (!runtime)
			return;
		const auto &api = runtime->api();
		if (binding) {
			api.ReleaseIoBinding(binding);
			binding = nullptr;
		}
		if (run_options) {
			api.ReleaseRunOptions(run_options);
			run_options = nullptr;
		}
		if (session) {
			api.ReleaseSession(session);
			session = nullptr;
		}
		if (cache && cache_ready) {
			cache_ready = false;
			cache->commit();
		}
		cache.reset();
	}
	~Impl()
	{
		try {
			close();
		} catch (...) {
		}
	}
};
Session::Session(SessionConfig config) : impl_(std::make_unique<Impl>())
{
	if (config.precision == Precision::bf16 && !config.diagnostic_only)
		throw std::runtime_error("BF16 is diagnostic-only; use this graph's validated FP16 or FP32 policy");
	// EP registration and device enumeration share the process-wide environment.
	std::lock_guard loader_guard(runtime_control_mutex());
	auto &state = *impl_;
	state.persistent_io = config.persistent_io;
	state.capture = config.bundle.backend == Backend::tensorrt &&
			config.options.contains("trt_cuda_graph_enable") &&
			config.options.at("trt_cuda_graph_enable") == "1";
	if (state.capture && !state.persistent_io)
		throw std::runtime_error("TensorRT capture requires persistent device I/O");
	state.model_lease = std::move(config.model_lease);
	state.runtime = Runtime::load(config.bundle);
	const auto &runtime = *state.runtime;
	const auto &api = runtime.api();
	auto check = [&](OrtStatus *s) {
		runtime.check(s);
	};
	const bool cpu = config.bundle.backend == Backend::cpu_diagnostic;
	auto devices = enumerate_devices(config.bundle.backend);
	const OrtEpDevice *selected_ep = nullptr;
	if (config.bundle.registration == Registration::plugin) {
		const OrtEpDevice *const *eps = nullptr;
		size_t count = 0;
		check(api.GetEpDevices(runtime.env(), &eps, &count));
		for (size_t i = 0; i < count; ++i) {
			if (config.bundle.provider_name != api.EpDevice_EpName(eps[i]))
				continue;
			auto identity = ep_adapter(api, eps[i]);
			if (!config.obs_adapter.stable_id.empty() && !adapter_matches(identity, config.obs_adapter))
				continue;
			if (identity.stable_id.empty())
				continue;
			state.device.adapter = identity;
			for (const auto &d : devices)
				if (adapter_matches(d.adapter, identity))
					state.device = d;
			const auto *metadata = api.EpDevice_EpMetadata(eps[i]);
			if (state.device.driver.empty() && metadata)
				if (auto driver = api.GetKeyValue(metadata, "os_driver_version"))
					state.device.driver = driver;
			selected_ep = eps[i];
			break;
		}
		if (!selected_ep)
			throw std::runtime_error("EP has no device with a verified matching LUID/PCI identity");
	} else {
		bool found = false;
		for (const auto &d : devices) {
			if (!cpu && !config.obs_adapter.stable_id.empty() &&
			    !adapter_matches(d.adapter, config.obs_adapter))
				continue;
			state.device = d;
			found = true;
			break;
		}
		if (!found)
			throw std::runtime_error("Provider has no matching adapter");
	}
	if (!cpu && config.require_adapter_match && !adapter_matches(state.device.adapter, config.obs_adapter))
		throw std::runtime_error("OBS adapter identity is unknown or does not match the provider");
	if (config.bundle.backend == Backend::tensorrt_rtx && !state.device.architecture.empty() &&
	    std::stoi(state.device.architecture.substr(3)) < 80)
		throw std::runtime_error("TensorRT-RTX requires an Ampere or newer GPU");
	if (config.precision == Precision::automatic)
		config.precision = Precision::fp32;
	auto options = config.bundle.options;
	for (const auto &[k, v] : config.options)
		options[k] = v;
	// Adapter selection owns device_id; a user option cannot redirect the session.
	options.erase("device_id");
	if (config.bundle.registration == Registration::classic && config.bundle.backend != Backend::directml && !cpu)
		options["device_id"] = std::to_string(state.device.ordinal);
	if (config.bundle.backend == Backend::tensorrt && !config.prebuilt_context)
		options["trt_fp16_enable"] = config.precision == Precision::fp16 ? "1" : "0";
	if (config.cache.graph_hash.empty())
		config.cache.graph_hash = file_sha256(config.graph);
	else if (config.cache.graph_hash != file_sha256(config.graph))
		throw std::runtime_error("Graph hash does not match cache identity");
	config.cache.runtime_build = runtime.build() + ":" + file_sha256(config.bundle.runtime);
	config.cache.provider_build =
		config.bundle.provider_build + ":" + sha256(nlohmann::json(config.bundle.file_hashes).dump());
	config.cache.gpu = state.device.adapter.stable_id;
	config.cache.driver = state.device.driver;
	config.cache.architecture = state.device.architecture;
	config.cache.precision = std::to_string(static_cast<int>(config.precision));
	config.cache.options = options;
	config.cache.options["context:prebuilt"] = config.prebuilt_context ? "1" : "0";
	for (const auto &[key, value] : config.session_options)
		config.cache.options["session:" + key] = value;
	if (!config.cache_root.empty()) {
		if (config.cache.weights_hash.empty() || config.cache.manifest_hash.empty() ||
		    config.cache.recipe_version.empty())
			throw std::runtime_error(
				"Persistent cache needs manifest, weights and binding recipe identities");
		if (!cpu && state.device.driver.empty())
			throw std::runtime_error("Driver identity unavailable; persistent backend caches disabled");
		state.cache = std::make_unique<CacheLease>(config.cache_root, config.cache);
		const auto path = state.cache->path().generic_string();
		if (config.bundle.backend == Backend::tensorrt_rtx)
			options["nv_runtime_cache_path"] = path;
		if (config.bundle.backend == Backend::tensorrt) {
			options["trt_engine_cache_enable"] = "1";
			options["trt_engine_cache_path"] = path;
			options["trt_timing_cache_enable"] = "1";
			options["trt_timing_cache_path"] = path;
		}
	}
	OrtSessionOptions *raw_options = nullptr;
	check(api.CreateSessionOptions(&raw_options));
	OrtOwned<OrtSessionOptions> session_options(raw_options, api.ReleaseSessionOptions);
	check(api.SetSessionExecutionMode(raw_options, ORT_SEQUENTIAL));
	check(api.SetIntraOpNumThreads(raw_options, 1));
	check(api.SetInterOpNumThreads(raw_options, 1));
	for (const auto &[key, value] : config.session_options) {
		if (key == "session.disable_cpu_ep_fallback")
			throw std::runtime_error("CPU fallback policy cannot be overridden");
		check(api.AddSessionConfigEntry(raw_options, key.c_str(), value.c_str()));
	}
	if (!cpu)
		check(api.AddSessionConfigEntry(raw_options, "session.disable_cpu_ep_fallback", "1"));
	if (config.bundle.backend == Backend::directml)
		check(api.DisableMemPattern(raw_options));
	if (!config.profile_prefix.empty()) {
		fs::create_directories(config.profile_prefix.parent_path());
		check(api.EnableProfiling(raw_options, config.profile_prefix.c_str()));
		state.profiling = true;
	}
	OptionArrays arrays(options);
	if (selected_ep) {
		check(api.SessionOptionsAppendExecutionProvider_V2(raw_options, runtime.env(), &selected_ep, 1,
								   arrays.keys.data(), arrays.values.data(),
								   arrays.keys.size()));
	} else if (config.bundle.backend == Backend::directml) {
		const void *extension = nullptr;
		check(api.GetExecutionProviderApi("DML", runtime.api_version(), &extension));
		if (!extension)
			throw std::runtime_error("DirectML provider API is absent");
		const auto *dml = static_cast<const DmlApiPrefix *>(extension);
		if (config.dml_device || config.dml_queue) {
			if (!config.dml_device || !config.dml_queue)
				throw std::runtime_error("DirectML device and queue must be supplied together");
#ifdef _WIN32
			Microsoft::WRL::ComPtr<ID3D12Device> queue_device;
			if (FAILED(config.dml_queue->GetDevice(IID_PPV_ARGS(&queue_device))))
				throw std::runtime_error("Cannot inspect the supplied DirectML queue's adapter");
			const auto luid = queue_device->GetAdapterLuid();
			std::ostringstream identity;
			identity << "luid:" << std::hex << std::setfill('0') << std::setw(8)
				 << static_cast<std::uint32_t>(luid.HighPart) << ':' << std::setw(8) << luid.LowPart;
			if (identity.str() != state.device.adapter.stable_id)
				throw std::runtime_error("DirectML queue belongs to a different adapter");
#endif
			check(dml->append_device(raw_options, config.dml_device, config.dml_queue));
		} else
			check(dml->append(raw_options, state.device.ordinal));
	} else if (config.bundle.backend == Backend::cuda || config.bundle.backend == Backend::tensorrt) {
		if (config.bundle.backend == Backend::tensorrt) {
			OrtTensorRTProviderOptionsV2 *trt = nullptr;
			check(api.CreateTensorRTProviderOptions(&trt));
			OrtOwned<OrtTensorRTProviderOptionsV2> guard(trt, api.ReleaseTensorRTProviderOptions);
			check(api.UpdateTensorRTProviderOptions(trt, arrays.keys.data(), arrays.values.data(),
								arrays.keys.size()));
			check(api.SessionOptionsAppendExecutionProvider_TensorRT_V2(raw_options, trt));
		}
		OrtCUDAProviderOptionsV2 *cuda = nullptr;
		check(api.CreateCUDAProviderOptions(&cuda));
		OrtOwned<OrtCUDAProviderOptionsV2> guard(cuda, api.ReleaseCUDAProviderOptions);
		auto cuda_options = config.bundle.backend == Backend::cuda
					    ? options
					    : std::map<std::string, std::string>{
						      {"device_id", std::to_string(state.device.ordinal)}};
		OptionArrays cuda_arrays(cuda_options);
		check(api.UpdateCUDAProviderOptions(cuda, cuda_arrays.keys.data(), cuda_arrays.values.data(),
						    cuda_arrays.keys.size()));
		check(api.SessionOptionsAppendExecutionProvider_CUDA_V2(raw_options, cuda));
	} else if (config.bundle.backend == Backend::migraphx) {
		throw std::runtime_error("MIGraphX requires a matched plugin-EP bundle");
	} else if (!cpu)
		throw std::runtime_error("Unsupported provider registration");
	check(api.CreateSession(runtime.env(), config.graph.c_str(), raw_options, &state.session));
	OrtAllocator *allocator = nullptr;
	check(api.GetAllocatorWithDefaultOptions(&allocator));
	auto inspect = [&](bool input) {
		std::vector<TensorSpec> specs;
		size_t count = 0;
		check(input ? api.SessionGetInputCount(state.session, &count)
			    : api.SessionGetOutputCount(state.session, &count));
		for (size_t i = 0; i < count; ++i) {
			char *name = nullptr;
			OrtTypeInfo *raw_type = nullptr;
			check(input ? api.SessionGetInputName(state.session, i, allocator, &name)
				    : api.SessionGetOutputName(state.session, i, allocator, &name));
			OrtOwned<char> name_guard(name, [&](char *p) { allocator->Free(allocator, p); });
			check(input ? api.SessionGetInputTypeInfo(state.session, i, &raw_type)
				    : api.SessionGetOutputTypeInfo(state.session, i, &raw_type));
			OrtOwned<OrtTypeInfo> type_guard(raw_type, api.ReleaseTypeInfo);
			const OrtTensorTypeAndShapeInfo *tensor = nullptr;
			check(api.CastTypeInfoToTensorInfo(raw_type, &tensor));
			if (!tensor)
				throw std::runtime_error("Only static tensor graphs are supported");
			TensorSpec spec;
			spec.name = name;
			size_t rank = 0;
			check(api.GetDimensionsCount(tensor, &rank));
			spec.shape.resize(rank);
			check(api.GetDimensions(tensor, spec.shape.data(), rank));
			check(api.GetTensorElementType(tensor, &spec.type));
			for (auto dim : spec.shape)
				if (dim <= 0)
					throw std::runtime_error("Graph has dynamic or empty tensor dimensions");
			specs.push_back(std::move(spec));
		}
		return specs;
	};
	state.inputs = inspect(true);
	state.outputs = inspect(false);
	check(api.CreateIoBinding(state.session, &state.binding));
	check(api.CreateRunOptions(&state.run_options));
}
Session::~Session() = default;
PreparedSession prepare_session(SessionConfig config, ProviderProbe &probe, std::stop_token stop)
{
	if (config.diagnostic_only || config.precision == Precision::bf16)
		throw std::runtime_error("Diagnostic precision is not a live pipeline option");
	ProbeRequest request;
	request.backend = config.bundle.backend;
	request.obs_adapter = config.obs_adapter;
	request.precision = config.precision;
	request.package_id = config.bundle.id;
	PreparedSession prepared;
	prepared.probe = probe.probe(request, stop);
	const auto &capability = prepared.probe.provider;
	const bool cpu = request.backend == Backend::cpu_diagnostic;
	if (prepared.probe.failure || !capability.available || !capability.tested ||
	    (!cpu && (!capability.permits_live_inference || !capability.matches_obs_adapter))) {
		if (!prepared.probe.failure) {
			prepared.probe.failure = error(ErrorCode::backend_unavailable);
			prepared.probe.diagnostic_detail =
				"Provider preflight did not establish live inference on the OBS adapter";
		}
		return prepared;
	}
	if (stop.stop_requested()) {
		prepared.probe.failure = error(ErrorCode::cancelled);
		return prepared;
	}
	prepared.session = std::make_unique<Session>(std::move(config));
	return prepared;
}
const std::vector<TensorSpec> &Session::inputs() const
{
	return impl_->inputs;
}
const std::vector<TensorSpec> &Session::outputs() const
{
	return impl_->outputs;
}
const Device &Session::device() const
{
	return impl_->device;
}
const Runtime &Session::runtime() const
{
	return *impl_->runtime;
}
void Session::finish_build()
{
	std::lock_guard lock(impl_->mutex);
	impl_->cache_ready = true;
}
void Session::close()
{
	std::lock_guard lock(impl_->mutex);
	impl_->close();
}
void Session::run_bound(std::span<const std::pair<std::string, const OrtValue *>> inputs,
			std::span<const std::pair<std::string, const OrtValue *>> outputs, std::stop_token stop)
{
	std::lock_guard lock(impl_->mutex);
	if (!impl_->session)
		throw std::runtime_error("Session is closed");
	const auto &rt = runtime();
	const auto &api = rt.api();
	if (stop.stop_requested())
		throw std::runtime_error("Inference cancelled");
	if (inputs.size() != impl_->inputs.size() || outputs.size() != impl_->outputs.size())
		throw std::runtime_error("All static graph tensors must be bound");
	if (impl_->capture) {
		if (!impl_->bound_inputs.empty() &&
		    (!std::equal(inputs.begin(), inputs.end(), impl_->bound_inputs.begin(),
				 impl_->bound_inputs.end()) ||
		     !std::equal(outputs.begin(), outputs.end(), impl_->bound_outputs.begin(),
				 impl_->bound_outputs.end())))
			throw std::runtime_error("Captured I/O changed; recreate the session before rebinding");
		if (impl_->bound_inputs.empty()) {
			for (auto values : {inputs, outputs})
				for (const auto &[name, value] : values) {
					(void)name;
					const OrtMemoryInfo *memory = nullptr;
					const char *domain = nullptr;
					int ordinal = -1;
					rt.check(api.GetTensorMemoryInfo(value, &memory));
					rt.check(api.MemoryInfoGetName(memory, &domain));
					rt.check(api.MemoryInfoGetId(memory, &ordinal));
					if (!domain || std::string_view(domain) != "Cuda" ||
					    ordinal != impl_->device.ordinal)
						throw std::runtime_error(
							"Captured I/O must reside on the selected CUDA device");
				}
		}
	}
	rt.check(api.RunOptionsUnsetTerminate(impl_->run_options));
	std::stop_callback cancel(stop, [&] {
		if (auto status = api.RunOptionsSetTerminate(impl_->run_options))
			api.ReleaseStatus(status);
	});
	if (!impl_->persistent_io ||
	    !std::equal(inputs.begin(), inputs.end(), impl_->bound_inputs.begin(), impl_->bound_inputs.end()) ||
	    !std::equal(outputs.begin(), outputs.end(), impl_->bound_outputs.begin(), impl_->bound_outputs.end())) {
		api.ClearBoundInputs(impl_->binding);
		api.ClearBoundOutputs(impl_->binding);
		impl_->bound_inputs.clear();
		impl_->bound_outputs.clear();
		for (const auto &[name, value] : inputs)
			rt.check(api.BindInput(impl_->binding, name.c_str(), value));
		for (const auto &[name, value] : outputs)
			rt.check(api.BindOutput(impl_->binding, name.c_str(), value));
		impl_->bound_inputs.assign(inputs.begin(), inputs.end());
		impl_->bound_outputs.assign(outputs.begin(), outputs.end());
	}
	rt.check(api.SynchronizeBoundInputs(impl_->binding));
	rt.check(api.RunWithBinding(impl_->session, impl_->run_options, impl_->binding));
	rt.check(api.SynchronizeBoundOutputs(impl_->binding));
	impl_->cache_ready = true;
}
std::map<std::string, std::size_t> Session::finish_profile()
{
	std::lock_guard lock(impl_->mutex);
	return finish_profile_unlocked();
}
std::map<std::string, std::size_t> Session::finish_profile_unlocked()
{
	std::map<std::string, size_t> partitions;
	if (!impl_->profiling || !impl_->session)
		return impl_->profile_result;
	const auto &rt = runtime();
	const auto &api = rt.api();
	OrtAllocator *allocator = nullptr;
	rt.check(api.GetAllocatorWithDefaultOptions(&allocator));
	char *path = nullptr;
	rt.check(api.SessionEndProfiling(impl_->session, allocator, &path));
	OrtOwned<char> guard(path, [&](char *p) { allocator->Free(allocator, p); });
	impl_->profiling = false;
	std::ifstream in(corridorkey::utf8_path(path));
	const auto events = nlohmann::json::parse(in);
	for (const auto &event : events) {
		if (event.value("cat", "") != "Node" || !event.contains("args"))
			continue;
		const auto provider = event["args"].value("provider", "");
		if (!provider.empty())
			++partitions[provider];
	}
	impl_->profile_result = partitions;
	return partitions;
}
} // namespace corridorkey::ort
