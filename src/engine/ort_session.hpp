// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "providers/loader.hpp"
#include "contracts/models.hpp"
#include <functional>
#include <mutex>

struct IDMLDevice;
struct ID3D12CommandQueue;
namespace corridorkey::ort {
struct TensorSpec {
	std::string name;
	std::vector<std::int64_t> shape;
	ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
};
struct SessionConfig {
	std::shared_ptr<const ReadyModel> model_lease;
	Bundle bundle;
	fs::path graph, cache_root, profile_prefix;
	AdapterIdentity obs_adapter;
	Precision precision = Precision::fp32;
	CacheIdentity cache;
	std::map<std::string, std::string> options;
	std::map<std::string, std::string> session_options;
	bool require_adapter_match = true;
	bool diagnostic_only = false;  // BF16 fixture exploration, never live preparation.
	bool prebuilt_context = false; // EPContext graph + engine hashes supplied by the binder.
	// W11 may supply its matched DML device/queue; both must be provided together.
	IDMLDevice *dml_device = nullptr;
	ID3D12CommandQueue *dml_queue = nullptr;
};
class Session {
public:
	explicit Session(SessionConfig);
	~Session();
	Session(const Session &) = delete;
	Session &operator=(const Session &) = delete;
	const std::vector<TensorSpec> &inputs() const;
	const std::vector<TensorSpec> &outputs() const;
	const Device &device() const;
	const Runtime &runtime() const;
	// Caller retains values, buffers and device allocations until run_bound returns.
	// Calls serialize per session (required by DirectML). No render-thread calls.
	void run_bound(std::span<const std::pair<std::string, const OrtValue *>> inputs,
		       std::span<const std::pair<std::string, const OrtValue *>> outputs, std::stop_token = {});
	std::map<std::string, std::size_t> finish_profile();
	void finish_build(); // Publish compilation artifacts; does not claim first-run parity.
	void close();        // Worker teardown; reports cache publication failures. Destructor is best effort.
private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
// Production preparation entry point. The raw constructor also serves the helper.
// The caller maps the returned probe capabilities into PrepareRequest::provider.
struct PreparedSession {
	std::unique_ptr<Session> session;
	ProbeResult probe;
};
PreparedSession prepare_session(SessionConfig, ProviderProbe &, std::stop_token = {});
struct CompilerConfig {
	fs::path helper, scratch;
	std::chrono::milliseconds timeout{120000};
};
struct GraphAttempt {
	Backend backend = Backend::automatic;
	Precision precision = Precision::automatic;
	Error failure;
	std::string detail;
};
GraphAttempt compile_in_helper(const SessionConfig &, const CompilerConfig &, std::stop_token = {});
struct PreparedGraph {
	std::unique_ptr<Session> session;
	ProviderCapabilities provider;
	std::vector<GraphAttempt> attempts;
};
// Ordered alternatives belong to one graph; stages may use different EPs and precision.
PreparedGraph prepare_graph(std::span<const SessionConfig>, ProviderProbe &, const CompilerConfig &,
			    std::stop_token = {});
} // namespace corridorkey::ort
