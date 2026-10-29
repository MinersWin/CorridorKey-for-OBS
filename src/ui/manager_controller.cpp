// SPDX-License-Identifier: GPL-3.0-or-later
#include "manager_model.hpp"
#include "models/binder.hpp"
#include <algorithm>

namespace corridorkey::ui {
namespace {
std::uint64_t disk_bytes(const models::fs::path &path)
{
	std::error_code ec;
	std::uint64_t bytes = 0;
	models::fs::recursive_directory_iterator it(path, models::fs::directory_options::skip_permission_denied, ec),
		end;
	for (; !ec && it != end; it.increment(ec)) {
		const auto status = it->symlink_status(ec);
		if (ec)
			break;
		if (models::fs::is_symlink(status)) {
			it.disable_recursion_pending();
			continue;
		}
		if (models::fs::is_regular_file(status))
			bytes = models::checked_add(bytes, it->file_size(ec));
	}
	return bytes;
}
} // namespace
ManagerController::ManagerController(models::fs::path path, models::LicenceRegistry licences,
				     std::vector<ModelRow> rows, Binder binder,
				     std::shared_ptr<models::HttpClient> http)
	: cache_(std::move(path)),
	  licences_(std::move(licences)),
	  binder_(std::move(binder)),
	  model_(std::move(rows))
{
	std::vector<models::Manifest> manifests;
	for (const auto &r : model_.rows())
		if (r.manifest && r.catalogued)
			manifests.push_back(*r.manifest);
	service_ = std::make_unique<models::CatalogueService>(
		cache_, licences_, std::move(manifests),
		[this](const models::Manifest &m, models::Cache &cache, std::stop_token stop) {
			if (!binder_) {
				models::bind_package(m, cache, stop, [&](Progress p) {
					std::lock_guard lock(mutex_);
					binding_[m.key.id + m.key.revision] = {p.completed_bytes,
									       p.total_bytes.value_or(0)};
				});
				return;
			}
			auto result = binder_(m, cache, stop, [&](BindingProgress p) {
				std::lock_guard lock(mutex_);
				binding_[m.key.id + m.key.revision] = p;
			});
			if (result.failure)
				throw models::Failure(result.failure.code, "Weight preparation failed");
		},
		std::move(http));
	snapshot_.rows = model_.rows();
	worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
}
ManagerController::~ManagerController()
{
	worker_.request_stop();
	changed_.notify_all();
	if (worker_.joinable())
		worker_.join();
	for (auto &[key, stop] : stops_)
		stop.request_stop();
	// Join the catalogue worker while the binder's mutex and callback state still exist.
	service_.reset();
}
void ManagerController::enqueue(std::function<void()> command)
{
	std::lock_guard lock(mutex_);
	if (commands_.size() < 32)
		commands_.push_back(std::move(command));
	changed_.notify_one();
}
ManagerSnapshot ManagerController::snapshot() const
{
	std::lock_guard lock(mutex_);
	return snapshot_;
}
void ManagerController::install(ModelKey key, bool accepted, std::optional<models::fs::path> offline)
{
	enqueue([this, key = std::move(key), accepted, offline = std::move(offline)] {
		for (auto &r : model_.rows())
			if (r.key == key && r.manifest && r.catalogued) {
				if (working(r.status.state))
					return;
				{
					std::lock_guard lock(mutex_);
					snapshot_.notice.clear();
				}
				auto &stop = stops_[key.id + key.revision];
				stop = std::stop_source{};
				{
					std::lock_guard lock(mutex_);
					binding_.erase(key.id + key.revision);
				}
				r.cancelling = false;
				const auto licences = accepted ? r.manifest->licences
							       : std::vector<LicenceAcceptance>{};
				const auto failure =
					offline ? service_->request_import(key, licences, *offline, stop.get_token())
						: service_->request_install(key, licences, stop.get_token());
				if (failure) {
					r.status.state = ModelState::failed;
					r.status.failure = failure;
				}
			}
	});
}
void ManagerController::cancel(ModelKey key)
{
	cancel_preparation(key);
	enqueue([this, key = std::move(key)] {
		if (auto it = stops_.find(key.id + key.revision); it != stops_.end()) {
			it->second.request_stop();
			for (auto &r : model_.rows())
				if (r.key == key && working(r.status.state))
					r.cancelling = true;
		}
	});
}
void ManagerController::remove(ModelKey key)
{
	enqueue([this, key = std::move(key)] {
		for (auto &r : model_.rows())
			if (r.key == key) {
				// Do not clear current first: a failed lease check must preserve selection.
				if (working(r.status.state) || !cache_.remove(key, true)) {
					r.status.failure = error(ErrorCode::busy);
					std::lock_guard lock(mutex_);
					snapshot_.notice = failure_text(r.status.failure);
					return;
				}
				r.installed_bytes = 0;
				r.status = {key, ModelState::missing, {}, {}};
				validated_.erase(key.id + key.revision);
				{
					std::lock_guard lock(mutex_);
					snapshot_.notice.clear();
				}
			}
	});
}
void ManagerController::refresh(bool disk)
{
	if (disk) {
		discover_installed();
		// Keep the prepared current refiner; old readers defer removal until a later refresh.
		for (const auto &row : model_.rows())
			if (!row.catalogued && row.key.id == "refiner-v12") {
				const auto current = cache_.current(row.key.id);
				if (current && *current != row.key &&
				    std::any_of(model_.rows().begin(), model_.rows().end(), [&](const auto &candidate) {
					    return candidate.catalogued && candidate.key == *current;
				    }))
					cache_.remove(row.key);
			}
	}
	auto statuses = service_->snapshot();
	// CatalogueService keeps its last Ready state after removal; disk is authoritative.
	for (auto &s : statuses)
		if (s.state == ModelState::ready && !models::fs::exists(cache_.revision(s.key) / "ready.json"))
			s.state = ModelState::missing;
	model_.apply(statuses, Clock::now());
	for (auto &r : model_.rows())
		if (r.manifest) {
			if (disk) {
				r.accepted = models::AcceptanceStore(cache_).contains(*r.manifest, licences_);
				r.installed_bytes = disk_bytes(cache_.revision(r.key));
				if (!working(r.status.state) && r.installed_bytes > 0 &&
				    !validated_.contains(r.key.id + r.key.revision)) {
					auto ready = r.catalogued ? service_->acquire(r.key)
								  : cache_.acquire(*r.manifest);
					if (ready) {
						r.status.state = ModelState::ready;
						validated_.insert(r.key.id + r.key.revision);
					}
				}
			}
			std::lock_guard lock(mutex_);
			if (r.status.state == ModelState::binding)
				if (auto it = binding_.find(r.key.id + r.key.revision); it != binding_.end())
					r.status.progress = {it->second.completed,
							     it->second.total ? std::optional{it->second.total}
									      : std::nullopt,
							     true};
		}
	std::erase_if(model_.rows(), [](const auto &r) { return !r.catalogued && r.installed_bytes == 0; });
	for (auto &r : model_.rows())
		if (r.status.state == ModelState::missing && r.manifest) {
			if (std::any_of(model_.rows().begin(), model_.rows().end(), [&](const auto &other) {
				    return other.key.id == r.key.id && other.key.revision != r.key.revision &&
					   other.installed_bytes > 0;
			    }))
				r.status.state = ModelState::update_available;
		}
	ManagerSnapshot next;
	{
		std::lock_guard lock(mutex_);
		next = snapshot_;
	}
	next.rows = model_.rows();
	for (auto &r : next.rows)
		for (const auto &task : preparation_tasks())
			if (r.key == task.status.key) {
				r.status = task.status;
				r.preparation_started = task.started;
				r.cancelling = task.cancellation.stop_requested();
			}
	if (disk) {
		next.disk_measured = true;
		next.source_bytes = disk_bytes(cache_.path("blobs"));
		next.bound_bytes = disk_bytes(cache_.path("packages"));
		next.build_bytes = disk_bytes(cache_.path("engines"));
		next.total_bytes = disk_bytes(cache_.root());
		next.available_bytes = models::fs::space(cache_.root()).available;
	}
	std::lock_guard lock(mutex_);
	snapshot_ = std::move(next);
}
void ManagerController::discover_installed()
{
	for (const auto &family : models::fs::directory_iterator(cache_.path("packages"))) {
		if (!family.is_directory() || family.is_symlink())
			continue;
		const auto id = family.path().filename().string();
		try {
			models::identifier(id);
		} catch (const models::Failure &) {
			continue;
		}
		for (const auto &revision : models::fs::directory_iterator(cache_.path("packages/" + id))) {
			if (!revision.is_directory() || revision.is_symlink())
				continue;
			ModelKey key{id, revision.path().filename().string()};
			if (!models::hex_string(key.revision, 40) ||
			    std::any_of(model_.rows().begin(), model_.rows().end(),
					[&](const auto &r) { return r.key == key; }))
				continue;
			const auto path = cache_.revision(key);
			if (!models::fs::exists(path / "ready.json"))
				continue;
			try {
				auto receipt = models::strict_json(models::read_small_file(path / "ready.json"));
				auto m = models::parse_manifest(models::read_small_file(path / "manifest.json"),
								models::string(receipt.at("manifest_sha256")));
				models::require(m.key == key, "Cached revision identity differs");
				if (!models::AcceptanceStore(cache_).contains(m, licences_) || !cache_.acquire(m))
					continue;
				auto r = model_rows({m}, {}).front();
				r.catalogued = false;
				r.recommended = false;
				r.accepted = true;
				r.status.state = ModelState::ready;
				r.installed_bytes = disk_bytes(path);
				r.description +=
					" Installed revision retained for rollback. It is not offered for new downloads.";
				validated_.insert(key.id + key.revision);
				model_.rows().push_back(std::move(r));
			} catch (const std::exception &) {
				std::lock_guard lock(mutex_);
				snapshot_.notice =
					"An installed revision could not be verified. Check the cache or reinstall its package.";
			}
		}
	}
}
void ManagerController::run(std::stop_token stop)
{
	unsigned tick = 0;
	while (!stop.stop_requested()) {
		std::deque<std::function<void()>> commands;
		{
			std::unique_lock lock(mutex_);
			changed_.wait_for(lock, stop, std::chrono::milliseconds(500),
					  [this] { return !commands_.empty(); });
			commands.swap(commands_);
		}
		if (stop.stop_requested())
			break;
		try {
			for (auto &command : commands)
				command();
			refresh(tick++ % 10 == 0 || !commands.empty());
		} catch (const models::Failure &f) {
			std::lock_guard lock(mutex_);
			snapshot_.notice = failure_text(error(f.code));
		} catch (...) {
			std::lock_guard lock(mutex_);
			snapshot_.notice = failure_text(error(ErrorCode::io_failure));
		}
	}
}
} // namespace corridorkey::ui
