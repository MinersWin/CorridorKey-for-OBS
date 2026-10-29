// SPDX-License-Identifier: GPL-3.0-or-later
#define OrtGetApiBase UnusedOrtGetApiBaseDeclaration
#include <onnxruntime_c_api.h>
#undef OrtGetApiBase
#include <cstdint>
namespace {
OrtStatus *ORT_API_CALL create_env(OrtLoggingLevel, const char *, OrtEnv **out) noexcept
{
	*out = reinterpret_cast<OrtEnv *>(1);
	return nullptr;
}
void ORT_API_CALL release_env(OrtEnv *) {}
const OrtApi *ORT_API_CALL get_api(std::uint32_t version) noexcept
{
	static OrtApi api{};
	api.CreateEnv = create_env;
	api.ReleaseEnv = release_env;
	return version <= 19 ? &api : nullptr;
}
const char *ORT_API_CALL version() noexcept
{
	return "999.0.0";
}
} // namespace
#ifdef _WIN32
#define CK_EXPORT __declspec(dllexport)
#else
#define CK_EXPORT __attribute__((visibility("default")))
#endif
extern "C" CK_EXPORT const OrtApiBase *ORT_API_CALL OrtGetApiBase() noexcept
{
	static OrtApiBase base{get_api, version};
	return &base;
}
