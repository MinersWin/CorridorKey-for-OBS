// SPDX-License-Identifier: GPL-3.0-or-later
#include "process.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace corridorkey::ort {
namespace {
std::string utf8(const fs::path &path)
{
	auto s = path.u8string();
	return {s.begin(), s.end()};
}
#ifdef _WIN32
std::wstring quote(const fs::path &path)
{
	std::wstring out = L"\"";
	size_t slashes = 0;
	for (wchar_t c : path.wstring()) {
		if (c == L'\\') {
			++slashes;
			continue;
		}
		if (c == L'\"') {
			out.append(slashes * 2 + 1, L'\\');
			out += c;
		} else {
			out.append(slashes, L'\\');
			out += c;
		}
		slashes = 0;
	}
	out.append(slashes * 2, L'\\');
	out += L'\"';
	return out;
}
struct Handle {
	HANDLE value = nullptr;
	~Handle()
	{
		if (value && value != INVALID_HANDLE_VALUE)
			CloseHandle(value);
	}
};
#endif
} // namespace
ProcessResult run_helper(const fs::path &helper, const fs::path &request, const fs::path &response,
			 std::chrono::milliseconds timeout, std::stop_token stop)
{
	ProcessResult out;
	if (!helper.is_absolute() || !request.is_absolute() || !response.is_absolute() || timeout.count() <= 0)
		return out;
	if (stop.stop_requested()) {
		out.cancelled = true;
		return out;
	}
	const auto deadline = Clock::now() + timeout;
#ifdef _WIN32
	Handle job{CreateJobObjectW(nullptr, nullptr)};
	if (!job.value)
		return out;
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
		return out;
	auto command = quote(helper) + L" --request " + quote(request) + L" --response " + quote(response);
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	// No inherited pipe handles, environment changes, visible console, or shell.
	if (!CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE,
			    CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, helper.parent_path().c_str(), &startup,
			    &process))
		return out;
	Handle proc{process.hProcess}, thread{process.hThread};
	if (!AssignProcessToJobObject(job.value, proc.value) || ResumeThread(thread.value) == static_cast<DWORD>(-1)) {
		TerminateProcess(proc.value, 1);
		WaitForSingleObject(proc.value, INFINITE);
		return out;
	}
	out.started = true;
	for (;;) {
		const auto wait = WaitForSingleObject(proc.value, 10);
		if (wait == WAIT_OBJECT_0)
			break;
		if (wait == WAIT_FAILED) {
			TerminateJobObject(job.value, 1);
			WaitForSingleObject(proc.value, INFINITE);
			break;
		}
		if (stop.stop_requested() || Clock::now() >= deadline) {
			out.cancelled = stop.stop_requested();
			out.timed_out = !out.cancelled;
			TerminateJobObject(job.value, 1);
			WaitForSingleObject(proc.value, INFINITE);
			break;
		}
	}
	DWORD code = 0;
	GetExitCodeProcess(proc.value, &code);
	out.exit_code = static_cast<int>(code);
	out.crashed = !out.timed_out && !out.cancelled && (code & 0x80000000u) != 0;
#else
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
	posix_spawnattr_setpgroup(&attr, 0);
	std::string h = helper.string(), req = request.string(), resp = response.string();
	char request_flag[] = "--request", response_flag[] = "--response";
	char *argv[] = {h.data(), request_flag, req.data(), response_flag, resp.data(), nullptr};
	pid_t pid = -1;
	const auto result = posix_spawn(&pid, h.c_str(), &actions, &attr, argv, environ);
	posix_spawn_file_actions_destroy(&actions);
	posix_spawnattr_destroy(&attr);
	if (result != 0)
		return out;
	out.started = true;
	int status = 0;
	for (;;) {
		siginfo_t info{};
		const auto waited = waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT);
		if (waited == 0 && info.si_pid == pid) {
			// Keep the unreaped child PID reserved while terminating its descendants.
			kill(-pid, SIGKILL);
			while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
			}
			break;
		}
		if (waited == -1 && errno != EINTR) {
			if (errno != ECHILD) {
				kill(-pid, SIGKILL);
				while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
				}
			}
			out.crashed = true;
			return out;
		}
		if (stop.stop_requested() || Clock::now() >= deadline) {
			out.cancelled = stop.stop_requested();
			out.timed_out = !out.cancelled;
			kill(-pid, SIGKILL);
			while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
			}
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	out.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	out.crashed = !out.cancelled && !out.timed_out && WIFSIGNALED(status);
#endif
	return out;
}
ProbeResult DisposableProbe::probe(const ProbeRequest &request, std::stop_token stop)
{
	ProbeResult result;
	result.provider.backend = request.backend;
	auto fail = [&](ErrorCode code, std::string detail) {
		result.failure = error(code);
		result.provider.unavailable_reason = result.failure;
		result.diagnostic_detail = std::move(detail);
		return result;
	};
	try {
		if (stop.stop_requested())
			return fail(ErrorCode::cancelled, "Backend test cancelled");
		const auto bundle = config_.bundles.find(request.backend);
		if (bundle == config_.bundles.end())
			return fail(ErrorCode::backend_unavailable, "No installed bundle for this provider");
		if (!request.package_id.empty() && read_bundle(bundle->second).id != request.package_id)
			return fail(ErrorCode::backend_unavailable,
				    "Requested runtime bundle differs from the installed probe configuration");
		auto fixture = config_.fixture_fp32;
		if (request.precision == Precision::fp16)
			fixture = config_.fixture_fp16;
		if (request.precision == Precision::bf16)
			fixture = config_.fixture_bf16;
		if (fixture.empty())
			return fail(ErrorCode::unsupported, "No standard-op fixture for requested precision");
		fs::create_directories(config_.scratch);
		std::random_device random;
		fs::path directory;
		for (int attempt = 0; attempt < 8; ++attempt) {
			directory = fs::absolute(config_.scratch) /
				    ("probe-" + std::to_string(random()) + "-" + std::to_string(random()));
			if (fs::create_directory(directory))
				break;
			directory.clear();
		}
		if (directory.empty())
			return fail(ErrorCode::io_failure, "Cannot allocate backend test directory");
#ifndef _WIN32
		chmod(directory.c_str(), 0700);
#endif
		struct Cleanup {
			fs::path p;
			~Cleanup()
			{
				std::error_code ec;
				fs::remove_all(p, ec);
			}
		} cleanup{directory};
		const auto req = directory / "request.json", response = directory / "response.json";
		const int precision = static_cast<int>(request.precision == Precision::automatic ? Precision::fp32
												 : request.precision);
		nlohmann::json job{{"schema", 1},
				   {"mode", "probe"},
				   {"bundle", utf8(fs::absolute(bundle->second))},
				   {"graph", utf8(fs::absolute(fixture))},
				   {"precision", precision},
				   {"adapter", request.obs_adapter.stable_id},
				   {"require_adapter_match", !request.obs_adapter.stable_id.empty()},
				   {"iterations", 5},
				   {"profile", utf8(directory / "profile")}};
		{
			std::ofstream out(req);
			out << job.dump();
			if (!out)
				return fail(ErrorCode::io_failure, "Cannot write backend test request");
		}
		const auto process = run_helper(fs::absolute(config_.helper), req, response, request.timeout, stop);
		result.timed_out = process.timed_out;
		result.process_crashed = process.crashed;
		if (process.cancelled)
			return fail(ErrorCode::cancelled, "Backend test cancelled");
		if (process.timed_out)
			return fail(ErrorCode::timeout, "Backend helper exceeded its deadline and was terminated");
		if (process.crashed)
			return fail(ErrorCode::backend_unavailable, "Backend helper crashed; OBS remains running");
		if (!process.started)
			return fail(ErrorCode::backend_unavailable, "Backend helper could not start");
		if (!fs::exists(response) || fs::file_size(response) > 1024 * 1024)
			return fail(ErrorCode::backend_unavailable, "Backend helper returned no valid result");
		std::ifstream input(response);
		const auto j = nlohmann::json::parse(input);
		if (j.at("schema") != 1 || j.at("backend") != backend_name(request.backend))
			return fail(ErrorCode::backend_unavailable, "Backend helper result does not match the request");
		if (process.exit_code != 0 || !j.at("ok").get<bool>())
			return fail(ErrorCode::backend_unavailable, j.value("detail", "Backend test failed"));
		auto &p = result.provider;
		p.adapter.stable_id = j.at("adapter");
		p.adapter.display_name = j.at("device");
		p.adapter.vendor_id = j.at("vendor");
		p.matches_obs_adapter = adapter_matches(p.adapter, request.obs_adapter);
		p.runtime_build = j.at("runtime");
		p.provider_build = j.at("provider_build");
		p.available = true;
		p.tested = true;
		p.io_binding = true;
		p.cooperative_cancel = true;
		p.permits_live_inference = request.backend != Backend::cpu_diagnostic &&
					   request.precision != Precision::bf16 &&
					   j.at("gpu_partition_verified").get<bool>();
		p.precisions = {static_cast<Precision>(precision)};
		p.tensor_domains = {MemoryDomain::host}; // Native device tensors belong to transport validation.
		result.negotiated_api = j.at("api");
		result.diagnostic_detail = j.dump();
		return result;
	} catch (...) {
		return fail(ErrorCode::backend_unavailable, "Backend descriptor or helper response is invalid");
	}
}
} // namespace corridorkey::ort
