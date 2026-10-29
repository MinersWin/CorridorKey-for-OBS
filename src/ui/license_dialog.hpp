// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "manager_model.hpp"
#include <QDialog>

namespace corridorkey::ui {
class LicenseDialog final : public QDialog {
public:
	LicenseDialog(QWidget *, const ModelRow &, const models::LicenceRegistry &, QString destination,
		      bool review_only, std::function<void()> accepted, bool offline = false);
};
} // namespace corridorkey::ui
