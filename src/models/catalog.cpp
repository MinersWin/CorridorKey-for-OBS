// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "catalog.hpp"
#include "binder.hpp"
namespace corridorkey::models {
Catalogue admit_catalogue(Cache &cache, std::string_view bytes, std::string_view signature, std::string_view public_key)
{
	FileLock lock(cache.path("locks/catalogue.lock"), false, true);
	auto p = cache.path("receipts/catalogue.json");
	std::uint64_t floor = 0;
	std::string previous_digest;
	if (fs::exists(p)) {
		auto previous = strict_json(read_small_file(p));
		fields(previous, {"schema_version", "sequence", "sha256"});
		require(uint64(previous.at("schema_version")) == 1, "Invalid catalogue receipt");
		floor = uint64(previous.at("sequence"));
		previous_digest = string(previous.at("sha256"));
	}
	auto catalogue = parse_catalogue(bytes, signature, public_key, floor);
	require(catalogue.sequence != floor || previous_digest.empty() || previous_digest == sha256(bytes),
		"Conflicting catalogue sequence", ErrorCode::hash_mismatch);
	atomic_write(p,
		     Json{{"schema_version", 1}, {"sequence", catalogue.sequence}, {"sha256", sha256(bytes)}}.dump());
	return catalogue;
}
static auto index(const ModelKey &k)
{
	return std::pair{k.id, k.revision};
}
CatalogueService::CatalogueService(Cache &cache, LicenceRegistry licences, std::vector<Manifest> manifests,
				   BindWeights binder, std::shared_ptr<HttpClient> http, DownloadOptions options)
	: cache_(cache),
	  licences_(std::move(licences)),
	  acceptance_(cache),
	  downloads_(cache, std::move(http), std::move(options)),
	  binder_(std::move(binder))
{
	for (auto &m : manifests) {
		// Reparse so callers cannot bypass validation by changing a public struct.
		auto verified = parse_manifest(m.raw, m.digest);
		for (auto &a : verified.licences)
			(void)licences_.at(a);
		auto k = index(verified.key);
		require(manifests_.emplace(k, verified).second, "Duplicate package revision");
		statuses_[k] = {verified.key, ModelState::missing, {}, {}};
	}
	worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
}
CatalogueService::~CatalogueService()
{
	worker_.request_stop();
	changed_.notify_all();
}
std::vector<ModelStatus> CatalogueService::snapshot() const
{
	std::lock_guard lock(mutex_);
	std::vector<ModelStatus> out;
	for (auto &[k, status] : statuses_)
		out.push_back(status);
	return out;
}
void CatalogueService::state(const ModelKey &key, ModelState state, Error failure, Progress progress)
{
	std::lock_guard lock(mutex_);
	statuses_[index(key)] = {key, state, progress, failure};
}
std::shared_ptr<const ReadyModel> CatalogueService::acquire(const ModelKey &key)
{
	auto it = manifests_.find(index(key));
	if (it == manifests_.end())
		return {};
	try {
		require(acceptance_.contains(it->second, licences_), "Licence receipt missing",
			ErrorCode::licence_required);
		auto ready = cache_.acquire(it->second);
		if (ready)
			state(key, ModelState::ready);
		return ready;
	} catch (const Failure &f) {
		state(key, ModelState::failed, error(f.code));
		return {};
	} catch (...) {
		state(key, ModelState::failed, error(ErrorCode::io_failure));
		return {};
	}
}
Error CatalogueService::enqueue(const ModelKey &key, const std::vector<LicenceAcceptance> &accepted,
				std::stop_token stop, std::optional<fs::path> offline)
{
	try {
		auto it = manifests_.find(index(key));
		if (it == manifests_.end())
			return error(ErrorCode::model_missing);
		if (stop.stop_requested())
			return error(ErrorCode::cancelled);
		std::lock_guard lock(mutex_);
		if (pending_.contains(index(key)))
			return {}; // same job shared by all filter instances
		if (jobs_.size() >= 32)
			return error(ErrorCode::busy);
		if (!accepted.empty())
			acceptance_.accept(it->second, licences_, accepted);
		if (!acceptance_.contains(it->second, licences_)) {
			statuses_[index(key)] = {key,
						 ModelState::awaiting_acceptance,
						 {},
						 error(ErrorCode::licence_required)};
			return error(ErrorCode::licence_required);
		}
		jobs_.push_back({it->second, stop, std::move(offline)});
		pending_.insert(index(key));
		statuses_[index(key)] = {key, ModelState::downloading, {}, {}};
		changed_.notify_one();
		return {};
	} catch (const Failure &f) {
		return error(f.code);
	} catch (...) {
		return error(ErrorCode::io_failure);
	}
}
Error CatalogueService::request_install(const ModelKey &key, const std::vector<LicenceAcceptance> &accepted,
					std::stop_token stop)
{
	return enqueue(key, accepted, stop, {});
}
Error CatalogueService::request_import(const ModelKey &key, const std::vector<LicenceAcceptance> &accepted,
				       fs::path source, std::stop_token stop)
{
	return enqueue(key, accepted, stop, std::move(source));
}
void CatalogueService::run(std::stop_token shutdown)
{
	for (;;) {
		std::optional<Job> job;
		{
			std::unique_lock lock(mutex_);
			if (!changed_.wait(lock, shutdown, [this] { return !jobs_.empty(); }))
				return;
			job = std::move(jobs_.front());
			jobs_.pop_front();
		}
		auto &m = job->manifest;
		std::stop_source source;
		std::stop_callback on_shutdown(shutdown, [&source] { source.request_stop(); });
		std::stop_callback on_cancel(job->stop, [&source] { source.request_stop(); });
		auto stop = source.get_token();
		try {
			FileLock data(cache_.path("locks/data.lock"), true, true, stop);
			FileLock revision(cache_.revision_lock(m.key), false, true, stop);
			// A different process may have finished while we waited. Do not upgrade a lease.
			if (!fs::exists(cache_.revision(m.key) / "ready.json")) {
				cache_.check_space(m.peak_additional_bytes());
				if (job->offline)
					downloads_.import_offline(m, *job->offline, licences_, acceptance_, stop);
				else {
					std::uint64_t completed = 0, total = 0;
					for (auto &[id, a] : m.artifacts)
						total = checked_add(total, a.bytes);
					for (auto &[id, a] : m.artifacts) {
						downloads_.fetch(m, a, licences_, acceptance_, stop, [&](Progress p) {
							p.completed_bytes = checked_add(completed, p.completed_bytes);
							p.total_bytes = total;
							state(m.key, ModelState::downloading, {}, p);
						});
						completed = checked_add(completed, a.bytes);
					}
				}
				state(m.key, ModelState::verifying);
				for (auto &g : m.graphs)
					validate_export_metadata(
						read_small_file(cache_.blob(m.artifacts.at(g.metadata).sha256),
								32 * 1024 * 1024),
						g, m);
				state(m.key, ModelState::binding);
				{
					auto conversion =
						checked_add(m.staged_graph_bytes,
							    checked_add(m.bound_bytes, checked_add(m.workspace_bytes,
												   m.engine_bytes)));
					auto margin =
						std::max({m.margin_bytes, std::uint64_t(1) << 30, conversion / 10});
					cache_.check_space(checked_add(conversion, margin));
					cache_.prepare(m);
					if (binder_)
						binder_(m, cache_, stop);
					else
						bind_package(m, cache_, stop, [&](Progress p) {
							state(m.key, ModelState::binding, {}, p);
						});
					cache_.publish(m, stop);
				}
			}
			// Release the exclusive lock before acquiring the shared reader lock.
			revision = FileLock{};
			if (fs::exists(cache_.revision(m.key) / "ready.json")) {
				auto ready = cache_.acquire(m, stop);
				require(bool(ready), "Published revision unavailable", ErrorCode::busy);
				state(m.key, ModelState::ready);
			}
		} catch (const Failure &f) {
			state(m.key, ModelState::failed, error(f.code));
		} catch (const std::bad_alloc &) {
			state(m.key, ModelState::failed, error(ErrorCode::out_of_memory));
		} catch (...) {
			state(m.key, ModelState::failed, error(ErrorCode::io_failure));
		}
		std::lock_guard lock(mutex_);
		pending_.erase(index(m.key));
	}
}
} // namespace corridorkey::models
