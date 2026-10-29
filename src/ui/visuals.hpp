// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <obs-module.h>
#include <util/bmem.h>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>

namespace corridorkey::ui {
// OBS semantic classes supply theme colours; other themes fall back to text.
inline QPalette state_palette(QWidget *host, const char *semantic_class, bool active)
{
	auto result = host->palette();
	QLabel sample(host);
	sample.setProperty("class", semantic_class);
	sample.ensurePolished();
	const auto tone = active ? result.color(QPalette::Active, QPalette::Link)
				 : sample.palette().color(QPalette::Active, QPalette::WindowText);
	const auto text = result.color(QPalette::Active, QPalette::Text);
	const QColor ink((tone.red() * 4 + text.red()) / 5, (tone.green() * 4 + text.green()) / 5,
			 (tone.blue() * 4 + text.blue()) / 5);
	for (const auto group : {QPalette::Active, QPalette::Inactive})
		result.setColor(group, QPalette::ButtonText, ink);
	return result;
}

// Render the authored SVG silhouette in the host's palette, including selection.
inline QIcon mono_icon(const char *name, const QPalette &palette, QPalette::ColorRole role = QPalette::ButtonText)
{
	const auto relative = QString("icons/%1.svg").arg(name).toUtf8();
	char *path = obs_module_file(relative.constData());
	QIcon source(QString::fromUtf8(path ? path : ""));
	bfree(path);
	QIcon result;
	for (const auto mode : {QIcon::Normal, QIcon::Disabled, QIcon::Selected}) {
		const auto color = palette.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active,
						 mode == QIcon::Selected ? QPalette::HighlightedText : role);
		for (const int size : {16, 24, 32, 48, 64}) {
			auto pixels = source.pixmap(size, size);
			QPainter painter(&pixels);
			painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
			painter.fillRect(pixels.rect(), color);
			painter.end();
			result.addPixmap(pixels, mode);
		}
	}
	return result;
}

class ElidedPath final : public QLabel {
	QString path_;
	void resizeEvent(QResizeEvent *event) override
	{
		QLabel::resizeEvent(event);
		setText(fontMetrics().elidedText(path_, Qt::ElideMiddle, contentsRect().width()));
	}

public:
	ElidedPath(const QString &path, QWidget *parent) : QLabel(parent), path_(path)
	{
		setTextFormat(Qt::PlainText);
		setToolTip(path);
		setAccessibleName(tr("Model storage folder"));
		setAccessibleDescription(path);
		// Keep portable paths useful without displaying the installation's parent folders.
		path_.replace('\\', '/');
		const auto portable = path_.indexOf("/config/obs-studio/", 0, Qt::CaseInsensitive);
		if (portable >= 0)
			path_ = tr("<Portable OBS>") + path_.mid(portable);
		setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		setMinimumWidth(80);
		setText(path_);
	}
};
} // namespace corridorkey::ui
