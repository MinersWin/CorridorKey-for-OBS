// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime_dialog.hpp"
#include "runtime_install.hpp"
#include "ui/backend_test.hpp"
#include "util/utf8_path.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <future>

namespace corridorkey::packaging {
namespace {
models::fs::path module_file(const char *name)
{
	char *raw = obs_module_file(name);
	std::unique_ptr<char, decltype(&bfree)> owned(raw, bfree);
	models::require(raw != nullptr, "Runtime catalogue is missing");
	return utf8_path(raw);
}
class RuntimeDialog final : public QDialog {
	QComboBox *choice_;
	QPlainTextEdit *text_;
	QCheckBox *accept_;
	QPushButton *install_, *cancel_, *test_;
	QLabel *status_, *size_;
	QProgressBar *progress_;
	QTimer poll_;
	std::future<std::string> work_;
	std::stop_source stop_;
	std::atomic<std::uint64_t> received_{0}, total_{0};
	std::string raw_;
	std::optional<RuntimePackage> package_;
	models::fs::path destination_;
	void select()
	{
		accept_->setChecked(false);
		test_->setEnabled(false);
		package_.reset();
		try {
			package_ = runtime_package(raw_, CK_RUNTIME_CATALOGUE_SHA256,
						   choice_->currentData().toString().toStdString());
			text_->setPlainText(QString::fromStdString(
				models::licence_notices(package_->download, package_->licences)));
			std::uint64_t bytes = 0;
			for (const auto &[id, artifact] : package_->download.artifacts)
				bytes = models::checked_add(bytes, artifact.bytes);
			size_->setText(
				tr("Download %1; extracted files %2. Keep room for both.")
					.arg(QLocale().formattedDataSize(static_cast<qint64>(bytes)),
					     QLocale().formattedDataSize(static_cast<qint64>(
						     package_->recipe.at("expanded_bytes").get<std::uint64_t>()))));
			status_->setText(tr("Read all licence texts, then accept to download from the vendors."));
			accept_->setEnabled(true);
			destination_ = models::obs_model_cache_path() / "runtime-cache" / "bundles" /
				       package_->download.key.id;
			const auto &descriptor = package_->recipe.at("descriptors").at(0);
			if (models::fs::exists(destination_ /
					       utf8_path(descriptor.at("filename").get<std::string>()))) {
				test_->setEnabled(true);
				status_->setText(tr(
					"A local bundle exists. Test it before using it; restart OBS to change runtimes."));
			}
		} catch (const std::exception &e) {
			size_->clear();
			text_->setPlainText(QString::fromUtf8(e.what()));
			status_->setText(tr("This bundle is unavailable. The bundled runtime remains available."));
			accept_->setEnabled(false);
		}
		install_->setEnabled(false);
	}
	void install()
	{
		if (!package_ || !accept_->isChecked() || work_.valid())
			return;
		if (obs_frontend_streaming_active() || obs_frontend_recording_active()) {
			status_->setText(tr("Stop streaming and recording before installing a runtime."));
			return;
		}
		stop_ = std::stop_source{};
		auto stop = stop_.get_token();
		auto package = *package_;
		auto root = models::obs_model_cache_path() / "runtime-cache";
		destination_ = root / "bundles" / package.download.key.id;
		choice_->setEnabled(false);
		accept_->setEnabled(false);
		install_->setEnabled(false);
		cancel_->setEnabled(true);
		total_ = received_ = 0;
		status_->setText(tr(
			"Downloading and verifying. Files become available only after the entire bundle passes verification."));
		work_ = std::async(std::launch::async, [this, package = std::move(package), root, stop] {
			try {
				models::Cache cache(root);
				models::AcceptanceStore accepted(cache);
				accepted.accept(package.download, package.licences, package.download.licences);
				install_runtime(package, cache, accepted, stop, [this](Progress p) {
					received_ = p.completed_bytes;
					total_ = p.total_bytes.value_or(0);
				});
				return std::string{};
			} catch (const std::exception &e) {
				return std::string(e.what());
			}
		});
	}
	void poll()
	{
		if (!work_.valid())
			return;
		auto total = total_.load(), received = received_.load();
		progress_->setRange(0, total ? 1000 : 0);
		if (total)
			progress_->setValue(static_cast<int>(std::min<std::uint64_t>(1000, received * 1000 / total)));
		if (work_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			return;
		auto error = work_.get();
		status_->setText(
			error.empty()
				? tr("Installed. Restart OBS before using the new runtime. You can test it in a disposable helper now.")
				: QString::fromStdString(error));
		choice_->setEnabled(true);
		accept_->setEnabled(true);
		cancel_->setEnabled(false);
		test_->setEnabled(error.empty());
		progress_->setRange(0, 1000);
		if (error.empty())
			progress_->setValue(1000);
		else
			install_->setEnabled(accept_->isChecked());
	}
	void test()
	{
		if (!package_)
			return;
		ort::ProbeConfig config;
#ifdef _WIN32
		config.helper = module_file("tools/corridorkey_backend_probe.exe");
#else
		config.helper = utf8_path(CK_RUNTIME_PROBE_HELPER);
#endif
		config.scratch = models::obs_model_cache_path().parent_path() / "backend-tests";
		config.fixture_fp32 = module_file("tools/probe-fp32.onnx");
		config.fixture_fp16 = module_file("tools/probe-fp16.onnx");
		for (const auto &d : package_->recipe.at("descriptors"))
			config.bundles.emplace(ort::parse_backend(d.at("backend").get<std::string>()),
					       destination_ / utf8_path(d.at("filename").get<std::string>()));
		(new ui::BackendTest(this, std::move(config)))->show();
	}

public:
	explicit RuntimeDialog(QWidget *parent) : QDialog(parent)
	{
		setAttribute(Qt::WA_DeleteOnClose);
		setWindowTitle(tr("CorridorKey runtimes"));
		resize(760, 660);
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(16, 16, 16, 16);
		layout->setSpacing(10);
		auto *intro = new QLabel(
			tr("NVIDIA acceleration is an optional download. The Windows installer already includes DirectML.\nRestart OBS when switching runtime cores; DirectML and NVIDIA cores cannot share this OBS process."),
			this);
		intro->setWordWrap(true);
		layout->addWidget(intro);
		choice_ = new QComboBox(this);
		choice_->setAccessibleName(tr("Runtime bundle"));
		layout->addWidget(choice_);
		size_ = new QLabel(this);
		size_->setWordWrap(true);
		layout->addWidget(size_);
		text_ = new QPlainTextEdit(this);
		text_->setReadOnly(true);
		text_->setAccessibleName(tr("Runtime licences"));
		layout->addWidget(text_, 1);
		accept_ = new QCheckBox(tr("I have read and accept all licence texts above."), this);
		layout->addWidget(accept_);
		progress_ = new QProgressBar(this);
		progress_->setRange(0, 1000);
		progress_->setValue(0);
		layout->addWidget(progress_);
		status_ = new QLabel(this);
		status_->setWordWrap(true);
		status_->setTextFormat(Qt::PlainText);
		layout->addWidget(status_);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
		install_ = buttons->addButton(tr("Download runtime"), QDialogButtonBox::ActionRole);
		cancel_ = buttons->addButton(tr("Cancel download"), QDialogButtonBox::ActionRole);
		test_ = buttons->addButton(tr("Test installed bundle"), QDialogButtonBox::ActionRole);
		cancel_->setEnabled(false);
		test_->setEnabled(false);
		layout->addWidget(buttons);
		connect(choice_, &QComboBox::currentIndexChanged, this, [this] { select(); });
		connect(accept_, &QCheckBox::toggled, this,
			[this](bool yes) { install_->setEnabled(yes && package_.has_value() && !work_.valid()); });
		connect(install_, &QPushButton::clicked, this, [this] { install(); });
		connect(cancel_, &QPushButton::clicked, this, [this] { stop_.request_stop(); });
		connect(test_, &QPushButton::clicked, this, [this] { test(); });
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
		connect(&poll_, &QTimer::timeout, this, [this] { poll(); });
		raw_ = models::read_small_file(module_file("runtimes/runtime-recipes-v1.json"));
		models::require(models::sha256(raw_) == CK_RUNTIME_CATALOGUE_SHA256,
				"Runtime catalogue differs from this plugin");
		auto catalogue = models::strict_json(raw_);
		int preferred = -1;
		for (const auto &[id, value] : catalogue.at("bundles").items()) {
#ifdef _WIN32
			if (value.value("platform", "windows-x64") != "windows-x64")
				continue;
#else
			if (value.value("platform", "linux-x86_64") != "linux-x86_64")
				continue;
#endif
			choice_->addItem(QString::fromStdString(value.value("display_name", id)),
					 QString::fromStdString(id));
			if (value.at("enabled").get<bool>() && (preferred < 0 || id == "nvidia-rtx-0.5.0"))
				preferred = choice_->count() - 1;
		}
		if (preferred >= 0)
			choice_->setCurrentIndex(preferred);
		select();
		poll_.start(150);
	}
	~RuntimeDialog() override
	{
		stop_.request_stop();
		if (work_.valid())
			work_.wait();
	}
};
QPointer<RuntimeDialog> dialog;
bool callback_registered = false;
void frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		callback_registered = false;
		delete dialog.data();
		dialog = nullptr;
	}
}
} // namespace
void register_runtime_manager()
{
	obs_frontend_add_tools_menu_item(
		"CorridorKey runtimes...",
		[](void *) {
			QMetaObject::invokeMethod(
				qApp,
				[] {
					try {
						if (!dialog)
							dialog = new RuntimeDialog(
								static_cast<QWidget *>(obs_frontend_get_main_window()));
						dialog->show();
						dialog->raise();
					} catch (const std::exception &) {
						blog(LOG_ERROR,
						     "[corridorkey] Runtime manager could not read its installed catalogue; reinstall the plugin");
					}
				},
				Qt::QueuedConnection);
		},
		nullptr);
	obs_frontend_add_event_callback(frontend_event, nullptr);
	callback_registered = true;
}
void shutdown_runtime_manager()
{
	if (callback_registered)
		obs_frontend_remove_event_callback(frontend_event, nullptr);
	callback_registered = false;
	delete dialog.data();
	dialog = nullptr;
}
} // namespace corridorkey::packaging
