// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "models/catalog.hpp"
#include <functional>
#include <mutex>

namespace corridorkey::ui {
struct PreparationTask {
	ModelStatus status;
	TimePoint started;
	std::stop_source cancellation;
};
void preparation_state(InstanceId, ModelKey, ModelState, std::stop_source);
void preparation_finished(InstanceId);
std::vector<PreparationTask> preparation_tasks();
void cancel_preparation(const ModelKey &);
struct BindingProgress {
	std::uint64_t completed = 0, total = 0;
};
struct BindingResult {
	Error failure;
};
// W05 adapter: materialize declared outputs only; W04 owns verification/publication.
using Binder = std::function<BindingResult(const models::Manifest &, models::Cache &, std::stop_token,
					   const std::function<void(BindingProgress)> &)>;
struct ModelRow {
	ModelKey key;
	std::string name, publisher, description, availability;
	std::optional<models::Manifest> manifest;
	ModelStatus status;
	std::uint64_t download_bytes = 0, installed_bytes = 0;
	double bytes_per_second = 0;
	bool recommended = false, accepted = false, cancelling = false;
	bool catalogued = true;
	TimePoint preparation_started{};
};
struct Actions {
	bool install = false, cancel = false, remove = false, import = false;
	std::string primary;
};
Actions actions(const ModelRow &);
bool working(ModelState);
std::string state_name(ModelState);
std::string byte_size(std::uint64_t);
std::string failure_text(Error);
std::vector<ModelRow> model_rows(const std::vector<models::Manifest> &,
				 const std::map<std::string, models::Artifact> &);
// Progress belongs to one attempt. Reset rate samples on transitions/restarts.
class ViewModel {
	std::vector<ModelRow> rows_;
	std::map<std::string, std::pair<TimePoint, std::uint64_t>> samples_;

public:
	explicit ViewModel(std::vector<ModelRow> rows) : rows_(std::move(rows)) {}
	const std::vector<ModelRow> &rows() const { return rows_; }
	std::vector<ModelRow> &rows() { return rows_; }
	void apply(const std::vector<ModelStatus> &, TimePoint);
	bool first_run() const;
};
struct ManagerSnapshot {
	std::vector<ModelRow> rows;
	bool disk_measured = false;
	std::uint64_t source_bytes = 0, bound_bytes = 0, build_bytes = 0, total_bytes = 0, available_bytes = 0;
	std::string notice;
};
// This report accepts only allowlisted values. Never pass arbitrary log/helper text.
std::string diagnostics(const ManagerSnapshot &, std::string_view plugin, std::string_view obs, std::string_view qt,
			std::string_view os);

class ManagerController {
	models::Cache cache_;
	models::LicenceRegistry licences_;
	Binder binder_;
	ViewModel model_;
	mutable std::mutex mutex_;
	ManagerSnapshot snapshot_;
	std::deque<std::function<void()>> commands_;
	std::map<std::string, std::stop_source> stops_;
	std::map<std::string, BindingProgress> binding_;
	std::set<std::string> validated_;
	std::unique_ptr<models::CatalogueService> service_;
	std::condition_variable_any changed_;
	std::jthread worker_;
	void run(std::stop_token);
	void refresh(bool disk);
	void discover_installed();
	void enqueue(std::function<void()>);

public:
	ManagerController(models::fs::path, models::LicenceRegistry, std::vector<ModelRow>, Binder = {},
			  std::shared_ptr<models::HttpClient> = models::system_http_client());
	~ManagerController();
	ManagerSnapshot snapshot() const;
	const models::fs::path &cache_path() const { return cache_.root(); }
	const models::LicenceRegistry &licences() const { return licences_; }
	void install(ModelKey, bool accepted, std::optional<models::fs::path> offline = {});
	void cancel(ModelKey);
	void remove(ModelKey);
};
} // namespace corridorkey::ui
