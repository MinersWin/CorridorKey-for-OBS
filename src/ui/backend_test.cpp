// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend_test.hpp"
#include "manager_model.hpp"
#include <obs-frontend-api.h>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>
#include <cmath>

namespace corridorkey::ui {
BackendTest::BackendTest(QWidget *parent, ort::ProbeConfig config) : QDialog(parent), config_(std::move(config))
{
	setWindowTitle(tr("Test backend"));
	setAttribute(Qt::WA_DeleteOnClose);
	resize(600, 480);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(8);
	auto *intro = new QLabel(tr("A short test runs in a separate helper process. It uses synthetic data.\n"
				    "Model compatibility and video transport need separate validation."),
				 this);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *form = new QFormLayout;
	provider_ = new QComboBox(this);
	provider_->setAccessibleName(tr("Provider"));
	for (auto b : {Backend::tensorrt_rtx, Backend::tensorrt, Backend::cuda, Backend::directml, Backend::migraphx,
		       Backend::cpu_diagnostic})
		provider_->addItem(QString::fromStdString(ort::backend_name(b)), static_cast<int>(b));
	precision_ = new QComboBox(this);
	precision_->addItem("FP32", static_cast<int>(Precision::fp32));
	precision_->addItem("FP16", static_cast<int>(Precision::fp16));
	precision_->setAccessibleName(tr("Precision"));
	form->addRow(tr("Provider"), provider_);
	form->addRow(tr("Precision"), precision_);
	layout->addLayout(form);
	result_ = new QPlainTextEdit(this);
	result_->setReadOnly(true);
	result_->setAccessibleName(tr("Backend test results"));
	result_->setPlainText(tr("Choose a provider and start the test. Missing runtime bundles are reported here."));
	layout->addWidget(result_, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	start_ = buttons->addButton(tr("Start test"), QDialogButtonBox::ActionRole);
	start_->setObjectName("startBackendTest");
	cancel_ = buttons->addButton(tr("Cancel test"), QDialogButtonBox::ActionRole);
	cancel_->setEnabled(false);
	connect(start_, &QPushButton::clicked, this, &BackendTest::start);
	connect(cancel_, &QPushButton::clicked, this, [this] {
		stop_.request_stop();
		cancel_->setEnabled(false);
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
	layout->addWidget(buttons);
	connect(&poll_, &QTimer::timeout, this, &BackendTest::poll);
	poll_.start(200);
}
BackendTest::~BackendTest()
{
	stop_.request_stop();
	if (future_.valid())
		future_.wait();
}
void BackendTest::start()
{
	if (future_.valid())
		return;
	if (obs_frontend_streaming_active() || obs_frontend_recording_active()) {
		result_->setPlainText(
			tr("Stop streaming and recording before running a backend test. Video stays in passthrough."));
		return;
	}
	ProbeRequest request;
	request.backend = static_cast<Backend>(provider_->currentData().toInt());
	request.precision = static_cast<Precision>(precision_->currentData().toInt());
	request.timeout = std::chrono::seconds(25);
	stop_ = std::stop_source{};
	auto stop = stop_.get_token();
	auto config = config_;
	result_->setPlainText(tr("Testing the selected provider in the helper process…"));
	start_->setEnabled(false);
	cancel_->setEnabled(true);
	provider_->setEnabled(false);
	precision_->setEnabled(false);
	future_ = std::async(std::launch::async, [config = std::move(config), request, stop] {
		return ort::DisposableProbe(config).probe(request, stop);
	});
}
void BackendTest::poll()
{
	if (!future_.valid() || future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return;
	const auto result = future_.get();
	QString text = tr("Provider: %1\nPrecision: %2\n")
			       .arg(QString::fromStdString(ort::backend_name(result.provider.backend)),
				    precision_->currentText());
	if (!result.provider.available) {
		// Error details are displayed locally; they never enter Copy diagnostics.
		text += tr("Unavailable: %1\n%2")
				.arg(QString::fromStdString(failure_text(result.failure)),
				     QString::fromStdString(result.diagnostic_detail));
	} else {
		text += tr(
			"Result: synthetic fixture passed\nTransport: host tensors (native video transport untested)\n");
		try {
			auto j = nlohmann::json::parse(result.diagnostic_detail);
			for (const auto *key : {"session_build_ms", "first_run_ms", "warm_median_ms"})
				if (j.contains(key) && j.at(key).is_number()) {
					const auto value = j.at(key).get<double>();
					if (std::isfinite(value) && value >= 0)
						text += QString::fromLatin1(key) + ": " +
							QString::number(value, 'f', 3) + " ms\n";
				}
		} catch (...) {
			text += tr("Timing details unavailable.\n");
		}
		text += tr("ORT API: %1\nThis result does not predict CorridorKey frame rate.")
				.arg(result.negotiated_api);
	}
	result_->setPlainText(text);
	start_->setEnabled(true);
	cancel_->setEnabled(false);
	provider_->setEnabled(true);
	precision_->setEnabled(true);
}
} // namespace corridorkey::ui
