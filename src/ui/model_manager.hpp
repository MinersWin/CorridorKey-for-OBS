// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "manager_model.hpp"
#include "engine/providers/runtime.hpp"
#include <QDialog>
#include <QPointer>
#include <QTimer>

class QLabel;
class QTableWidget;
class QProgressBar;
class QPushButton;
namespace corridorkey::ui {
class ModelManager final : public QDialog {
	std::shared_ptr<ManagerController> controller_;
	ManagerSnapshot snapshot_;
	QTableWidget *table_;
	QLabel *intro_, *name_, *publisher_, *description_, *task_, *cache_, *space_, *notice_;
	QProgressBar *progress_;
	QPushButton *setup_, *install_, *remove_, *import_, *cancel_, *licence_;
	QTimer timer_;
	ort::ProbeConfig probe_;
	QPointer<QDialog> backend_dialog_;
	const ModelRow *selected() const;
	void render();
	void details();
	void review(bool only, std::optional<models::fs::path> offline = {});
	void import();
	void choose_import(bool zip);
	void diagnostics_preview();

public:
	ModelManager(QWidget *, std::shared_ptr<ManagerController>, ort::ProbeConfig);
#ifdef CORRIDORKEY_MANAGER_FIXTURES
	void fixture_snapshot(ManagerSnapshot);
#endif
};
void register_model_manager();
// Register W05 before the first dialog is opened; replacing a running service is refused.
bool set_model_binder(Binder);
void open_model_manager();
void shutdown_model_manager();
#ifdef CORRIDORKEY_MANAGER_FIXTURES
void start_manager_fixture();
#endif
} // namespace corridorkey::ui
