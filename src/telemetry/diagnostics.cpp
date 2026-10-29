// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagnostics.hpp"
#include "settings/schema.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <regex>

namespace corridorkey::diagnostics {
namespace {
using Json = nlohmann::json;
std::mutex mutex;
std::map<const void *, std::function<Instance()>> readers;
std::map<Backend, ProbeResult> probes;
std::array<std::atomic<ErrorCode>, 16> errors{};
std::atomic<unsigned> error_next{0};
std::atomic<ErrorCode> last_error{ErrorCode::none};
std::string text(std::string_view input)
{
	auto value = redact(input);
	for (char &c : value)
		if (static_cast<unsigned char>(c) < 32 || c == 127)
			c = ' ';
	return value.substr(0, 256);
}
std::string identity(std::string_view value)
{
	// Identities cannot carry arbitrary paths, URLs, whitespace or credentials.
	if (value.size() > 128 || value.find_first_of("/\\: \t\r\n@?=") != std::string_view::npos)
		return "<redacted>";
	return text(value);
}
std::string hash(std::string_view value)
{
	return models::hex_string(value, 64) ? std::string(value) : "unavailable";
}
Json number(std::optional<double> value)
{
	return value && std::isfinite(*value) && *value >= 0 ? Json(*value) : Json(nullptr);
}
std::string_view backend_name(Backend value)
{
	constexpr std::string_view names[] = {"auto",     "tensorrt-rtx", "tensorrt",       "cuda",
					      "directml", "migraphx",     "cpu diagnostic", "fake (synthetic)"};
	const auto index = static_cast<unsigned>(value);
	return index < std::size(names) ? names[index] : "unknown";
}
Json settings_report(const Settings &s)
{
	Json out{{"settings_version", settings::schema_version}};
	for (const auto &f : settings::inventory()) {
		const auto v = f.get(s);
		if (const auto *b = std::get_if<bool>(&v))
			out[f.key] = *b;
		else if (const auto *i = std::get_if<std::int64_t>(&v))
			out[f.key] = *i;
		else if (const auto *d = std::get_if<double>(&v))
			out[f.key] = std::isfinite(*d) ? Json(*d) : Json(nullptr);
		else if (f.kind == settings::Kind::choice) {
			const auto &choice = std::get<std::string>(v);
			if (std::any_of(f.choices.begin(), f.choices.end(),
					[&](const auto &c) { return choice == c.value; }))
				out[f.key] = choice;
		}
	}
	return out;
}
} // namespace
void register_instance(const void *key, std::function<Instance()> read)
{
	std::lock_guard lock(mutex);
	readers[key] = std::move(read);
}
void unregister_instance(const void *key)
{
	std::lock_guard lock(mutex);
	readers.erase(key);
}
std::vector<Instance> instances()
{
	std::lock_guard lock(mutex);
	std::vector<Instance> out;
	for (const auto &[key, read] : readers) {
		(void)key;
		out.push_back(read());
	}
	return out;
}
void record_error(ErrorCode code) noexcept
{
	if (code == ErrorCode::none || last_error.exchange(code, std::memory_order_relaxed) == code)
		return;
	errors[error_next.fetch_add(1, std::memory_order_relaxed) % errors.size()].store(code,
											 std::memory_order_relaxed);
}
void record_probe(const ProbeResult &result)
{
	std::lock_guard lock(mutex);
	auto safe = result;
	safe.diagnostic_detail.clear();
	safe.failure.action = {};
	safe.provider.unavailable_reason.action = {};
	probes[result.provider.backend] = std::move(safe);
	record_error(result.failure.code);
}
std::optional<ProbeResult> latest_probe(Backend backend)
{
	std::lock_guard lock(mutex);
	const auto it = probes.find(backend);
	return it == probes.end() ? std::nullopt : std::optional(it->second);
}
std::string redact(std::string_view input)
{
	std::string value(input);
	for (const auto *key : {"USERPROFILE", "HOME", "APPDATA", "LOCALAPPDATA"}) {
		const auto *home = std::getenv(key);
		if (!home || !*home)
			continue;
		std::string path(home);
		for (int slash = 0; slash < 2; ++slash) {
			std::size_t at = 0;
			while ((at = value.find(path, at)) != std::string::npos) {
				value.replace(at, path.size(), "<profile>");
				at += 9;
			}
			std::replace(path.begin(), path.end(), '\\', '/');
		}
	}
	value = std::regex_replace(
		value, std::regex(R"(([A-Za-z]:[\\/]Users[\\/]|/home/|/Users/)[^\\/\r\n]+)", std::regex::icase),
		"<profile>");
	const std::regex secret(
		R"((hf_|gh[pousr]_|github_pat_|sk-)[A-Za-z0-9_-]+|Bearer\s+\S+|(?:token|password|secret|authorization|api[_-]?key)\s*[:=]\s*(?:Bearer\s+)?\S+)",
		std::regex::icase);
	return std::regex_replace(value, secret, "<secret>");
}
std::string report(const ui::ManagerSnapshot &s, const Environment &e)
{
	Json j{{"schema", 1},
	       {"plugin", text(e.plugin)},
	       {"commit", identity(e.commit)},
	       {"obs", text(e.obs)},
	       {"renderer", text(e.renderer)},
	       {"os", text(e.os)},
	       {"kernel", text(e.kernel)},
	       {"architecture", text(e.architecture)},
	       {"qt", text(e.qt)},
	       {"canvas", {e.canvas.width, e.canvas.height}},
	       {"fps", number(e.fps)},
	       {"active_fps_api", e.active_fps_api},
	       {"streaming", e.streaming},
	       {"recording", e.recording},
	       {"cache", {{"path", "<model-cache>"}, {"measured", s.disk_measured}, {"bytes", s.total_bytes}}},
	       {"gpus", Json::array()},
	       {"providers", Json::array()},
	       {"models", Json::array()},
	       {"instances", Json::array()},
	       {"recent_errors", Json::array()}};
	for (const auto &d : e.devices) {
		const auto vram = e.vram_bytes.find(d.adapter.stable_id);
		j["gpus"].push_back({{"name", text(d.adapter.display_name)},
				     {"adapter", text(d.adapter.stable_id)},
				     {"driver", text(d.driver)},
				     {"architecture", text(d.architecture)},
				     {"vram_bytes", vram == e.vram_bytes.end() ? Json(nullptr) : Json(vram->second)},
				     {"vendor_id", d.adapter.vendor_id},
				     {"device_id", d.adapter.device_id}});
	}
	for (const auto &p : e.providers) {
		Json item{{"backend", backend_name(p.backend)},
			  {"bundle", nullptr},
			  {"ort_core", nullptr},
			  {"ep_build", nullptr},
			  {"ort_api", nullptr},
			  {"configured", p.bundle.has_value()},
			  {"tested", p.probe && p.probe->provider.tested},
			  {"available", false},
			  {"reason", p.bundle ? "not probed in this session" : "runtime bundle not installed"}};
		item["selected"] = std::any_of(e.instances.begin(), e.instances.end(), [&](const auto &i) {
			return i.status && i.status->backend == p.backend && i.status->lifecycle == Lifecycle::ready;
		});
		if (p.bundle) {
			item["bundle_integrity"] = "descriptor identity; diagnostics does not rehash libraries";
			item["bundle"] = identity(p.bundle->id);
			item["ort_core"] = text(p.bundle->runtime_build);
			item["ep_build"] = text(p.bundle->provider_build);
			item["file_hashes"] = Json::array();
			for (const auto &[path, digest] : p.bundle->file_hashes) {
				(void)path;
				item["file_hashes"].push_back(hash(digest));
			}
		}
		if (p.probe) {
			const auto &r = *p.probe;
			const auto failure = r.failure ? r.failure : r.provider.unavailable_reason;
			item["error"] = error_id(failure.code);
			item["available"] = r.provider.available;
			item["reason"] = failure                           ? std::string(error(failure.code).action)
					 : !r.provider.available           ? "provider unavailable"
					 : !r.provider.matches_obs_adapter ? "OBS adapter match unverified"
					 : !r.provider.permits_live_inference
						 ? "diagnostic only"
						 : "probe passed; graph and video transport require validation";
			item["ort_core"] = text(r.provider.runtime_build);
			item["ep_build"] = text(r.provider.provider_build);
			item["ort_api"] = r.negotiated_api;
			item["matches_obs_adapter"] = r.provider.matches_obs_adapter;
		}
		j["providers"].push_back(std::move(item));
	}
	for (const auto &r : s.rows) {
		Json m{{"id", identity(r.key.id)},
		       {"revision", identity(r.key.revision)},
		       {"state", ui::state_name(r.status.state)},
		       {"installed_bytes", r.installed_bytes},
		       {"error", error_id(r.status.failure.code)},
		       {"graphs", Json::array()}};
		m["verification"] = r.status.state == ModelState::ready ? "verified by model manager" : "not ready";
		m["hash_scope"] = "expected manifest hashes; model manager verifies before Ready";
		if (r.manifest) {
			m["manifest_sha256"] = hash(r.manifest->digest);
			for (const auto &g : r.manifest->graphs) {
				Json graph{{"id", identity(g.id)}, {"weights_sha256", hash(g.weights_sha256)}};
				if (auto it = r.manifest->artifacts.find(g.graph); it != r.manifest->artifacts.end()) {
					graph["sha256"] = hash(it->second.sha256);
					graph["revision"] = identity(it->second.revision);
				}
				m["graphs"].push_back(std::move(graph));
			}
		}
		j["models"].push_back(std::move(m));
	}
	for (const auto &i : e.instances) {
		Json item{{"source_size", {i.source.width, i.source.height}}};
		if (i.settings)
			item["settings"] = settings_report(*i.settings);
		if (i.status) {
			const auto &v = *i.status;
			item["backend"] = backend_name(v.backend);
			constexpr const char *transports[] = {"auto (not selected)", "interop",           "staged grid",
							      "staged tiles",        "staged full frame", "synthetic"};
			const auto transport = static_cast<unsigned>(v.transport);
			item["transport"] = transport < std::size(transports) ? transports[transport] : "unknown";
			item["selection_reason"] = v.failure ? std::string(error(v.failure.code).action)
						   : v.lifecycle == Lifecycle::ready
							   ? "pipeline ready"
							   : "pipeline not active; passthrough";
			item["lifecycle"] = static_cast<int>(v.lifecycle);
			item["error"] = error_id(v.failure.code);
			item["model"] = identity(v.model_id);
			item["model_revision"] = identity(v.model_revision);
			item["timing"] = {{"age_median_ms", number(v.age_median_ms)},
					  {"age_p95_ms", number(v.age_p95_ms)},
					  {"inference_median_ms", number(v.inference_median_ms)},
					  {"input_transfer_median_ms", number(v.input_transfer_median_ms)},
					  {"output_transfer_median_ms", number(v.output_transfer_median_ms)},
					  {"queue_median_ms", number(v.queue_median_ms)},
					  {"keyed_fps", number(v.keyed_fps)},
					  {"submitted", v.submitted},
					  {"completed", v.completed},
					  {"dropped", v.dropped},
					  {"repeated", v.repeated}};
		}
		j["instances"].push_back(std::move(item));
	}
	const auto end = error_next.load(std::memory_order_relaxed);
	for (unsigned n = end > errors.size() ? end - static_cast<unsigned>(errors.size()) : 0; n < end; ++n) {
		const auto code = errors[n % errors.size()].load(std::memory_order_relaxed);
		if (code != ErrorCode::none)
			j["recent_errors"].push_back({{"code", error_id(code)}, {"action", error(code).action}});
	}
	j["privacy"] =
		"No media, logs, tokens, scene/source names, UUIDs or personal paths. Free-text settings and errors omitted. Nothing uploaded.";
	j["timing_scope"] =
		"W15 rolling window: up to 5 seconds / 1024 presentations; stale values are null. Counters retain producer scope.";
	return "CorridorKey diagnostics\n```json\n" + j.dump(2) + "\n```\n";
}
} // namespace corridorkey::diagnostics
