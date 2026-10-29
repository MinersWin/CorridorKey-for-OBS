// SPDX-License-Identifier: GPL-3.0-or-later
#include "model_manager.hpp"
#include "backend_test.hpp"
#include "license_dialog.hpp"
#include "telemetry/diagnostics_obs.hpp"
#include "util/utf8_path.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QSysInfo>
#include <QSignalBlocker>
#include <QScrollArea>
#include <algorithm>

namespace corridorkey::ui {
namespace {
QPointer<ModelManager> manager;
QPointer<QDialog> manager_error;
std::shared_ptr<ManagerController> controller;
Binder registered_binder;
QString q(const std::string &s)
{
	return QString::fromStdString(s);
}
QString path_text(const models::fs::path &p)
{
	const auto s = p.u8string();
	return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
models::fs::path module_path(const char *relative)
{
	char *raw = obs_module_file(relative);
	std::unique_ptr<char, decltype(&bfree)> owned(raw, bfree);
	return raw ? utf8_path(raw) : models::fs::path{};
}
QLabel *label(QWidget *parent, const QString &text = {})
{
	auto *l = new QLabel(text, parent);
	l->setTextFormat(Qt::PlainText);
	l->setWordWrap(true);
	l->setTextInteractionFlags(Qt::TextSelectableByMouse);
	return l;
}
void heading(QLabel *l, bool large = false)
{
	auto f = l->font();
	f.setWeight(QFont::DemiBold);
	if (large) {
		if (f.pointSizeF() > 0)
			f.setPointSizeF(f.pointSizeF() * 1.3);
		else if (f.pixelSize() > 0)
			f.setPixelSize(static_cast<int>(f.pixelSize() * 1.3));
	}
	l->setFont(f);
}
ort::ProbeConfig probe_config()
{
	ort::ProbeConfig config;
#ifdef _WIN32
	config.helper = module_path("tools/corridorkey_backend_probe.exe");
#else
	config.helper = utf8_path(CORRIDORKEY_PROBE_HELPER);
#endif
	config.scratch = models::obs_model_cache_path().parent_path() / "backend-tests";
	config.fixture_fp32 = module_path("tools/probe-fp32.onnx");
	config.fixture_fp16 = module_path("tools/probe-fp16.onnx");
	// Only installer-owned runtime descriptors may select native libraries.
	const auto index = module_path("runtimes/probes.json");
	if (!index.empty() && models::fs::exists(index)) {
		auto j = models::strict_json(models::read_small_file(index));
		models::fields(j, {"schema_version", "bundles"});
		models::require(j.at("schema_version") == 1 && j.at("bundles").is_object(), "Invalid probe index");
		for (auto &[name, value] : j.at("bundles").items()) {
			const auto relative = models::string(value);
			models::relative_path(relative);
			config.bundles.emplace(ort::parse_backend(name), index.parent_path() / utf8_path(relative));
		}
	}
	return config;
}
std::shared_ptr<ManagerController> make_controller()
{
	auto licences = models::LicenceRegistry::parse(models::read_small_file(module_path("models/licenses-v1.json")));
	auto sources = models::parse_source_inventory(models::read_small_file(module_path("models/sources-v1.json")));
	std::vector<models::Manifest> manifests;
	const auto catalogue_path = module_path("models/catalogue-v1.json");
	auto bytes = models::read_small_file(catalogue_path);
	auto index = models::strict_json(bytes);
	if (!index.at("packages").empty()) {
		// The release key is compiled in. A catalogue cannot supply its own trust.
		const std::string public_key = CORRIDORKEY_CATALOGUE_PUBLIC_KEY;
		models::require(!public_key.empty(), "Release catalogue key not configured", ErrorCode::unsupported);
		auto sig = models::strict_json(models::read_small_file(module_path("models/catalogue-signature.json")));
		models::Cache cache(models::obs_model_cache_path());
		auto admitted = models::admit_catalogue(cache, bytes, models::string(sig.at("signature")), public_key);
		for (const auto &pin : admitted.entries) {
			const auto path = module_path(("models/manifests/" + pin.manifest.sha256 + ".json").c_str());
			manifests.push_back(models::parse_manifest(models::read_small_file(path), pin));
		}
	}
	return std::make_shared<ManagerController>(models::obs_model_cache_path(), std::move(licences),
						   model_rows(manifests, sources), registered_binder);
}
void frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT)
		shutdown_model_manager();
#ifdef CORRIDORKEY_MANAGER_FIXTURES
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		start_manager_fixture();
#endif
}
} // namespace
ModelManager::ModelManager(QWidget *parent, std::shared_ptr<ManagerController> service, ort::ProbeConfig config)
	: QDialog(parent),
	  controller_(std::move(service)),
	  probe_(std::move(config))
{
	setWindowTitle(tr("CorridorKey Models"));
	setObjectName("corridorKeyModels");
	setModal(false);
	setMinimumSize(640, 460);
	resize(800, 728);
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(16, 16, 16, 16);
	outer->setSpacing(16);
	auto *scroll = new QScrollArea(this);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidgetResizable(true);
	auto *content = new QWidget(scroll);
	scroll->setWidget(content);
	outer->addWidget(scroll, 1);
	auto *layout = new QVBoxLayout(content);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(8);
	auto *title = label(this, tr("CorridorKey Models"));
	heading(title, true);
	layout->addWidget(title);
	layout->addWidget(label(this, tr("Models run on this computer. Camera video is not uploaded.")));
	auto *first = new QHBoxLayout;
	intro_ = label(this);
	first->addWidget(intro_, 1);
	setup_ = new QPushButton(tr("Set up CorridorKey"), this);
	setup_->setObjectName("setupCorridorKey");
	setup_->setDefault(true);
	first->addWidget(setup_);
	layout->addLayout(first);
	layout->addSpacing(8);
	table_ = new QTableWidget(0, 4, this);
	table_->setObjectName("modelList");
	table_->setAccessibleName(tr("Models and installation status"));
	table_->setHorizontalHeaderLabels({tr("Model"), tr("State"), tr("Download"), tr("Installed")});
	table_->verticalHeader()->hide();
	table_->setShowGrid(false);
	table_->setAlternatingRowColors(false);
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setSelectionMode(QAbstractItemView::SingleSelection);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table_->setWordWrap(false);
	table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 8px; }");
	table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	for (int i = 2; i < 4; ++i)
		table_->horizontalHeaderItem(i)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
	table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	for (int i = 1; i < 4; ++i)
		table_->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
	table_->setMinimumHeight(168);
	layout->addWidget(table_, 1);
	name_ = label(this);
	heading(name_);
	publisher_ = label(this);
	description_ = label(this);
	layout->addSpacing(8);
	layout->addWidget(name_);
	layout->addWidget(publisher_);
	layout->addWidget(description_);
	auto *actions = new QHBoxLayout;
	install_ = new QPushButton(this);
	install_->setObjectName("installModel");
	actions->addWidget(install_);
	remove_ = new QPushButton(tr("Remove…"), this);
	actions->addWidget(remove_);
	import_ = new QPushButton(tr("Import…"), this);
	auto *import_menu = new QMenu(import_);
	import_menu->addAction(tr("From a folder…"), this, &ModelManager::import);
	import_menu->addAction(tr("From a ZIP file…"), this, [this] { choose_import(true); });
	import_->setMenu(import_menu);
	actions->addWidget(import_);
	actions->addStretch();
	licence_ = new QPushButton(tr("View licences"), this);
	actions->addWidget(licence_);
	layout->addLayout(actions);
	task_ = label(this);
	layout->addWidget(task_);
	auto *progress_row = new QHBoxLayout;
	progress_ = new QProgressBar(this);
	progress_->setAccessibleName(tr("Model task progress"));
	progress_row->addWidget(progress_, 1);
	cancel_ = new QPushButton(tr("Cancel task"), this);
	cancel_->setObjectName("cancelModelTask");
	progress_row->addWidget(cancel_);
	layout->addLayout(progress_row);
	auto cache_path = QDir::fromNativeSeparators(path_text(controller_->cache_path()));
	if (cache_path.startsWith(QDir::homePath(), Qt::CaseInsensitive))
		cache_path.replace(0, QDir::homePath().size(), "~");
	cache_ = label(this, tr("Cache: %1").arg(cache_path));
	auto *cache_row = new QHBoxLayout;
	cache_row->addWidget(cache_, 1);
	auto *open = new QPushButton(tr("Open folder"), this);
	open->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
	cache_row->addWidget(open);
	layout->addSpacing(8);
	layout->addLayout(cache_row);
	space_ = label(this);
	layout->addWidget(space_);
	notice_ = label(this);
	layout->addWidget(notice_);
	auto *footer = new QDialogButtonBox(QDialogButtonBox::Close, this);
	auto *test = footer->addButton(tr("Test backend"), QDialogButtonBox::ActionRole);
	auto *copy = footer->addButton(tr("Copy diagnostics…"), QDialogButtonBox::ActionRole);
	auto *report = footer->addButton(tr("Report a problem"), QDialogButtonBox::ActionRole);
	outer->addWidget(footer);
	connect(footer, &QDialogButtonBox::rejected, this, &QDialog::hide);
	connect(table_, &QTableWidget::itemSelectionChanged, this, &ModelManager::details);
	connect(install_, &QPushButton::clicked, this, [this] { review(false); });
	connect(licence_, &QPushButton::clicked, this, [this] { review(true); });
	connect(setup_, &QPushButton::clicked, this, [this] {
		for (std::size_t i = 0; i < snapshot_.rows.size(); ++i)
			if (snapshot_.rows[i].recommended) {
				table_->selectRow(static_cast<int>(i));
				review(false);
				break;
			}
	});
	connect(cancel_, &QPushButton::clicked, this, [this] {
		if (auto *r = selected())
			controller_->cancel(r->key);
	});
	connect(remove_, &QPushButton::clicked, this, [this] {
		const auto *row = selected();
		if (!row)
			return;
		auto key = row->key;
		auto *confirmation = new QDialog(this);
		confirmation->setAttribute(Qt::WA_DeleteOnClose);
		confirmation->setWindowTitle(tr("Remove model"));
		auto *box = new QVBoxLayout(confirmation);
		box->setContentsMargins(16, 16, 16, 16);
		box->setSpacing(8);
		box->addWidget(
			label(confirmation, tr("Remove %1? Active reader leases protect models in use.\n"
					       "Shared source downloads are kept for other models and future installs.")
						    .arg(q(row->name))));
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, confirmation);
		auto *remove = buttons->addButton(tr("Remove model"), QDialogButtonBox::DestructiveRole);
		connect(remove, &QPushButton::clicked, confirmation, [this, key, confirmation] {
			controller_->remove(key);
			confirmation->close();
		});
		connect(buttons, &QDialogButtonBox::rejected, confirmation, &QDialog::close);
		box->addWidget(buttons);
		confirmation->show();
	});
	connect(test, &QPushButton::clicked, this, [this] {
		if (!backend_dialog_)
			backend_dialog_ = new BackendTest(this, probe_);
		backend_dialog_->show();
		backend_dialog_->raise();
	});
	connect(copy, &QPushButton::clicked, this, &ModelManager::diagnostics_preview);
	connect(report, &QPushButton::clicked, this, [] {
		QDesktopServices::openUrl(QUrl("https://github.com/MinersWin/CorridorKey-for-OBS/issues/new/choose"));
	});
	connect(open, &QPushButton::clicked, this,
		[this] { QDesktopServices::openUrl(QUrl::fromLocalFile(path_text(controller_->cache_path()))); });
	connect(&timer_, &QTimer::timeout, this, [this] {
		snapshot_ = controller_->snapshot();
		render();
	});
	timer_.start(500);
	snapshot_ = controller_->snapshot();
	render();
	table_->selectRow(0);
}
const ModelRow *ModelManager::selected() const
{
	const auto i = table_->currentRow();
	return i >= 0 && static_cast<std::size_t>(i) < snapshot_.rows.size()
		       ? &snapshot_.rows[static_cast<std::size_t>(i)]
		       : nullptr;
}
void ModelManager::render()
{
	const QSignalBlocker blocker(table_);
	table_->setRowCount(static_cast<int>(snapshot_.rows.size()));
	for (std::size_t row = 0; row < snapshot_.rows.size(); ++row) {
		const auto &r = snapshot_.rows[row];
		const QString values[] = {q(r.name), q(state_name(r.status.state)),
					  r.download_bytes
						  ? q(byte_size(r.download_bytes)) + (r.manifest ? "" : tr(" + graphs"))
						  : tr("Pending"),
					  r.installed_bytes ? q(byte_size(r.installed_bytes)) : QString::fromUtf8("—")};
		for (int col = 0; col < 4; ++col) {
			auto *item = table_->item(static_cast<int>(row), col);
			if (!item) {
				item = new QTableWidgetItem;
				table_->setItem(static_cast<int>(row), col, item);
			}
			if (item->text() != values[col])
				item->setText(values[col]);
			item->setToolTip(col == 0 ? q(r.description) : values[col]);
			if (col >= 2)
				item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
		}
		table_->setRowHeight(static_cast<int>(row), 32);
	}
	const auto recommended =
		std::find_if(snapshot_.rows.begin(), snapshot_.rows.end(), [](const auto &r) { return r.recommended; });
	const bool first_run = ViewModel(snapshot_.rows).first_run();
	const bool offer_setup = first_run && recommended != snapshot_.rows.end() &&
				 recommended->status.state == ModelState::missing;
	setup_->setVisible(offer_setup);
	setup_->setEnabled(recommended != snapshot_.rows.end() && ui::actions(*recommended).install);
	setup_->setToolTip(recommended == snapshot_.rows.end() ? tr("No recommended package in this catalogue")
							       : q(recommended->availability));
	intro_->setText(offer_setup ? tr("Start with Balanced · CK2 and Edge Refiner v12\n%1")
					      .arg(recommended == snapshot_.rows.end()
							   ? tr("Package size pending")
							   : q(byte_size(recommended->download_bytes)) +
								     (recommended->manifest
									      ? tr(" total download")
									      : tr(" sources; graph size pending")))
				    : tr("Installed models are shared by all CorridorKey filters."));
	space_->setText(tr("Sources %1 · Packages %2 · Builds %3\nTotal cache %4 · Available %5")
				.arg(q(byte_size(snapshot_.source_bytes)), q(byte_size(snapshot_.bound_bytes)),
				     q(byte_size(snapshot_.build_bytes)), q(byte_size(snapshot_.total_bytes)),
				     q(byte_size(snapshot_.available_bytes))));
	if (!snapshot_.disk_measured)
		space_->setText(tr("Calculating cache usage…"));
	notice_->setText(q(snapshot_.notice));
	notice_->setVisible(!snapshot_.notice.empty());
	details();
}
void ModelManager::details()
{
	const auto *r = selected();
	if (!r)
		return;
	name_->setText(q(r->name) +
		       (r->key.revision.empty() ? "" : tr(" · revision %1").arg(q(r->key.revision.substr(0, 8)))));
	publisher_->setText(tr("By %1").arg(q(r->publisher)));
	description_->setText(q(r->availability.empty() ? r->description : r->availability));
	auto a = ui::actions(*r);
	install_->setText(q(a.primary));
	install_->setEnabled(a.install);
	remove_->setEnabled(a.remove);
	install_->setVisible(
		!(r->recommended && r->status.state == ModelState::missing && ViewModel(snapshot_.rows).first_run()));
	import_->setEnabled(a.import);
	licence_->setEnabled(r->manifest.has_value());
	const bool active = working(r->status.state);
	progress_->setVisible(active);
	cancel_->setVisible(active);
	cancel_->setEnabled(a.cancel);
	QString task = q(state_name(r->status.state));
	if (r->status.failure)
		task += " · " + q(failure_text(r->status.failure));
	else if (r->status.state == ModelState::downloading) {
		task += tr(" · %1 of %2 · %3/s")
				.arg(q(byte_size(r->status.progress.completed_bytes)),
				     r->status.progress.total_bytes ? q(byte_size(*r->status.progress.total_bytes))
								    : tr("unknown size"),
				     q(byte_size(static_cast<std::uint64_t>(std::max(0., r->bytes_per_second)))));
	} else if (r->status.state == ModelState::binding)
		task += tr(" · One-time local preparation");
	if (r->cancelling)
		task += tr(" · Cancelling at a safe boundary…");
	task_->setText(task);
	const auto &p = r->status.progress;
	if (p.total_bytes && *p.total_bytes) {
		progress_->setRange(0, 1000);
		progress_->setValue(static_cast<int>(
			std::min(1., static_cast<double>(p.completed_bytes) / static_cast<double>(*p.total_bytes)) *
			1000));
		progress_->setFormat("%p%");
	} else
		progress_->setRange(0, 0);
	progress_->setAccessibleDescription(task);
}
void ModelManager::review(bool only, std::optional<models::fs::path> offline)
{
	const auto *r = selected();
	if (!r || !r->manifest || (!only && !ui::actions(*r).install && !offline))
		return;
	const auto key = r->key;
	if (!only && r->accepted) {
		controller_->install(key, false, std::move(offline));
		return;
	}
	const bool is_offline = offline.has_value();
	auto *dialog = new LicenseDialog(
		this, *r, controller_->licences(), path_text(controller_->cache_path()), only,
		[service = controller_, key, offline = std::move(offline)] { service->install(key, true, offline); },
		is_offline);
	dialog->show();
}
void ModelManager::import()
{
	choose_import(false);
}
void ModelManager::choose_import(bool zip)
{
	auto *dialog = new QFileDialog(this, zip ? tr("Import a verified model bundle")
						 : tr("Choose an offline bundle folder"));
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setOption(QFileDialog::DontUseNativeDialog);
	dialog->setFileMode(zip ? QFileDialog::ExistingFile : QFileDialog::Directory);
	if (zip)
		dialog->setNameFilter(tr("ZIP bundles (*.zip)"));
	else
		dialog->setOption(QFileDialog::ShowDirsOnly);
	connect(dialog, &QFileDialog::fileSelected, this, [this](const QString &path) {
		if (!path.isEmpty())
			review(false, utf8_path(path.toUtf8().toStdString()));
	});
	dialog->setWindowModality(Qt::NonModal);
	dialog->show();
}
void ModelManager::diagnostics_preview()
{
	auto *dialog = new QDialog(this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(tr("Review diagnostics"));
	dialog->resize(780, 620);
	auto *layout = new QVBoxLayout(dialog);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(8);
	layout->addWidget(label(dialog, tr("Review this report before copying. Nothing is uploaded.")));
	auto *text = new QPlainTextEdit(
		q(corridorkey::diagnostics::report(snapshot_, corridorkey::diagnostics::collect_environment(probe_))),
		dialog);
	text->setReadOnly(true);
	text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	text->setLineWrapMode(QPlainTextEdit::NoWrap);
	text->setAccessibleName(tr("Redacted diagnostics preview"));
	layout->addWidget(text, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
	auto *copy = buttons->addButton(tr("Copy to clipboard"), QDialogButtonBox::ActionRole);
	connect(copy, &QPushButton::clicked, dialog,
		[text] { QApplication::clipboard()->setText(text->toPlainText()); });
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
	layout->addWidget(buttons);
	dialog->show();
}
#ifdef CORRIDORKEY_MANAGER_FIXTURES
void ModelManager::fixture_snapshot(ManagerSnapshot snapshot)
{
	timer_.stop();
	snapshot_ = std::move(snapshot);
	render();
	table_->selectRow(0);
}
#endif
void open_model_manager()
{
	QMetaObject::invokeMethod(
		qApp,
		[] {
			try {
				if (!manager) {
					if (!controller)
						controller = make_controller();
					manager =
						new ModelManager(static_cast<QWidget *>(obs_frontend_get_main_window()),
								 controller, probe_config());
				}
				manager->show();
				manager->raise();
				manager->activateWindow();
			} catch (...) {
				blog(LOG_ERROR,
				     "[corridorkey] Model manager setup failed; check installed catalogue and licence files");
				if (!manager_error) {
					manager_error =
						new QDialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
					manager_error->setAttribute(Qt::WA_DeleteOnClose);
					manager_error->setWindowTitle(QObject::tr("Model catalogue unavailable"));
					auto *layout = new QVBoxLayout(manager_error);
					layout->setContentsMargins(16, 16, 16, 16);
					layout->setSpacing(8);
					layout->addWidget(label(
						manager_error,
						QObject::tr(
							"The model catalogue or licence files could not be opened. "
							"Reinstall CorridorKey to restore them, then try again. Video stays in passthrough.")));
					auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, manager_error);
					QObject::connect(buttons, &QDialogButtonBox::rejected, manager_error,
							 &QDialog::close);
					layout->addWidget(buttons);
				}
				manager_error->show();
				manager_error->raise();
			}
		},
		Qt::QueuedConnection);
}
void register_model_manager()
{
	obs_frontend_add_tools_menu_item(
		obs_module_text("Models.Tools"), [](void *) { open_model_manager(); }, nullptr);
	obs_frontend_add_event_callback(frontend_event, nullptr);
}
bool set_model_binder(Binder binder)
{
	if (controller)
		return false;
	registered_binder = std::move(binder);
	return true;
}
void shutdown_model_manager()
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	delete manager.data();
	manager = nullptr;
	delete manager_error.data();
	manager_error = nullptr;
	controller.reset();
	registered_binder = {};
}
} // namespace corridorkey::ui
