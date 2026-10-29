// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/ort_session.hpp"
#include "process.hpp"
#include "telemetry/diagnostics.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <algorithm>

namespace corridorkey::ort {
GraphAttempt compile_in_helper(const SessionConfig &config, const CompilerConfig &compiler, std::stop_token stop)
{
	GraphAttempt result{};
	result.backend = config.bundle.backend;
	result.precision = config.precision;
	try {
		if (stop.stop_requested()) {
			result.failure = error(ErrorCode::cancelled);
			return result;
		}
		if (config.bundle.descriptor.empty())
			throw std::runtime_error("Compilation needs an installed bundle descriptor");
		fs::create_directories(compiler.scratch);
		std::random_device random;
		fs::path dir;
		for (int attempt = 0; attempt < 8; ++attempt) {
			dir = fs::absolute(compiler.scratch) /
			      ("compile-" + std::to_string(random()) + "-" + std::to_string(random()));
			if (fs::create_directory(dir))
				break;
			dir.clear();
		}
		if (dir.empty())
			throw std::runtime_error("Cannot allocate compilation workspace");
		struct Cleanup {
			fs::path p;
			~Cleanup()
			{
				std::error_code ec;
				fs::remove_all(p, ec);
			}
		} cleanup{dir};
		auto utf8 = [](const fs::path &path) {
			auto s = path.u8string();
			return std::string(s.begin(), s.end());
		};
		nlohmann::json request{
			{"schema", 1},
			{"mode", "build"},
			{"bundle", utf8(config.bundle.descriptor)},
			{"graph", utf8(fs::absolute(config.graph))},
			{"adapter", config.obs_adapter.stable_id},
			{"require_adapter_match", config.require_adapter_match},
			{"prebuilt_context", config.prebuilt_context},
			{"persistent_io", config.persistent_io},
			{"rebuild_cache", config.rebuild_cache},
			{"precision", static_cast<int>(config.precision == Precision::automatic ? Precision::fp32
												: config.precision)},
			{"options", config.options},
			{"session_options", config.session_options},
			{"cache", config.cache_root.empty() ? "" : utf8(fs::absolute(config.cache_root))},
			{"manifest_hash", config.cache.manifest_hash},
			{"weights_hash", config.cache.weights_hash},
			{"recipe_version", config.cache.recipe_version},
			{"graph_hash", config.cache.graph_hash},
			{"shapes", config.cache.shapes},
			{"tile_batch", config.cache.tile_batch}};
		const auto req = dir / "request.json", resp = dir / "response.json";
		{
			std::ofstream out(req);
			out << request.dump();
			if (!out)
				throw std::runtime_error("Cannot write compilation request");
		}
		const auto process = run_helper(fs::absolute(compiler.helper), req, resp, compiler.timeout, stop);
		if (process.cancelled) {
			result.failure = error(ErrorCode::cancelled);
			result.detail = "Graph compilation cancelled";
			return result;
		}
		if (process.timed_out) {
			result.failure = error(ErrorCode::timeout);
			result.detail = "Graph compiler exceeded its deadline";
			return result;
		}
		if (!process.started || process.crashed || !fs::exists(resp) || fs::file_size(resp) > 1024 * 1024)
			throw std::runtime_error("Graph compiler crashed or returned no valid result");
		std::ifstream in(resp);
		const auto j = nlohmann::json::parse(in);
		if (j.at("schema") != 1 || j.at("backend") != backend_name(config.bundle.backend))
			throw std::runtime_error("Graph compiler returned a mismatched response");
		if (process.exit_code != 0 || !j.at("ok").get<bool>())
			result.failure = error(ErrorCode::backend_unavailable);
		else if (j.at("mode") != "build")
			throw std::runtime_error("Graph compiler returned a mismatched mode");
		result.detail = j.value("detail", "");
	} catch (...) {
		result.failure = error(ErrorCode::backend_unavailable);
		result.detail = "Graph compilation unavailable; verify helper, bundle and static graph";
	}
	return result;
}
PreparedGraph prepare_graph(std::span<const SessionConfig> alternatives, ProviderProbe &probe,
			    const CompilerConfig &compiler, std::stop_token stop)
{
	PreparedGraph result;
	struct Finish {
		const CompilerConfig &compiler;
		~Finish()
		{
			if (compiler.finished)
				compiler.finished();
		}
	} finish{compiler};
	for (const auto &config : alternatives) {
		if (stop.stop_requested())
			break;
		GraphAttempt attempt{};
		attempt.backend = config.bundle.backend;
		attempt.precision = config.precision;
		if (config.diagnostic_only || config.precision == Precision::bf16 ||
		    config.bundle.backend == Backend::cpu_diagnostic) {
			attempt.failure = error(ErrorCode::unsupported);
			attempt.detail = "Diagnostic-only backend or precision excluded from live graph fallback";
		} else {
			ProbeRequest request;
			request.backend = config.bundle.backend;
			request.obs_adapter = config.obs_adapter;
			request.precision = config.precision;
			request.package_id = config.bundle.id;
			// Large runtime bundles must finish hash verification before the fixture runs.
			request.timeout = compiler.timeout;
			if (compiler.state)
				compiler.state(config.bundle.backend, ModelState::warming);
			auto tested = probe.probe(request, stop);
			if (tested.failure || !tested.provider.available || !tested.provider.tested ||
			    !tested.provider.matches_obs_adapter || !tested.provider.permits_live_inference) {
				attempt.failure = tested.failure ? tested.failure
								 : error(ErrorCode::backend_unavailable);
				attempt.detail = tested.diagnostic_detail;
			} else {
				if (compiler.state)
					compiler.state(config.bundle.backend,
						       config.bundle.backend == Backend::tensorrt ||
								       config.bundle.backend == Backend::tensorrt_rtx
							       ? ModelState::building
							       : ModelState::warming);
				attempt = compile_in_helper(config, compiler, stop);
				if (attempt.failure && !stop.stop_requested() && !config.cache_root.empty() &&
				    (config.bundle.backend == Backend::tensorrt ||
				     config.bundle.backend == Backend::tensorrt_rtx)) {
					auto fresh = config;
					fresh.rebuild_cache = true;
					const auto first = attempt.detail;
					attempt = compile_in_helper(fresh, compiler, stop);
					attempt.detail =
						"Cache rebuild attempted once after: " + first + "; " + attempt.detail;
				}
				if (!attempt.failure && !stop.stop_requested()) {
					try {
						if (compiler.state)
							compiler.state(config.bundle.backend, ModelState::warming);
						result.session = std::make_unique<Session>(config);
						result.provider = tested.provider;
					} catch (const std::bad_alloc &) {
						attempt.failure = error(ErrorCode::out_of_memory);
					} catch (const std::exception &e) {
						attempt.failure = error(ErrorCode::backend_unavailable);
						attempt.detail =
							"In-process preparation failed after graph compilation: " +
							diagnostics::redact(e.what());
					}
				}
			}
		}
		result.attempts.push_back(std::move(attempt));
		if (result.session)
			break;
	}
	return result;
}
} // namespace corridorkey::ort
