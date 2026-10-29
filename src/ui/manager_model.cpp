// SPDX-License-Identifier: GPL-3.0-or-later
#include "manager_model.hpp"
#include "telemetry/diagnostics.hpp"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace corridorkey::ui {
namespace {
std::mutex preparation_mutex;
std::map<InstanceId, PreparationTask> preparations;
} // namespace
void preparation_state(InstanceId id, ModelKey key, ModelState state, std::stop_source stop)
{
	std::lock_guard lock(preparation_mutex);
	auto &task = preparations[id];
	if (task.started == TimePoint{} || task.status.key != key)
		task.started = Clock::now();
	task.status = {std::move(key), state, {0, {}, true}, {}};
	task.cancellation = std::move(stop);
}
void preparation_finished(InstanceId id)
{
	std::lock_guard lock(preparation_mutex);
	preparations.erase(id);
}
std::vector<PreparationTask> preparation_tasks()
{
	std::lock_guard lock(preparation_mutex);
	std::vector<PreparationTask> result;
	for (const auto &[id, task] : preparations) {
		(void)id;
		result.push_back(task);
	}
	return result;
}
void cancel_preparation(const ModelKey &key)
{
	std::lock_guard lock(preparation_mutex);
	for (auto &[id, task] : preparations) {
		(void)id;
		if (task.status.key == key)
			task.cancellation.request_stop();
	}
}
bool working(ModelState s)
{
	return s == ModelState::downloading || s == ModelState::verifying || s == ModelState::binding ||
	       s == ModelState::building || s == ModelState::warming;
}
std::string state_name(ModelState s)
{
	switch (s) {
	case ModelState::missing:
		return "Not installed";
	case ModelState::awaiting_acceptance:
		return "Review licences";
	case ModelState::downloading:
		return "Downloading";
	case ModelState::verifying:
		return "Verifying";
	case ModelState::binding:
		return "Preparing weights";
	case ModelState::building:
		return "Building TensorRT engine";
	case ModelState::warming:
		return "Testing backend";
	case ModelState::ready:
		return "Ready";
	case ModelState::update_available:
		return "Update available";
	case ModelState::failed:
		return "Error";
	}
	return "Unknown";
}
Actions actions(const ModelRow &r)
{
	if (working(r.status.state))
		return {false, r.status.progress.can_cancel_now && !r.cancelling, false, false, "In progress"};
	const bool admitted = r.manifest.has_value() && r.catalogued;
	return {admitted && r.status.state != ModelState::ready, false, r.installed_bytes > 0, admitted,
		r.status.state == ModelState::failed             ? "Retry"
		: r.status.state == ModelState::update_available ? "Review update"
		: r.status.state == ModelState::ready            ? "Installed"
								 : "Review and install"};
}
std::string byte_size(std::uint64_t bytes)
{
	constexpr const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
	double size = static_cast<double>(bytes);
	int unit = 0;
	while (size >= 1024 && unit < 4) {
		size /= 1024;
		++unit;
	}
	std::ostringstream out;
	out << std::fixed << std::setprecision(unit == 0 ? 0 : size < 10 ? 2 : 1) << size << ' ' << units[unit];
	return out.str();
}
std::string failure_text(Error e)
{
	if (!e)
		return {};
	std::string reason;
	switch (e.code) {
	case ErrorCode::cancelled:
		reason = "Cancelled. Verified files and resumable downloads are kept. Retry to continue.";
		break;
	case ErrorCode::hash_mismatch:
		reason = "File verification failed. Retry to download a clean copy.";
		break;
	case ErrorCode::disk_full:
		reason = "Not enough free space. Free space on the cache drive, then retry.";
		break;
	case ErrorCode::licence_required:
		reason = "Review and accept every required model licence before installation.";
		break;
	case ErrorCode::busy:
		reason = "This revision is in use or being installed. Stop its filters and retry.";
		break;
	case ErrorCode::unsupported:
		reason = "The weight binder is not included in this build. Update the plugin, then retry.";
		break;
	case ErrorCode::io_failure:
		reason =
			"The files could not be read or downloaded. Check the connection and cache permissions, then retry.";
		break;
	default:
		reason = std::string(e.action);
		break;
	}
	return reason + " (" + std::string(error_id(e.code)) + ")";
}
std::vector<ModelRow> model_rows(const std::vector<models::Manifest> &manifests,
				 const std::map<std::string, models::Artifact> &sources)
{
	std::vector<ModelRow> rows;
	for (const auto &m : manifests) {
		ModelRow r;
		r.key = m.key;
		r.name = m.display_name;
		r.publisher = m.publisher;
		r.description = m.description;
		r.manifest = m;
		r.status.key = m.key;
		r.recommended = m.pipeline == "ck2" &&
				std::any_of(m.profiles.begin(), m.profiles.end(),
					    [](const auto &p) { return p.value("id", "") == "balanced"; });
		for (auto &[id, a] : m.artifacts)
			r.download_bytes = models::checked_add(r.download_bytes, a.bytes);
		rows.push_back(std::move(r));
	}
	auto placeholder = [&](std::string id, std::string name, std::string publisher, std::string why,
			       bool recommended, std::initializer_list<const char *> dependencies) {
		if (std::any_of(rows.begin(), rows.end(),
				[&](const auto &r) { return r.key.id == id || (recommended && r.recommended); }))
			return;
		ModelRow r;
		r.key.id = std::move(id);
		r.name = std::move(name);
		r.publisher = std::move(publisher);
		r.availability = std::move(why);
		r.recommended = recommended;
		for (const auto *key : dependencies)
			if (auto it = sources.find(key); it != sources.end())
				r.download_bytes = models::checked_add(r.download_bytes, it->second.bytes);
		rows.push_back(std::move(r));
	};
	placeholder(
		"ck2-balanced", "CorridorKey 2.0 · Balanced", "Corridor Digital / Niko Pueringer",
		"Release graphs and a signed package are not available yet. Source size excludes unreleased graphs.",
		true, {"ck2", "stats", "moge", "rvm", "refiner"});
	placeholder("refiner-v12", "Edge Refiner v12",
		    "Moritz Mantel · CorridorKey attribution: Corridor Digital / Niko Pueringer",
		    "Included with the recommended CK2 set when its package is released.", false, {"refiner"});
	placeholder("modnet", "MODNet hint", "ZHKKKe / MODNet contributors",
		    "Optional CK1 hint. A verified checkpoint and graph package are not available yet.", false, {});
	placeholder("mediapipe", "MediaPipe hint", "Google / MediaPipe contributors",
		    "Planned optional CK1 hint. No package is available.", false, {});
	return rows;
}
void ViewModel::apply(const std::vector<ModelStatus> &statuses, TimePoint now)
{
	for (auto &r : rows_)
		for (const auto &s : statuses)
			if (r.key == s.key) {
				auto &sample = samples_[r.key.id + r.key.revision];
				const bool restart = r.status.state != s.state ||
						     s.progress.completed_bytes < sample.second;
				const double seconds = std::chrono::duration<double>(now - sample.first).count();
				if (!restart && s.state == ModelState::downloading && sample.first != TimePoint{} &&
				    seconds >= .25) {
					r.bytes_per_second =
						static_cast<double>(s.progress.completed_bytes - sample.second) /
						seconds;
					sample = {now, s.progress.completed_bytes};
				} else if (restart || sample.first == TimePoint{}) {
					r.bytes_per_second = 0;
					sample = {now, s.progress.completed_bytes};
				}
				r.status = s;
				if (!working(s.state))
					r.cancelling = false;
			}
}
bool ViewModel::first_run() const
{
	return std::none_of(rows_.begin(), rows_.end(), [](const auto &r) { return r.installed_bytes > 0; });
}
std::string diagnostics(const ManagerSnapshot &s, std::string_view plugin, std::string_view obs, std::string_view qt,
			std::string_view os)
{
	corridorkey::diagnostics::Environment environment;
	environment.plugin = plugin;
	environment.obs = obs;
	environment.qt = qt;
	environment.os = os;
	return corridorkey::diagnostics::report(s, environment);
}
} // namespace corridorkey::ui
