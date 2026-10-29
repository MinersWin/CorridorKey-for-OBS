// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "runtime.hpp"
#include <onnxruntime_c_api.h>
#include <memory>
#include <mutex>

namespace corridorkey::ort {
std::recursive_mutex &runtime_control_mutex();
class Runtime {
public:
	static std::shared_ptr<Runtime> load(const Bundle &);
	~Runtime();
	const OrtApi &api() const { return *api_; }
	OrtEnv *env() const { return env_; }
	std::uint32_t api_version() const { return version_; }
	const std::string &build() const { return build_; }
	void *symbol(const char *) const;
	void check(OrtStatus *) const;
	void register_provider(const Bundle &);

private:
	Runtime() = default;
	void *module_ = nullptr;
	const OrtApi *api_ = nullptr;
	OrtEnv *env_ = nullptr;
	std::uint32_t version_ = 0;
	std::string build_;
	std::map<std::string, fs::path> registered_;
};
} // namespace corridorkey::ort
