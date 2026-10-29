// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/model_manager.hpp"
#include "ui/license_dialog.hpp"
#include "ui/backend_test.hpp"
#include "ui/visuals.hpp"
#include "models/installation.hpp"
#include "tiny_fixture.hpp"
#include "util/utf8_path.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTableWidget>
#include <QTimer>
#include <QSplitter>

namespace corridorkey::ui {
namespace {
struct Capture : QObject {
	QString root = qEnvironmentVariable("CK_MANAGER_CAPTURE");
	QString theme = qEnvironmentVariable("CK_MANAGER_THEME");
	std::shared_ptr<ManagerController> service;
	ModelManager *dialog = nullptr;
	ManagerSnapshot snapshot;
	int step = 0;
	QTimer timer;
	void save(QWidget *widget, const QString &name)
	{
		for (auto *text : widget->findChildren<QLabel *>()) {
			const auto visible = QString(text->text()).replace('\\', '/');
			if (visible.contains("S:/Projects/", Qt::CaseInsensitive))
				throw std::runtime_error("Capture contains a local development path");
		}
		const auto pixels = widget->grab();
		if (!pixels.save(root + "/" + theme + "-" + name + ".png"))
			blog(LOG_ERROR, "[corridorkey-test] Screenshot save failed");
		QJsonObject item{{"obs", obs_get_version_string()},
				 {"qt", qVersion()},
				 {"device_pixel_ratio", widget->devicePixelRatioF()},
				 {"logical_width", widget->width()},
				 {"logical_height", widget->height()},
				 {"pixel_width", pixels.width()},
				 {"pixel_height", pixels.height()},
				 {"fixture", name != "first-run" && !name.startsWith("setup-")},
				 {"cursor", "excluded"}};
		QFile metadata(root + "/" + theme + "-metadata.json");
		QJsonObject document;
		if (metadata.open(QIODevice::ReadOnly)) {
			document = QJsonDocument::fromJson(metadata.readAll()).object();
			metadata.close();
		}
		document.insert(name, item);
		if (metadata.open(QIODevice::WriteOnly))
			metadata.write(QJsonDocument(document).toJson());
	}
	explicit Capture(QObject *parent) : QObject(parent)
	{
		QDir().mkpath(root);
		connect(&timer, &QTimer::timeout, this, [this] { advance(); });
		timer.start(700);
	}
	void advance()
	{
		try {
			if (step == 0) {
				open_model_manager();
				++step;
				return;
			}
			if (step == 1) {
				auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
				auto *production = main->findChild<ModelManager *>("corridorKeyModels");
				if (!production)
					throw std::runtime_error("Production manager did not open");
				for (auto *text : production->findChildren<QLabel *>())
					if (text->text() == "Calculating cache usage…" && waits++ < 10)
						return;
				waits = 0;
				save(production, "first-run");
				production->findChild<QPushButton *>("setupCorridorKey")->click();
				QTimer::singleShot(150, this, [this, production] {
					for (auto *window : production->findChildren<LicenseDialog *>()) {
						save(window, "setup-licences");
						window->findChild<QCheckBox *>("acceptLicences")->setChecked(true);
						save(window, "setup-accepted");
						window->close();
					}
				});
				// Exercise the public properties callback and Tools QAction on the same instance.
				auto *source = obs_source_create_private("corridorkey", "CorridorKey fixture", nullptr);
				auto *properties = obs_source_properties(source);
				obs_property_button_clicked(obs_properties_get(properties, "manage_models"), source);
				obs_properties_destroy(properties);
				obs_source_release(source);
				for (auto *action : main->findChildren<QAction *>())
					if (action->text() == obs_module_text("Models.Tools"))
						action->trigger();
				QTimer::singleShot(100, this, [this, main] {
					const auto dialogs = main->findChildren<ModelManager *>("corridorKeyModels");
					blog(LOG_INFO, "[corridorkey-test] Shared manager instances: %lld",
					     static_cast<long long>(dialogs.size()));
					for (auto *d : dialogs)
						d->hide();
				});
				++step;
				return;
			}
			if (step == 2) {
				auto folder = utf8_path(qEnvironmentVariable("CK_MANAGER_FIXTURE").toStdString());
				auto m = testing::manifest(folder);
				auto rows = model_rows({m}, {});
				rows[0].recommended = true;
				std::erase_if(rows, [](const auto &r) { return r.key.id == "ck2-balanced"; });
				rows[0].name = "Synthetic package · Balanced fixture";
				rows[0].description =
					"UI validation fixture. Tiny authored files; no CorridorKey inference or GPU engine build.";
				service = std::make_shared<ManagerController>(
					models::obs_model_cache_path() / "ui-validation" / theme.toStdString(),
					testing::licence_registry(), rows, testing::binder());
				dialog = new ModelManager(static_cast<QWidget *>(obs_frontend_get_main_window()),
							  service, {});
				dialog->setObjectName("fixtureManager");
				snapshot = service->snapshot();
				dialog->fixture_snapshot(snapshot);
				dialog->show();
				++step;
				return;
			}
			constexpr ModelState states[] = {ModelState::missing,          ModelState::awaiting_acceptance,
							 ModelState::downloading,      ModelState::verifying,
							 ModelState::binding,          ModelState::building,
							 ModelState::warming,          ModelState::ready,
							 ModelState::update_available, ModelState::failed};
			const QString names[] = {"not-installed", "acceptance", "downloading", "verifying", "binding",
						 "building",      "testing",    "ready",       "update",    "error"};
			if (step >= 3 && step < 23) {
				const auto i = (step - 3) / 2;
				if ((step - 3) % 2 == 0) {
					auto &r = snapshot.rows[0];
					r.status.state = states[i];
					r.status.failure = states[i] == ModelState::failed
								   ? error(ErrorCode::hash_mismatch)
								   : Error{};
					r.status.progress = {0, {}, true};
					if (states[i] == ModelState::downloading)
						r.status.progress = {36, r.download_bytes, true};
					if (states[i] == ModelState::binding)
						r.status.progress = {0, 4, true};
					r.installed_bytes = states[i] == ModelState::ready ||
									    states[i] == ModelState::update_available
								    ? 555
								    : 0;
					snapshot.notice = "State preview: " + state_name(states[i]) +
							  ". Fixture values; no model inference.";
					dialog->fixture_snapshot(snapshot);
					dialog->findChild<QTableWidget *>("modelList")->clearSelection();
				} else {
					auto *install = dialog->findChild<QPushButton *>("installModel");
					auto *cancel = dialog->findChild<QPushButton *>("cancelModelTask");
					if (working(states[i]) && (!install->isHidden() || cancel->isHidden()))
						throw std::runtime_error(
							"Active task must show cancellation without an install button");
					if (states[i] == ModelState::ready && install->isHidden())
						throw std::runtime_error(
							"Installed action was not restored after the task");
					const auto colors =
						state_palette(dialog,
							      states[i] == ModelState::ready    ? "text-success"
							      : states[i] == ModelState::failed ? "text-danger"
												: "",
							      working(states[i]));
					const auto ink = colors.color(QPalette::Active, QPalette::ButtonText);
					if ((states[i] == ModelState::ready && ink.green() <= ink.red()) ||
					    (states[i] == ModelState::failed && ink.red() <= ink.green()) ||
					    (working(states[i]) &&
					     ink == dialog->palette().color(QPalette::Active, QPalette::Text)))
						throw std::runtime_error(
							"Yami semantic state colours were not applied");
					blog(LOG_INFO, "[corridorkey-test] State %s icon %s, text %s",
					     names[i].toUtf8().constData(),
					     colors.color(QPalette::ButtonText).name().toUtf8().constData(),
					     dialog->palette().color(QPalette::Text).name().toUtf8().constData());
					save(dialog, names[i]);
				}
				++step;
				return;
			}
			if (step == 23) {
				auto *licence = new LicenseDialog(dialog, snapshot.rows[0], service->licences(),
								  "<local fixture cache>", false, [] {});
				licence->setObjectName("fixtureLicence");
				licence->show();
				++step;
				return;
			}
			if (step == 24) {
				auto *licence = dialog->findChild<LicenseDialog *>("fixtureLicence");
				const bool blocked =
					!licence->findChild<QPushButton *>("acceptAndInstall")->isEnabled();
				blog(LOG_INFO, "[corridorkey-test] Acceptance unchecked and blocked: %s",
				     blocked ? "yes" : "NO");
				save(licence, "licences");
				licence->close();
				++step;
				return;
			}
			if (step == 25) {
				for (auto *button : dialog->findChildren<QPushButton *>())
					if (button->text().contains("Copy diagnostics"))
						button->click();
				++step;
				return;
			}
			if (step == 26) {
				for (auto *window : dialog->findChildren<QDialog *>())
					if (window->windowTitle() == "Review diagnostics") {
						save(window, "diagnostics");
						window->close();
					}
				auto *backend = new BackendTest(dialog, {});
				backend->setObjectName("fixtureBackend");
				backend->show();
				backend->findChild<QPushButton *>("startBackendTest")->click();
				++step;
				return;
			}
			if (step == 27) {
				save(dialog->findChild<BackendTest *>("fixtureBackend"), "backend-unavailable");
				dialog->resize(640, 460);
				++step;
				return;
			}
			if (step == 28) {
				save(dialog, "compact");
				dialog->resize(800, 720);
				auto m = *snapshot.rows[0].manifest;
				// A real folder import exercises acceptance, verification and publication, not state previews.
				dialog->hide();
				service->install(m.key, true,
						 utf8_path(qEnvironmentVariable("CK_MANAGER_FIXTURE").toStdString()));
				++step;
				return;
			}
			if (step == 29) {
				const auto actual = service->snapshot();
				if (actual.rows[0].status.state != ModelState::ready ||
				    actual.rows[0].installed_bytes == 0) {
					if (++waits > 20)
						throw std::runtime_error("Fixture installation did not become ready");
					return;
				}
				blog(LOG_INFO, "[corridorkey-test] Real accepted folder import reached Ready");
				snapshot = actual;
				snapshot.notice =
					"Verified local fixture import completed while this manager was hidden. No model inference.";
				dialog->fixture_snapshot(snapshot);
				dialog->show();
				++step;
				return;
			}
			if (step == 30) {
				save(dialog, "import-ready");
				QFile done(root + "/" + theme + "-complete.txt");
				if (done.open(QIODevice::WriteOnly))
					done.write(
						"Shared instance, licence gate, missing backend and folder import passed.\n");
				timer.stop();
				dialog->close();
				dialog->deleteLater();
				dialog = nullptr;
				service.reset();
				capture_licences();
			}
		} catch (const std::exception &e) {
			blog(LOG_ERROR, "[corridorkey-test] %s", e.what());
			timer.stop();
			QTimer::singleShot(500, static_cast<QWidget *>(obs_frontend_get_main_window()),
					   &QWidget::close);
		}
	}
	void capture_properties()
	{
		auto *settings = obs_data_create();
		obs_data_set_int(settings, "width", 1280);
		obs_data_set_int(settings, "height", 720);
		obs_data_set_int(settings, "color", 0xff986e3b);
		auto *source = obs_source_create("color_source_v3", "CorridorKey Test card", settings, nullptr);
		obs_data_release(settings);
		auto *filter = obs_source_create("corridorkey", "CorridorKey", nullptr, nullptr);
		obs_source_filter_add(source, filter);
		obs_frontend_open_source_filters(source);
		QTimer::singleShot(500, this, [this, source, filter] {
			QDialog *window = nullptr;
			for (auto *widget : QApplication::topLevelWidgets())
				if (widget->windowTitle().contains("CorridorKey Test card"))
					window = qobject_cast<QDialog *>(widget);
			if (!window)
				throw std::runtime_error("OBS filter window did not open");
			window->resize(1000, 720);
			for (auto *splitter : window->findChildren<QSplitter *>())
				if (splitter->orientation() == Qt::Vertical)
					splitter->setSizes({160, 480});
			QTimer::singleShot(500, this, [this, window, source, filter] {
				save(window, "properties");
				auto *data = obs_source_get_settings(filter);
				obs_data_set_bool(data, "details_crop", true);
				obs_source_update(filter, data);
				obs_data_release(data);
				obs_source_update_properties(filter);
				QTimer::singleShot(500, this, [this, window, source, filter] {
					save(window, "properties-crop");
					auto *data = obs_source_get_settings(filter);
					obs_data_set_bool(data, "advanced", true);
					obs_data_set_bool(data, "details_crop", false);
					obs_source_update(filter, data);
					obs_data_release(data);
					obs_source_update_properties(filter);
					QTimer::singleShot(500, this, [this, window, source, filter] {
						save(window, "properties-advanced");
						window->close();
						obs_source_filter_remove(source, filter);
						obs_source_release(filter);
						obs_source_release(source);
						static_cast<QWidget *>(obs_frontend_get_main_window())->close();
					});
				});
			});
		});
	}

	int waits = 0;
	void capture_licences()
	{
		auto folder =
			utf8_path(qEnvironmentVariable("CK_MANAGER_FIXTURE").toStdString()).parent_path().parent_path();
		auto raw = models::read_small_file(folder / "data/models/licenses-v1.json");
		auto registry = models::LicenceRegistry::parse(raw);
		auto json = models::strict_json(raw);
		int delay = 0;
		for (const auto &[id, name] : std::vector<std::pair<std::string, QString>>{
			     {"LicenseRef-Corridor-Key-2.0", "corridorkey-licence"},
			     {"LicenseRef-MoGe-MIT-DINOv2", "moge-licence"},
			     {"CC-BY-4.0", "rvm-licence"}}) {
			for (const auto &record : json.at("licenses"))
				if (record.at("id") == id) {
					auto row = snapshot.rows[0];
					row.name = "Bundled licence preview";
					row.manifest->licences = {{id, record.at("text_sha256").get<std::string>()}};
					auto *window = new LicenseDialog(
						static_cast<QWidget *>(obs_frontend_get_main_window()), row, registry,
						"<local fixture cache>", true, [] {});
					QTimer::singleShot(delay, window, [window] { window->show(); });
					QTimer::singleShot(delay + 500, this, [this, window, name] {
						save(window, name);
						window->close();
					});
					delay += 700;
				}
		}
		QTimer::singleShot(delay + 500, this, [this] { capture_properties(); });
	}
};
} // namespace
void start_manager_fixture()
{
	if (!qEnvironmentVariableIsEmpty("CK_DIAGNOSTICS_CAPTURE")) {
		start_diagnostics_capture();
		return;
	}
	if (qEnvironmentVariableIsEmpty("CK_MANAGER_CAPTURE") || obs_frontend_streaming_active() ||
	    obs_frontend_recording_active())
		return;
	new Capture(qApp);
}
} // namespace corridorkey::ui
