// SPDX-License-Identifier: GPL-3.0-or-later
#include "loader.hpp"
#include <algorithm>
#include <cctype>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#else
#include <dlfcn.h>
#include <link.h>
#endif

namespace corridorkey::ort {
namespace {
std::recursive_mutex loader_mutex;
std::string basename(const fs::path &path)
{
	auto name = path.filename().string();
#ifdef _WIN32
	std::transform(name.begin(), name.end(), name.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
	return name;
}
std::vector<fs::path> modules()
{
	std::vector<fs::path> paths;
#ifdef _WIN32
	const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
	if (snapshot == INVALID_HANDLE_VALUE)
		throw std::runtime_error("Cannot inspect loaded runtime libraries");
	MODULEENTRY32W entry{sizeof(MODULEENTRY32W)};
	if (Module32FirstW(snapshot, &entry))
		do {
			paths.emplace_back(entry.szExePath);
		} while (Module32NextW(snapshot, &entry));
	CloseHandle(snapshot);
#else
	dl_iterate_phdr(
		[](dl_phdr_info *info, size_t, void *ctx) {
			if (info->dlpi_name && *info->dlpi_name)
				static_cast<std::vector<fs::path> *>(ctx)->emplace_back(info->dlpi_name);
			return 0;
		},
		&paths);
#endif
	return paths;
}
void collision_check(const fs::path &path, bool core)
{
	for (const auto &loaded : modules()) {
		const auto name = basename(loaded);
		const bool ort_core = name == "onnxruntime.dll" || name.starts_with("libonnxruntime.so");
		if (name != basename(path) && !(core && ort_core))
			continue;
		std::error_code ec;
		if (!fs::equivalent(path, loaded, ec) || ec)
			throw std::runtime_error("Runtime library collision: another plugin loaded a different " +
						 basename(path) + "; restart OBS with one runtime bundle enabled");
	}
}
void *load_library(const fs::path &path, bool core = false)
{
	if (!path.is_absolute())
		throw std::runtime_error("Runtime library requires a full private path");
	collision_check(path, core);
#ifdef _WIN32
	const auto handle =
		LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!handle)
		throw std::runtime_error("Cannot load " + basename(path) + "; Windows loader error " +
					 std::to_string(GetLastError()));
#else
	const auto handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!handle)
		throw std::runtime_error("Cannot load " + basename(path) + "; missing or incompatible dependency");
#endif
	// Pin modules for process lifetime: EP workers may outlive a session destructor.
	return reinterpret_cast<void *>(handle);
}
} // namespace
std::recursive_mutex &runtime_control_mutex()
{
	return loader_mutex;
}
std::shared_ptr<Runtime> Runtime::load(const Bundle &bundle)
{
	std::lock_guard guard(loader_mutex);
	verify_bundle(bundle);
	// Check core before loading any dependent EP libraries.
	collision_check(bundle.runtime, true);
	for (const auto &path : bundle.preload)
		collision_check(path, false);
	if (!bundle.provider.empty())
		collision_check(bundle.provider, false);
	static auto *runtimes = new std::map<fs::path, std::shared_ptr<Runtime>>;
	auto found = runtimes->find(bundle.runtime);
	if (found != runtimes->end()) {
		found->second->register_provider(bundle);
		return found->second;
	}
	auto runtime = std::shared_ptr<Runtime>(new Runtime);
	runtime->module_ = load_library(bundle.runtime, true);
	auto entry = reinterpret_cast<const OrtApiBase *(ORT_API_CALL *)()>(runtime->symbol("OrtGetApiBase"));
	if (!entry)
		throw std::runtime_error("ORT required symbol OrtGetApiBase is missing");
	const auto *base = entry();
	if (!base || !base->GetApi || !base->GetVersionString)
		throw std::runtime_error("ORT API base is invalid");
	for (std::uint32_t version = ORT_API_VERSION; version >= 17; --version) {
		if (auto api = base->GetApi(version)) {
			runtime->api_ = api;
			runtime->version_ = version;
			break;
		}
	}
	if (!runtime->api_)
		throw std::runtime_error("ORT lacks the required C API 17 baseline");
	runtime->build_ = base->GetVersionString();
	runtime->check(runtime->api().CreateEnv(ORT_LOGGING_LEVEL_WARNING, "CorridorKey", &runtime->env_));
	runtime->register_provider(bundle);
	runtimes->emplace(bundle.runtime, runtime);
	return runtime;
}
Runtime::~Runtime()
{
	if (env_)
		api_->ReleaseEnv(env_);
}
void *Runtime::symbol(const char *name) const
{
#ifdef _WIN32
	return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(module_), name));
#else
	return dlsym(module_, name);
#endif
}
void Runtime::check(OrtStatus *status) const
{
	if (!status)
		return;
	std::string message = api_->GetErrorMessage(status);
	api_->ReleaseStatus(status);
	throw std::runtime_error(message);
}
void Runtime::register_provider(const Bundle &bundle)
{
	std::lock_guard guard(loader_mutex);
	for (const auto &p : bundle.preload)
		load_library(p);
	if (bundle.registration != Registration::plugin)
		return;
	if (version_ < 23)
		throw std::runtime_error("This plugin EP needs C API 23 or newer; try a classic provider bundle");
	const auto existing = registered_.find(bundle.provider_name);
	if (existing != registered_.end()) {
		if (existing->second != bundle.provider)
			throw std::runtime_error("A different EP library is already registered under this name");
		return;
	}
	load_library(bundle.provider);
	check(api_->RegisterExecutionProviderLibrary(env_, bundle.provider_name.c_str(), bundle.provider.c_str()));
	registered_.emplace(bundle.provider_name, bundle.provider);
}
} // namespace corridorkey::ort
