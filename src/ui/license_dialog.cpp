// SPDX-License-Identifier: GPL-3.0-or-later
#include "license_dialog.hpp"
#include "visuals.hpp"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace corridorkey::ui {
LicenseDialog::LicenseDialog(QWidget *parent, const ModelRow &row, const models::LicenceRegistry &registry,
			     QString destination, bool review_only, std::function<void()> accepted, bool offline)
	: QDialog(parent)
{
	setWindowTitle(tr("Model licences"));
	setAttribute(Qt::WA_DeleteOnClose);
	setModal(false);
	setMinimumSize(560, 440);
	resize(680, 600);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 16);
	layout->setSpacing(8);
	auto *title = new QLabel(QString::fromStdString(row.name), this);
	auto font = title->font();
	font.setWeight(QFont::DemiBold);
	title->setFont(font);
	layout->addWidget(title);
	auto *summary =
		new QLabel(tr("Download %1 · Up to %2 additional space during setup")
				   .arg(QString::fromStdString(byte_size(row.download_bytes)),
					QString::fromStdString(byte_size(row.manifest->peak_additional_bytes()))),
			   this);
	summary->setTextFormat(Qt::PlainText);
	summary->setWordWrap(true);
	layout->addWidget(summary);
	layout->addWidget(new ElidedPath(destination, this));
	QString text;
	for (auto &[id, a] : row.manifest->artifacts)
		text += QString::fromStdString(a.id + " · " + byte_size(a.bytes) + "\n");
	text += "\n" + QString::fromStdString(models::licence_notices(*row.manifest, registry));
	auto *licences = new QPlainTextEdit(text, this);
	licences->setReadOnly(true);
	licences->setAccessibleName(tr("Required files, attributions and exact licence texts"));
	licences->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	layout->addWidget(licences, 1);
	auto *check = new QCheckBox(tr("I accept these model licences"), this);
	check->setChecked(false);
	check->setObjectName("acceptLicences");
	check->setVisible(!review_only);
	layout->addWidget(check);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	if (!review_only) {
		auto *install = buttons->addButton(offline ? tr("Accept and import") : tr("Accept and download"),
						   QDialogButtonBox::AcceptRole);
		install->setObjectName("acceptAndInstall");
		install->setEnabled(false);
		install->setDefault(true);
		connect(check, &QCheckBox::toggled, install, &QPushButton::setEnabled);
		connect(install, &QPushButton::clicked, this, [this, check, accepted = std::move(accepted)] {
			if (check->isChecked()) {
				accepted();
				close();
			}
		});
	}
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
	layout->addWidget(buttons);
}
} // namespace corridorkey::ui
