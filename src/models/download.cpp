// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "download.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <thread>
#include <exception>
#include <zlib.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif
namespace corridorkey::models {
std::string HttpResponse::header(std::string_view name) const
{
	auto it = headers.find(std::string(name));
	return it == headers.end() ? "" : it->second;
}
static void cancelled(std::stop_token s)
{
	require(!s.stop_requested(), "Cancelled", ErrorCode::cancelled);
}
static std::string trim(std::string s)
{
	auto a = s.find_first_not_of(" \r\n\t"), b = s.find_last_not_of(" \r\n\t");
	return a == s.npos ? "" : s.substr(a, b - a + 1);
}
static void network_url(std::string_view url, bool test)
{
	require(url.size() <= 16384 && url.find_first_of("\\\r\n\t #") == url.npos, "Invalid network URL");
	bool loopback = test && url.starts_with("http://127.0.0.1:");
	require(loopback || url.starts_with("https://"), "HTTPS required");
	auto host_start = loopback ? 7U : 8U;
	auto slash = url.find('/', host_start);
	auto host = url.substr(host_start, slash == url.npos ? url.size() - host_start : slash - host_start);
	require(!host.empty() && host.find_first_of("@%?#") == host.npos, "Invalid network host");
}
#ifdef _WIN32
struct InternetHandle {
	HINTERNET h = nullptr;
	explicit InternetHandle(HINTERNET p) : h(p)
	{
		require(h != nullptr, "WinHTTP handle failed", ErrorCode::io_failure);
	}
	~InternetHandle() { WinHttpCloseHandle(h); }
	InternetHandle(const InternetHandle &) = delete;
};
static std::wstring wide(std::string_view s)
{
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
	require(n > 0, "Invalid UTF8 URL");
	std::wstring out(static_cast<std::size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
	return out;
}
static std::string narrow(std::wstring_view s)
{
	int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<std::size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
	return out;
}
class SystemHttp final : public HttpClient {
	bool test_;

public:
	explicit SystemHttp(bool test) : test_(test) {}
	void get(const HttpRequest &r, const HeaderSink &headers, const ByteSink &body, std::stop_token stop) override
	{
		network_url(r.url, test_);
		cancelled(stop);
		auto url = wide(r.url);
		URL_COMPONENTS parts{};
		parts.dwStructSize = sizeof(parts);
		parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
		require(WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts), "Invalid HTTP URL");
		auto host = std::wstring(parts.lpszHostName, parts.dwHostNameLength);
		auto path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
			    std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
		InternetHandle session(WinHttpOpen(
			L"CorridorKey/1", test_ ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
		WinHttpSetTimeouts(session.h, 5000, 10000, 10000, 5000);
		InternetHandle connection(WinHttpConnect(session.h, host.c_str(), parts.nPort, 0));
		InternetHandle request(
			WinHttpOpenRequest(connection.h, L"GET", path.empty() ? L"/" : path.c_str(), nullptr,
					   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
					   parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
		DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
		require(WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)),
			"WinHTTP anonymous policy failed", ErrorCode::io_failure);
		std::string extra = "Accept-Encoding: identity\r\n";
		if (r.offset)
			extra += "Range: bytes=" + std::to_string(r.offset) + "-\r\nIf-Range: " + r.validator + "\r\n";
		auto wh = wide(extra);
		require(WinHttpSendRequest(request.h, wh.c_str(), static_cast<DWORD>(wh.size()),
					   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
				WinHttpReceiveResponse(request.h, nullptr),
			"HTTP request failed", ErrorCode::io_failure);
		HttpResponse response;
		DWORD status = 0, size = sizeof(status);
		require(WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
					    &status, &size, nullptr),
			"Missing HTTP status", ErrorCode::io_failure);
		response.status = status;
		for (auto key : {"location", "etag", "last-modified", "content-range", "retry-after", "content-length",
				 "content-encoding"}) {
			auto wk = wide(key);
			DWORD count = 0;
			WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CUSTOM, wk.c_str(), nullptr, &count, nullptr);
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
				continue;
			require(count <= 32768 && count % sizeof(wchar_t) == 0, "Oversized HTTP header",
				ErrorCode::io_failure);
			std::wstring value(count / sizeof(wchar_t), L'\0');
			require(WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CUSTOM, wk.c_str(), value.data(), &count,
						    nullptr),
				"HTTP header read failed", ErrorCode::io_failure);
			while (!value.empty() && value.back() == L'\0')
				value.pop_back();
			response.headers[key] = trim(narrow(value));
		}
		if (!headers(response))
			return;
		std::array<char, 65536> data{};
		for (;;) {
			cancelled(stop);
			DWORD count = 0;
			require(WinHttpReadData(request.h, data.data(), static_cast<DWORD>(data.size()), &count),
				"HTTP stream interrupted", ErrorCode::io_failure);
			if (!count)
				break;
			body(std::span<const char>(data.data(), count));
		}
		cancelled(stop);
	}
};
#else
class SystemHttp final : public HttpClient {
	bool test_;

public:
	explicit SystemHttp(bool test) : test_(test)
	{
		static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
		require(initialized == CURLE_OK, "curl initialization failed", ErrorCode::io_failure);
	}
	void get(const HttpRequest &r, const HeaderSink &headers, const ByteSink &body, std::stop_token stop) override
	{
		network_url(r.url, test_);
		cancelled(stop);
		std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(), curl_easy_cleanup);
		require(bool(easy), "curl allocation failed", ErrorCode::out_of_memory);
		struct State {
			const HeaderSink &head;
			const ByteSink &body;
			std::stop_token stop;
			HttpResponse response;
			std::exception_ptr failure;
			bool skip = false;
			std::size_t header_bytes = 0;
			bool delivered = false;
		} s{headers, body, stop, {}, {}, false, 0, false};
		curl_easy_setopt(easy.get(), CURLOPT_URL, r.url.c_str());
		curl_easy_setopt(easy.get(), CURLOPT_PROTOCOLS_STR, test_ ? "http,https" : "https");
		curl_easy_setopt(easy.get(), CURLOPT_FOLLOWLOCATION, 0L);
		curl_easy_setopt(easy.get(), CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
		curl_easy_setopt(easy.get(), CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(easy.get(), CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(easy.get(), CURLOPT_NETRC, CURL_NETRC_IGNORED);
		curl_easy_setopt(easy.get(), CURLOPT_HTTPAUTH, CURLAUTH_NONE);
		curl_easy_setopt(easy.get(), CURLOPT_PROXYAUTH, CURLAUTH_NONE);
		curl_easy_setopt(easy.get(), CURLOPT_CONNECTTIMEOUT, 10L);
		curl_easy_setopt(easy.get(), CURLOPT_TIMEOUT, 3600L);
		curl_easy_setopt(easy.get(), CURLOPT_LOW_SPEED_LIMIT, 1L);
		curl_easy_setopt(easy.get(), CURLOPT_LOW_SPEED_TIME, 15L);
		curl_easy_setopt(easy.get(), CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(easy.get(), CURLOPT_USERAGENT, "CorridorKey/1");
		if (test_)
			curl_easy_setopt(easy.get(), CURLOPT_PROXY, "");
		curl_slist *raw = curl_slist_append(nullptr, "Accept-Encoding: identity");
		if (r.offset) {
			raw = curl_slist_append(raw, ("Range: bytes=" + std::to_string(r.offset) + "-").c_str());
			raw = curl_slist_append(raw, ("If-Range: " + r.validator).c_str());
		}
		std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> list(raw, curl_slist_free_all);
		curl_easy_setopt(easy.get(), CURLOPT_HTTPHEADER, raw);
		curl_easy_setopt(easy.get(), CURLOPT_HEADERDATA, &s);
		curl_easy_setopt(
			easy.get(), CURLOPT_HEADERFUNCTION,
			+[](char *p, size_t size, size_t count, void *ctx) -> size_t {
				auto &st = *static_cast<State *>(ctx);
				auto n = size * count;
				try {
					st.header_bytes += n;
					require(st.header_bytes <= 65536, "HTTP headers oversized",
						ErrorCode::io_failure);
					std::string line(p, n);
					if (line.starts_with("HTTP/")) {
						st.response = {};
						st.delivered = false;
						auto space = line.find(' ');
						require(space != line.npos, "Invalid status", ErrorCode::io_failure);
						st.response.status =
							static_cast<unsigned>(std::stoul(line.substr(space + 1)));
					} else if (line == "\r\n") {
						if (st.response.status >= 200 && !st.delivered) {
							st.skip = !st.head(st.response);
							st.delivered = true;
						}
					} else if (auto colon = line.find(':'); colon != line.npos) {
						auto key = line.substr(0, colon);
						std::transform(key.begin(), key.end(), key.begin(),
							       [](unsigned char c) { return char(std::tolower(c)); });
						st.response.headers[key] = trim(line.substr(colon + 1));
					}
					return n;
				} catch (...) {
					st.failure = std::current_exception();
					return 0;
				}
			});
		curl_easy_setopt(easy.get(), CURLOPT_WRITEDATA, &s);
		curl_easy_setopt(
			easy.get(), CURLOPT_WRITEFUNCTION, +[](char *p, size_t a, size_t b, void *ctx) -> size_t {
				auto &st = *static_cast<State *>(ctx);
				try {
					cancelled(st.stop);
					if (st.skip)
						return 0;
					st.body(std::span<const char>(p, a * b));
					return a * b;
				} catch (...) {
					st.failure = std::current_exception();
					return 0;
				}
			});
		curl_easy_setopt(easy.get(), CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(easy.get(), CURLOPT_XFERINFODATA, &s);
		curl_easy_setopt(
			easy.get(), CURLOPT_XFERINFOFUNCTION,
			+[](void *ctx, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
				return static_cast<State *>(ctx)->stop.stop_requested() ? 1 : 0;
			});
		auto result = curl_easy_perform(easy.get());
		if (s.failure)
			std::rethrow_exception(s.failure);
		cancelled(stop);
		require(result == CURLE_OK || (s.skip && result == CURLE_WRITE_ERROR), "HTTP stream interrupted",
			ErrorCode::io_failure);
	}
};
#endif
std::shared_ptr<HttpClient> system_http_client(bool test)
{
	return std::make_shared<SystemHttp>(test);
}
static std::uint64_t decimal(std::string_view s)
{
	std::uint64_t n = 0;
	auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
	require(ec == std::errc{} && end == s.data() + s.size(), "Invalid HTTP integer", ErrorCode::io_failure);
	return n;
}
static bool wait_default(std::chrono::milliseconds duration, std::stop_token stop)
{
	auto end = Clock::now() + duration;
	while (Clock::now() < end) {
		if (stop.stop_requested())
			return false;
		std::this_thread::sleep_for(
			std::min(std::chrono::milliseconds(25),
				 std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now())));
	}
	return !stop.stop_requested();
}
DownloadEngine::DownloadEngine(Cache &c, std::shared_ptr<HttpClient> http, DownloadOptions options)
	: cache_(c),
	  http_(std::move(http)),
	  options_(std::move(options))
{
	require(http_ && options_.attempts > 0 && options_.attempts <= 10, "Invalid download configuration");
	if (!options_.wait)
		options_.wait = wait_default;
	if (!options_.check_space)
		options_.check_space = [this](std::uint64_t bytes) {
			cache_.check_space(bytes);
		};
}
static std::string validator(const HttpResponse &r)
{
	auto v = r.header("etag");
	if (v.starts_with("W/"))
		v.clear();
	if (v.empty())
		v = r.header("last-modified");
	require(v.size() <= 4096 && v.find_first_of("\r\n") == v.npos, "Invalid HTTP validator", ErrorCode::io_failure);
	return v;
}
void DownloadEngine::fetch(const Manifest &m, const Artifact &a, const LicenceRegistry &licenses,
			   const AcceptanceStore &acceptance, std::stop_token stop,
			   std::function<void(Progress)> progress)
{
	require(acceptance.contains(m, licenses), "Accept model licences before downloading",
		ErrorCode::licence_required);
	require(m.artifacts.contains(a.id) && m.artifacts.at(a.id).sha256 == a.sha256 &&
			m.artifacts.at(a.id).url == a.url && m.artifacts.at(a.id).bytes == a.bytes,
		"Artifact is not in package");
	https_url(a.url);
	cancelled(stop);
	FileLock global(cache_.path("locks/data.lock"), true, true, stop);
	FileLock lock(cache_.path("locks/blob-" + a.sha256 + ".lock"), false, true, stop);
	if (cache_.has_blob(a, stop)) {
		if (progress)
			progress({a.bytes, a.bytes, true});
		return;
	}
	FileLock transfer_slot;
	while (!transfer_slot.owns_lock()) {
		for (auto name : {"locks/network-0.lock", "locks/network-1.lock"}) {
			transfer_slot = FileLock(cache_.path(name), false, false, stop);
			if (transfer_slot.owns_lock())
				break;
		}
		if (!transfer_slot.owns_lock())
			require(options_.wait(std::chrono::milliseconds(25), stop),
				"Cancelled waiting for transfer slot", ErrorCode::cancelled);
	}
	auto partial = cache_.path("partial/" + a.sha256 + ".partial"),
	     sidecar = cache_.path("partial/" + a.sha256 + ".json");
	std::string saved;
	auto reset = [&] {
		std::error_code ec;
		fs::remove(partial, ec);
		fs::remove(sidecar, ec);
		saved.clear();
	};
	if (fs::exists(partial)) {
		try {
			auto s = strict_json(read_small_file(sidecar, 16384));
			fields(s, {"schema_version", "url_sha256", "sha256", "bytes", "validator"});
			require(uint64(s.at("schema_version")) == 1 && string(s.at("url_sha256")) == sha256(a.url) &&
					string(s.at("sha256")) == a.sha256 && uint64(s.at("bytes")) == a.bytes,
				"Stale partial identity");
			saved = string(s.at("validator"));
			require(saved.find_first_of("\r\n") == saved.npos && saved.size() <= 4096,
				"Invalid saved validator");
			require(fs::file_size(partial) <= a.bytes, "Oversized partial");
		} catch (const Failure &) {
			reset();
		}
	}
	if (fs::exists(partial) && fs::file_size(partial) == a.bytes) {
		if (file_sha256(partial, stop) == a.sha256) {
			atomic_finalize(partial, cache_.blob(a.sha256));
			fs::remove(sidecar);
			return;
		}
		reset();
	}
	for (unsigned attempt = 0; attempt < options_.attempts; ++attempt) {
		cancelled(stop);
		auto offset = fs::exists(partial) && !saved.empty() ? fs::file_size(partial) : 0;
		if (!offset && fs::exists(partial))
			reset();
		options_.check_space(a.bytes - offset);
		std::string url = a.url;
		unsigned redirects = 0;
		bool retry = false, restart = false, accepted_body = false;
		std::uint64_t received = offset, expected_end = a.bytes;
		auto delay = std::chrono::milliseconds(std::min(30000U, 500U << attempt));
		try {
			for (;;) {
				std::ofstream output;
				HttpResponse response;
				http_->get(
					{url, offset, saved},
					[&](const HttpResponse &r) {
						response = r;
						if (r.status >= 300 && r.status < 400)
							return false;
						if (r.status == 429 || r.status == 408 ||
						    (r.status >= 500 && r.status <= 599)) {
							retry = true;
							auto after = r.header("retry-after");
							if (!after.empty() &&
							    after.find_first_not_of("0123456789") == after.npos)
								delay = std::chrono::milliseconds(
									std::min<std::uint64_t>(60, decimal(after)) *
									1000);
							return false;
						}
						if (r.status == 416 && offset) {
							restart = true;
							return false;
						}
						require(r.status == 200 || r.status == 206, "HTTP download rejected",
							ErrorCode::unsupported);
						require(r.header("content-encoding").empty() ||
								r.header("content-encoding") == "identity",
							"Encoded response not supported", ErrorCode::io_failure);
						auto current = validator(r);
						if (r.status == 206) {
							auto range = r.header("content-range");
							require(range.starts_with("bytes "), "Missing Content-Range",
								ErrorCode::io_failure);
							auto dash = range.find('-', 6), slash = range.find('/', 6);
							require(dash != range.npos && slash != range.npos &&
									dash < slash,
								"Invalid Content-Range", ErrorCode::io_failure);
							auto begin = decimal(
								     std::string_view(range).substr(6, dash - 6)),
							     end = decimal(std::string_view(range).substr(
								     dash + 1, slash - dash - 1));
							require(begin == offset &&
									decimal(std::string_view(range).substr(
										slash + 1)) == a.bytes &&
									end >= begin && end < a.bytes,
								"Wrong Content-Range", ErrorCode::io_failure);
							expected_end = end + 1;
							if (offset && (current.empty() || current != saved)) {
								restart = true;
								return false;
							}
						} else {
							offset = received = 0;
							expected_end = a.bytes;
						}
						if (auto length = r.header("content-length"); !length.empty())
							require(decimal(length) == expected_end - offset,
								"Unexpected Content-Length", ErrorCode::io_failure);
						options_.check_space(a.bytes - offset);
						if (current.empty()) {
							std::error_code ec;
							fs::remove(sidecar, ec);
						} else
							atomic_write(sidecar, Json{{"schema_version", 1},
										   {"url_sha256", sha256(a.url)},
										   {"sha256", a.sha256},
										   {"bytes", a.bytes},
										   {"validator", current}}
										      .dump());
						saved = current;
						output.open(partial, std::ios::binary | (offset ? std::ios::app
												: std::ios::trunc));
						require(bool(output), "Cannot write partial download",
							ErrorCode::io_failure);
						accepted_body = true;
						return true;
					},
					[&](std::span<const char> data) {
						cancelled(stop);
						require(data.size() <= expected_end - received,
							"Response exceeds declared bytes", ErrorCode::io_failure);
						options_.check_space(data.size());
						output.write(data.data(), static_cast<std::streamsize>(data.size()));
						require(bool(output), "Disk write failed", ErrorCode::disk_full);
						received += data.size();
						if (progress)
							progress({received, a.bytes, true});
					},
					stop);
				if (output.is_open()) {
					output.flush();
					require(bool(output), "Disk flush failed", ErrorCode::disk_full);
					output.close();
				}
				if (response.status >= 300 && response.status < 400) {
					require(++redirects <= 8, "Too many redirects", ErrorCode::io_failure);
					auto location = response.header("location");
					require(!location.empty(), "Redirect missing location", ErrorCode::io_failure);
					if (location.front() == '/' && !location.starts_with("//")) {
						auto end = url.find('/', 8);
						location = url.substr(0, end) + location;
					}
					network_url(location, false);
					url = location;
					continue;
				}
				break;
			}
			if (restart)
				reset();
			if (!retry && !restart && accepted_body) {
				require(received == expected_end, "Truncated HTTP body", ErrorCode::io_failure);
				if (received != a.bytes)
					retry = true;
				else {
					if (file_sha256(partial, stop) != a.sha256) {
						reset();
						throw Failure(ErrorCode::hash_mismatch, "Downloaded SHA256 mismatch");
					}
					cancelled(stop);
					atomic_finalize(partial, cache_.blob(a.sha256));
					std::error_code ec;
					fs::remove(sidecar, ec);
					return;
				}
			}
		} catch (const Failure &f) {
			if (f.code != ErrorCode::io_failure)
				throw;
			retry = true;
		}
		if (attempt + 1 == options_.attempts)
			throw Failure(ErrorCode::io_failure, "Download retry limit reached; partial retained");
		require(options_.wait(restart ? std::chrono::milliseconds(0) : delay, stop), "Cancelled during retry",
			ErrorCode::cancelled);
	}
}
namespace {
struct ZipEntry {
	std::uint64_t offset = 0, compressed = 0, bytes = 0;
	unsigned method = 0, flags = 0;
	std::string name;
};
class OfflineZip {
	std::ifstream in;
	std::uint64_t size;
	std::map<std::string, ZipEntry> entries;
	std::string read(std::uint64_t off, std::size_t n)
	{
		require(off <= size && n <= size - off, "ZIP span outside archive");
		std::string out(n, '\0');
		in.clear();
		in.seekg(static_cast<std::streamoff>(off));
		in.read(out.data(), static_cast<std::streamsize>(n));
		require(bool(in), "ZIP read failed", ErrorCode::io_failure);
		return out;
	}
	static std::uint64_t le(std::string_view s, std::size_t off, std::size_t n)
	{
		require(off <= s.size() && n <= s.size() - off, "Truncated ZIP header");
		std::uint64_t value = 0;
		for (std::size_t i = 0; i < n; ++i)
			value |= std::uint64_t(static_cast<unsigned char>(s[off + i])) << (i * 8);
		return value;
	}

public:
	OfflineZip(const fs::path &p, const Manifest &m) : in(p, std::ios::binary), size(fs::file_size(p))
	{
		require(bool(in) && size >= 22, "Invalid ZIP");
		auto tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(size, 65557));
		auto tail = read(size - tail_size, tail_size);
		std::size_t pos = tail.size() - 22;
		for (;;) {
			if (le(tail, pos, 4) == 0x06054b50 && pos + 22 + le(tail, pos + 20, 2) == tail.size())
				break;
			require(pos > 0, "Missing ZIP directory");
			--pos;
		}
		require(le(tail, pos + 4, 2) == 0 && le(tail, pos + 6, 2) == 0 &&
				le(tail, pos + 8, 2) == le(tail, pos + 10, 2),
			"Split ZIP not supported");
		auto count = le(tail, pos + 10, 2), length = le(tail, pos + 12, 4), offset = le(tail, pos + 16, 4);
		if (count == 65535 || length == 0xffffffff || offset == 0xffffffff) {
			auto eocd = size - tail_size + pos;
			require(eocd >= 20, "Missing ZIP64 locator");
			auto locator = read(eocd - 20, 20);
			require(le(locator, 0, 4) == 0x07064b50 && le(locator, 4, 4) == 0 && le(locator, 16, 4) == 1,
				"Invalid ZIP64 locator");
			auto header = read(le(locator, 8, 8), 56);
			require(le(header, 0, 4) == 0x06064b50 && le(header, 16, 4) == 0 && le(header, 20, 4) == 0 &&
					le(header, 24, 8) == le(header, 32, 8),
				"Invalid ZIP64 directory");
			count = le(header, 32, 8);
			length = le(header, 40, 8);
			offset = le(header, 48, 8);
		}
		require(count <= 4096 && length <= 4 * 1024 * 1024, "ZIP directory limits exceeded");
		auto directory = read(offset, static_cast<std::size_t>(length));
		std::size_t cursor = 0;
		std::set<std::string> allowed;
		for (auto &[id, a] : m.artifacts)
			allowed.insert("blobs/" + a.sha256);
		for (std::uint64_t i = 0; i < count; ++i) {
			require(le(directory, cursor, 4) == 0x02014b50, "Invalid ZIP directory entry");
			auto name_length = le(directory, cursor + 28, 2), extra_length = le(directory, cursor + 30, 2),
			     comment_length = le(directory, cursor + 32, 2);
			auto extent =
				checked_add(46, checked_add(name_length, checked_add(extra_length, comment_length)));
			require(extent <= directory.size() - cursor, "ZIP entry outside directory");
			ZipEntry e;
			e.name = directory.substr(cursor + 46, static_cast<std::size_t>(name_length));
			e.flags = static_cast<unsigned>(le(directory, cursor + 8, 2));
			e.method = static_cast<unsigned>(le(directory, cursor + 10, 2));
			e.compressed = le(directory, cursor + 20, 4);
			e.bytes = le(directory, cursor + 24, 4);
			e.offset = le(directory, cursor + 42, 4);
			require(!(e.flags & 1) && (e.method == 0 || e.method == 8) &&
					le(directory, cursor + 34, 2) == 0,
				"Unsupported/encrypted ZIP entry");
			auto mode = le(directory, cursor + 38, 4) >> 16;
			require((mode & 0170000) == 0 || (mode & 0170000) == 0100000 || (mode & 0170000) == 0040000,
				"ZIP links/devices not permitted");
			if (e.bytes == 0xffffffff || e.compressed == 0xffffffff || e.offset == 0xffffffff) {
				auto extra = std::string_view(directory).substr(
					cursor + 46 + static_cast<std::size_t>(name_length),
					static_cast<std::size_t>(extra_length));
				bool found = false;
				for (std::size_t k = 0; k < extra.size();) {
					auto tag = le(extra, k, 2), n = le(extra, k + 2, 2);
					require(n <= extra.size() - k - 4, "Invalid ZIP extra field");
					if (tag == 1) {
						auto z = extra.substr(k + 4, static_cast<std::size_t>(n));
						std::size_t at = 0;
						if (e.bytes == 0xffffffff) {
							e.bytes = le(z, at, 8);
							at += 8;
						}
						if (e.compressed == 0xffffffff) {
							e.compressed = le(z, at, 8);
							at += 8;
						}
						if (e.offset == 0xffffffff)
							e.offset = le(z, at, 8);
						found = true;
					}
					k += static_cast<std::size_t>(4 + n);
				}
				require(found, "Missing ZIP64 sizes");
			}
			if (e.name == "blobs/")
				require(e.bytes == 0, "Invalid archive directory");
			else {
				relative_path(e.name);
				require(allowed.contains(e.name), "Unexpected ZIP path");
				require(entries.emplace(e.name, e).second, "Duplicate ZIP path");
			}
			cursor += static_cast<std::size_t>(extent);
		}
		require(cursor == directory.size() && entries.size() == allowed.size(), "Incomplete ZIP bundle");
	}
	void extract(const Artifact &a, const ByteSink &sink, std::stop_token stop)
	{
		auto &e = entries.at("blobs/" + a.sha256);
		require(e.bytes == a.bytes, "ZIP entry size mismatch");
		auto local = read(e.offset, 30);
		require(le(local, 0, 4) == 0x04034b50 && le(local, 8, 2) == e.method && le(local, 6, 2) == e.flags,
			"ZIP local header mismatch");
		auto nl = le(local, 26, 2), xl = le(local, 28, 2);
		require(read(e.offset + 30, static_cast<std::size_t>(nl)) == e.name, "ZIP local filename mismatch");
		auto start = checked_add(e.offset, checked_add(30, checked_add(nl, xl)));
		require(start <= size && e.compressed <= size - start, "ZIP data outside archive");
		std::uint64_t consumed = 0, emitted = 0;
		auto emit = [&](std::span<const char> bytes) {
			require(bytes.size() <= a.bytes - emitted, "ZIP expansion limit exceeded");
			sink(bytes);
			emitted += bytes.size();
		};
		if (e.method == 0) {
			require(e.compressed == e.bytes, "Invalid stored ZIP size");
			while (consumed < e.compressed) {
				cancelled(stop);
				auto n = static_cast<std::size_t>(
					std::min<std::uint64_t>(65536, e.compressed - consumed));
				auto bytes = read(start + consumed, n);
				emit(bytes);
				consumed += n;
			}
		} else {
			z_stream z{};
			require(inflateInit2(&z, -MAX_WBITS) == Z_OK, "ZIP inflater failed", ErrorCode::internal);
			struct End {
				z_stream *z;
				~End() { inflateEnd(z); }
			} end{&z};
			std::array<char, 65536> output{};
			std::string input;
			int result = Z_OK;
			while (result != Z_STREAM_END) {
				cancelled(stop);
				if (z.avail_in == 0 && consumed < e.compressed) {
					auto n = static_cast<std::size_t>(
						std::min<std::uint64_t>(65536, e.compressed - consumed));
					input = read(start + consumed, n);
					consumed += n;
					z.next_in = reinterpret_cast<Bytef *>(input.data());
					z.avail_in = static_cast<uInt>(n);
				}
				z.next_out = reinterpret_cast<Bytef *>(output.data());
				z.avail_out = static_cast<uInt>(output.size());
				auto before = z.avail_in;
				result = inflate(&z, Z_NO_FLUSH);
				require(result == Z_OK || result == Z_STREAM_END, "Invalid deflate stream");
				auto n = output.size() - z.avail_out;
				emit(std::span<const char>(output.data(), n));
				require(result == Z_STREAM_END || n || z.avail_in < before, "Truncated deflate stream");
			}
			require(consumed == e.compressed && z.avail_in == 0, "Trailing ZIP compressed bytes");
		}
		require(emitted == a.bytes, "ZIP output size mismatch");
	}
};
} // namespace
void DownloadEngine::import_offline(const Manifest &m, const fs::path &source, const LicenceRegistry &licenses,
				    const AcceptanceStore &acceptance, std::stop_token stop)
{
	require(acceptance.contains(m, licenses), "Accept licences before offline import", ErrorCode::licence_required);
	cancelled(stop);
	FileLock global(cache_.path("locks/data.lock"), true, true, stop);
	options_.check_space(m.peak_additional_bytes());
	std::unique_ptr<OfflineZip> zip;
	fs::path folder;
	if (fs::is_directory(source)) {
		folder = fs::canonical(source);
		require(fs::canonical(folder / "blobs") == folder / "blobs", "Linked offline blobs directory");
	} else
		zip = std::make_unique<OfflineZip>(source, m);
	for (auto &[id, a] : m.artifacts) {
		cancelled(stop);
		FileLock lock(cache_.path("locks/blob-" + a.sha256 + ".lock"), false, true, stop);
		if (cache_.has_blob(a, stop))
			continue;
		auto partial = cache_.path("partial/" + a.sha256 + ".import");
		try {
			std::ofstream out(partial, std::ios::binary | std::ios::trunc);
			require(bool(out), "Cannot open offline destination", ErrorCode::io_failure);
			std::uint64_t written = 0;
			auto sink = [&](std::span<const char> data) {
				cancelled(stop);
				require(data.size() <= a.bytes - written, "Oversized offline file");
				options_.check_space(data.size());
				out.write(data.data(), static_cast<std::streamsize>(data.size()));
				require(bool(out), "Offline disk write failed", ErrorCode::disk_full);
				written += data.size();
			};
			if (zip)
				zip->extract(a, sink, stop);
			else {
				auto p = folder / "blobs" / a.sha256;
				require(fs::canonical(p) == p && fs::is_regular_file(p) && fs::file_size(p) == a.bytes,
					"Missing, linked or wrong-sized offline blob");
				std::ifstream in(p, std::ios::binary);
				std::array<char, 65536> buffer{};
				while (in) {
					in.read(buffer.data(), buffer.size());
					sink(std::span<const char>(buffer.data(),
								   static_cast<std::size_t>(in.gcount())));
				}
				require(in.eof(), "Offline read failed", ErrorCode::io_failure);
			}
			out.flush();
			require(bool(out), "Offline flush failed", ErrorCode::disk_full);
			out.close();
			require(written == a.bytes && file_sha256(partial, stop) == a.sha256, "Offline SHA256 mismatch",
				ErrorCode::hash_mismatch);
			atomic_finalize(partial, cache_.blob(a.sha256));
		} catch (...) {
			std::error_code ec;
			fs::remove(partial, ec);
			throw;
		}
	}
}
} // namespace corridorkey::models
