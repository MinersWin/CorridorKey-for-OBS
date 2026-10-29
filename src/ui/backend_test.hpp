// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "engine/providers/runtime.hpp"
#include <QDialog>
#include <future>
#include <QTimer>

class QComboBox;
class QPlainTextEdit;
class QPushButton;
namespace corridorkey::ui {
class BackendTest final : public QDialog {
	ort::ProbeConfig config_;
	QComboBox *provider_, *precision_;
	QPlainTextEdit *result_;
	QPushButton *start_, *cancel_;
	QTimer poll_;
	std::stop_source stop_;
	std::future<ProbeResult> future_;
	void start();
	void poll();

public:
	BackendTest(QWidget *, ort::ProbeConfig);
	~BackendTest() override;
};
} // namespace corridorkey::ui
