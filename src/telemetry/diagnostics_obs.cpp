// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagnostics_obs.hpp"
#include "host_policy.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QSysInfo>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#ifdef _WIN32
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdio>
#endif

namespace corridorkey::diagnostics {
Environment collect_environment(const ort::ProbeConfig &config)
{
	Environment e;
	e.plugin = CORRIDORKEY_VERSION;
	e.commit = CORRIDORKEY_BUILD_COMMIT;
	e.obs = obs_get_version_string();
	e.os = QSysInfo::prettyProductName().toStdString();
	e.kernel = (QSysInfo::kernelType() + " " + QSysInfo::kernelVersion()).toStdString();
	e.architecture = QSysInfo::currentCpuArchitecture().toStdString();
	e.qt = qVersion();
	obs_enter_graphics();
	const char *renderer = gs_get_device_name();
	e.renderer = renderer ? renderer : "unavailable";
	obs_leave_graphics();
	obs_video_info video{};
	if (obs_get_video_info(&video)) {
		e.canvas = {video.base_width, video.base_height};
	}
	using ActiveFps = double (*)();
	std::optional<double> active_fps;
	if (auto fps = reinterpret_cast<ActiveFps>(obs_symbol("obs_get_active_fps"))) {
		e.active_fps_api = true;
		active_fps = fps();
	}
	e.fps = frame_rate(video.fps_num, video.fps_den, active_fps);
	e.streaming = obs_frontend_streaming_active();
	e.recording = obs_frontend_recording_active();
#ifdef _WIN32
	try {
		e.devices = ort::enumerate_devices(Backend::directml);
		for (auto &d : e.devices) {
			if (d.driver.empty()) {
				d.driver = "unavailable";
				continue;
			}
			const auto v = std::stoull(d.driver);
			d.driver = std::to_string((v >> 48) & 0xffff) + "." + std::to_string((v >> 32) & 0xffff) + "." +
				   std::to_string((v >> 16) & 0xffff) + "." + std::to_string(v & 0xffff);
		}
		Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
		if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
			for (UINT n = 0;; ++n) {
				Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
				if (factory->EnumAdapters1(n, &adapter) != S_OK)
					break;
				DXGI_ADAPTER_DESC1 desc{};
				if (SUCCEEDED(adapter->GetDesc1(&desc))) {
					char luid[40]{};
					std::snprintf(luid, sizeof(luid), "luid:%08x:%08x",
						      static_cast<unsigned>(desc.AdapterLuid.HighPart),
						      static_cast<unsigned>(desc.AdapterLuid.LowPart));
					e.vram_bytes[luid] = desc.DedicatedVideoMemory;
				}
			}
		}
	} catch (...) {
		record_error(ErrorCode::backend_unavailable);
	}
#else
	const auto read = [](const QString &path) {
		QFile file(path);
		return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.read(512)).trimmed() : QString{};
	};
	for (const auto &card : QDir("/sys/class/drm").entryList({"card*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
		if (card.contains('-'))
			continue;
		const auto root = "/sys/class/drm/" + card + "/device";
		const auto vendor = read(root + "/vendor");
		if (vendor.isEmpty())
			continue;
		ort::Device d;
		d.adapter.vendor_id = vendor.toUInt(nullptr, 16);
		d.adapter.device_id = read(root + "/device").toUInt(nullptr, 16);
		d.adapter.stable_id = ("pci:" + QFileInfo(root).canonicalFilePath().section('/', -1)).toStdString();
		d.adapter.display_name = ("PCI " + vendor + ":" + read(root + "/device")).toStdString();
		const auto driver = QFileInfo(root + "/driver").canonicalFilePath().section('/', -1);
		const auto version = read("/sys/module/" + driver + "/version");
		d.driver = (driver + " " + (version.isEmpty() ? "kernel " + QSysInfo::kernelVersion() : version))
				   .toStdString();
		const auto vram = read(root + "/mem_info_vram_total");
		if (!vram.isEmpty())
			e.vram_bytes[d.adapter.stable_id] = vram.toULongLong();
		e.devices.push_back(std::move(d));
	}
#endif
	for (const auto backend : {Backend::tensorrt_rtx, Backend::tensorrt, Backend::cuda, Backend::directml,
				   Backend::migraphx, Backend::cpu_diagnostic}) {
		Provider p;
		p.backend = backend;
		p.probe = latest_probe(backend);
		if (const auto it = config.bundles.find(backend); it != config.bundles.end()) {
			try {
				p.bundle = ort::read_bundle(it->second);
			} catch (...) {
				ProbeResult failure;
				failure.provider.backend = backend;
				failure.failure = error(ErrorCode::invalid_input);
				p.probe = failure;
			}
		}
		e.providers.push_back(std::move(p));
	}
	e.instances = instances();
	return e;
}
} // namespace corridorkey::diagnostics
