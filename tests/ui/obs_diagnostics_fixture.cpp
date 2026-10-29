// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/model_manager.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace corridorkey::ui {
namespace {
struct Capture : QObject {
	QTimer timer;
	obs_scene_t *scene = nullptr;
	obs_source_t *previous = nullptr;
	int step = 0;
	QPointer<QDialog> preview;
	QString root = qEnvironmentVariable("CK_DIAGNOSTICS_CAPTURE");
	QString prefix = qEnvironmentVariable("CK_MANAGER_THEME", "obs33");
	void save(QWidget *widget, const QString &name)
	{
		const auto pixels = widget->grab();
		if (!pixels.save(root + "/" + prefix + "-" + name + ".png"))
			throw std::runtime_error("Cannot capture diagnostics");
		QFile file(root + "/" + prefix + "-" + name + "-metadata.json");
		if (file.open(QIODevice::WriteOnly))
			file.write(QJsonDocument(QJsonObject{{"obs", obs_get_version_string()},
							     {"qt", qVersion()},
							     {"device_pixel_ratio", widget->devicePixelRatioF()},
							     {"logical_width", widget->width()},
							     {"logical_height", widget->height()},
							     {"pixel_width", pixels.width()},
							     {"pixel_height", pixels.height()},
							     {"cursor", "excluded"},
							     {"workload",
							      "synthetic colour source; no model performance claim"}})
					   .toJson());
	}

	static void render(void *value, std::uint32_t, std::uint32_t)
	{
		obs_source_video_render(obs_scene_get_source(static_cast<Capture *>(value)->scene));
	}
	explicit Capture(QObject *parent) : QObject(parent)
	{
		QDir().mkpath(root);
		connect(&timer, &QTimer::timeout, this, [this] { advance(); });
		timer.start(1500);
	}
	~Capture() override { cleanup(); }
	void cleanup()
	{
		if (!scene)
			return;
		obs_remove_main_render_callback(render, this);
		obs_source_dec_showing(obs_scene_get_source(scene));
		obs_set_output_source(0, previous);
		obs_source_release(previous);
		obs_scene_release(scene);
		scene = nullptr;
		previous = nullptr;
	}
	void advance()
	{
		try {
			auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
			if (step == 0) {
				previous = obs_get_output_source(0);
				scene = obs_scene_create_private("W17 private scene marker");
				auto *data = obs_data_create();
				obs_data_set_int(data, "width", 1280);
				obs_data_set_int(data, "height", 720);
				obs_data_set_int(data, "color", 0xff408040);
				auto *source =
					obs_source_create_private("color_source_v3", "W17 private source marker", data);
				obs_data_release(data);
				if (!source || !scene)
					throw std::runtime_error("Cannot create the private color-card scene");
				data = obs_data_create();
				obs_data_set_bool(data, "w03_fake_enabled", true);
				obs_data_set_int(data, "w03_fake_delay", 20);
				obs_data_set_string(data, "audio_source_id", "W17 private audio marker");
				auto *filter =
					obs_source_create_private("corridorkey", "W17 private filter marker", data);
				obs_data_release(data);
				if (!filter) {
					obs_source_release(source);
					throw std::runtime_error("Cannot create the diagnostic filter");
				}
				obs_source_filter_add(source, filter);
				obs_source_release(filter);
				obs_scene_add(scene, source);
				obs_source_release(source);
				obs_set_output_source(0, obs_scene_get_source(scene));
				obs_source_inc_showing(obs_scene_get_source(scene));
				obs_add_main_render_callback(render, this);
				open_model_manager();
			} else if (step == 4) {
				auto *manager = main->findChild<ModelManager *>("corridorKeyModels");
				if (!manager)
					throw std::runtime_error("Production manager did not open");
				for (auto *button : manager->findChildren<QPushButton *>())
					if (button->text().contains("Copy diagnostics"))
						button->click();
			} else if (step == 5) {
				for (auto *dialog : main->findChildren<QDialog *>())
					if (dialog->windowTitle() == "Review diagnostics")
						preview = dialog;
				if (!preview)
					throw std::runtime_error("Diagnostics preview did not open");
				auto *text = preview->findChild<QPlainTextEdit *>();
				const auto report = text->toPlainText();
				for (const auto &forbidden :
				     {QDir::homePath(), QString("W17 private"), QString("huggingface.txt")})
					if (report.contains(forbidden, Qt::CaseInsensitive))
						throw std::runtime_error("Private marker leaked into diagnostics");
				const auto old_clipboard = QApplication::clipboard()->text();
				for (auto *button : preview->findChildren<QPushButton *>())
					if (button->text() == "Copy to clipboard")
						button->click();
				const bool copied = QApplication::clipboard()->text() == report;
				QApplication::clipboard()->setText(old_clipboard);
				if (!copied)
					throw std::runtime_error("Clipboard does not match the reviewed report");
				QFile file(root + "/" + prefix + "-report.txt");
				if (!file.open(QIODevice::WriteOnly) || file.write(report.toUtf8()) < 0)
					throw std::runtime_error("Cannot save diagnostics evidence");
				save(preview, "preview");
				const auto timing = text->document()->find("\"timing\":");
				if (timing.isNull())
					throw std::runtime_error("Timing fields are missing from diagnostics");
				text->setTextCursor(timing);
				text->verticalScrollBar()->setValue(std::max(0, timing.blockNumber() - 4));
			} else if (step == 6) {
				save(preview, "timing");
				preview->close();
				cleanup();
				QFile done(root +
					   (prefix == "obs33" ? "/complete.txt" : "/" + prefix + "-complete.txt"));
				if (done.open(QIODevice::WriteOnly))
					done.write(
						"Production report preview, clipboard equality and privacy markers passed.\n");
				blog(LOG_INFO,
				     "[corridorkey-test] Diagnostics preview, clipboard and redaction checks passed");
				timer.stop();
				main->close();
			}
			++step;
		} catch (const std::exception &e) {
			blog(LOG_ERROR, "[corridorkey-test] %s", e.what());
			timer.stop();
			cleanup();
			static_cast<QWidget *>(obs_frontend_get_main_window())->close();
		}
	}
};
} // namespace
void start_diagnostics_capture()
{
	if (!obs_frontend_streaming_active() && !obs_frontend_recording_active())
		new Capture(qApp);
}
} // namespace corridorkey::ui
