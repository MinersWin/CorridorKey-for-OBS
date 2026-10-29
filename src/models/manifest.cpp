// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "manifest.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <memory>
#include <cstring>
#include <cctype>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#endif

namespace corridorkey::models {
void require(bool ok, std::string_view message, ErrorCode code)
{
	if (!ok)
		throw Failure(code, std::string(message));
}
Json strict_json(std::string_view bytes, std::size_t limit)
{
	require(bytes.size() <= limit, "JSON exceeds size limit");
	std::vector<std::set<std::string>> names;
	try {
		return Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json &value) {
			require(depth <= 32, "JSON exceeds depth limit");
			if (event == Json::parse_event_t::object_start)
				names.emplace_back();
			if (event == Json::parse_event_t::key)
				require(names.back().insert(value.get<std::string>()).second, "Duplicate JSON key");
			if (event == Json::parse_event_t::object_end)
				names.pop_back();
			return true;
		});
	} catch (const Json::exception &) {
		throw Failure(ErrorCode::invalid_input, "Invalid JSON");
	}
}
void fields(const Json &j, std::initializer_list<std::string_view> required,
	    std::initializer_list<std::string_view> optional)
{
	require(j.is_object(), "Expected object");
	if (j.contains("$comment"))
		require(j.at("$comment").is_string() && j.at("$comment").get_ref<const std::string &>().size() <= 4096,
			"Invalid JSON comment");
	for (auto key : required)
		require(j.contains(std::string(key)), "Missing field: " + std::string(key));
	for (auto it = j.begin(); it != j.end(); ++it)
		require(std::find(required.begin(), required.end(), it.key()) != required.end() ||
				std::find(optional.begin(), optional.end(), it.key()) != optional.end(),
			"Unknown field: " + it.key());
}
std::uint64_t uint64(const Json &j)
{
	require(j.is_number_unsigned(), "Expected unsigned 64-bit JSON integer");
	return j.get<std::uint64_t>();
}
std::uint64_t checked_add(std::uint64_t a, std::uint64_t b)
{
	require(b <= std::numeric_limits<std::uint64_t>::max() - a, "Integer overflow");
	return a + b;
}
std::string string(const Json &j, std::size_t max)
{
	require(j.is_string(), "Expected string");
	auto s = j.get<std::string>();
	require(!s.empty() && s.size() <= max && s.find('\0') == s.npos, "Invalid string length or NUL");
	return s;
}
bool hex_string(std::string_view s, std::size_t n)
{
	return s.size() == n && s.find_first_not_of("0123456789abcdef") == s.npos;
}
void identifier(std::string_view s)
{
	require(!s.empty() && s.size() <= 128 && s != "." && s != ".." &&
			s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") ==
				s.npos,
		"Invalid identifier");
}
void relative_path(std::string_view p)
{
	require(!p.empty() && p.size() <= 512 && p.front() != '/' && p.back() != '/', "Invalid relative path");
	std::size_t start = 0;
	while (start < p.size()) {
		auto end = p.find('/', start);
		auto part = p.substr(start, end == p.npos ? p.size() - start : end - start);
		identifier(part);
		require(part.back() != '.' && part.find(':') == part.npos, "Invalid path component");
		std::string stem(part.substr(0, part.find('.')));
		std::transform(stem.begin(), stem.end(), stem.begin(),
			       [](unsigned char c) { return char(std::toupper(c)); });
		require(stem != "CON" && stem != "PRN" && stem != "AUX" && stem != "NUL" &&
				!(stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
				  stem[3] >= '0' && stem[3] <= '9'),
			"Reserved filename");
		if (end == p.npos)
			break;
		start = end + 1;
	}
}
void https_url(std::string_view url)
{
	require(url.starts_with("https://") && url.size() <= 4096 && url.find_first_of("\\\r\n\t #") == url.npos,
		"Expected HTTPS URL");
	auto host = url.substr(8, url.find('/', 8) == url.npos ? url.size() - 8 : url.find('/', 8) - 8);
	require(!host.empty() && host.find_first_of("@%?#") == host.npos, "Credentials or malformed URL host");
	// Catalogue locators never carry credentials or signed query strings. CDN redirects may.
	require(url.find('?') == url.npos, "Query strings are not catalogue locators");
}
static std::vector<unsigned char> unhex(std::string_view s)
{
	require(hex_string(s, s.size()) && s.size() % 2 == 0, "Invalid hex");
	std::vector<unsigned char> out(s.size() / 2);
	for (std::size_t i = 0; i < out.size(); ++i)
		out[i] = static_cast<unsigned char>(std::stoul(std::string(s.substr(i * 2, 2)), nullptr, 16));
	return out;
}
static std::string tohex(const unsigned char *data, std::size_t size)
{
	std::string s;
	for (std::size_t i = 0; i < size; ++i) {
		s += "0123456789abcdef"[data[i] >> 4];
		s += "0123456789abcdef"[data[i] & 15];
	}
	return s;
}
class Hash {
#ifdef _WIN32
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
#else
	EVP_MD_CTX *hash = nullptr;
#endif
public:
	Hash()
	{
#ifdef _WIN32
		require(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "SHA256 provider",
			ErrorCode::internal);
		if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
			BCryptCloseAlgorithmProvider(alg, 0);
			throw Failure(ErrorCode::internal, "SHA256 create");
		}
#else
		hash = EVP_MD_CTX_new();
		require(hash != nullptr, "SHA256 allocation", ErrorCode::out_of_memory);
		if (EVP_DigestInit_ex(hash, EVP_sha256(), nullptr) != 1) {
			EVP_MD_CTX_free(hash);
			throw Failure(ErrorCode::internal, "SHA256 create");
		}
#endif
	}
	~Hash()
	{
#ifdef _WIN32
		BCryptDestroyHash(hash);
		BCryptCloseAlgorithmProvider(alg, 0);
#else
		EVP_MD_CTX_free(hash);
#endif
	}
	void update(const char *p, std::size_t n)
	{
#ifdef _WIN32
		require(n <= std::numeric_limits<ULONG>::max() &&
				BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char *>(p)),
					       static_cast<ULONG>(n), 0) >= 0,
			"SHA256 update", ErrorCode::internal);
#else
		require(EVP_DigestUpdate(hash, p, n) == 1, "SHA256 update", ErrorCode::internal);
#endif
	}
	std::string finish()
	{
		std::array<unsigned char, 32> out{};
#ifdef _WIN32
		require(BCryptFinishHash(hash, out.data(), 32, 0) >= 0, "SHA256 finish", ErrorCode::internal);
#else
		require(EVP_DigestFinal_ex(hash, out.data(), nullptr) == 1, "SHA256 finish", ErrorCode::internal);
#endif
		return tohex(out.data(), out.size());
	}
};
std::string sha256(std::string_view bytes)
{
	Hash h;
	h.update(bytes.data(), bytes.size());
	return h.finish();
}
std::string file_sha256(const fs::path &path, std::stop_token stop)
{
	std::ifstream in(path, std::ios::binary);
	require(bool(in), "Cannot open file for hashing", ErrorCode::io_failure);
	Hash h;
	std::array<char, 65536> buffer{};
	while (in) {
		require(!stop.stop_requested(), "Cancelled", ErrorCode::cancelled);
		in.read(buffer.data(), buffer.size());
		h.update(buffer.data(), static_cast<std::size_t>(in.gcount()));
	}
	require(in.eof(), "Read failed while hashing", ErrorCode::io_failure);
	return h.finish();
}
bool verify_signature(std::string_view bytes, std::string_view pub, std::string_view sig)
{
	if (!hex_string(pub, 128) || !hex_string(sig, 128))
		return false;
	auto xy = unhex(pub), signature = unhex(sig);
#ifdef _WIN32
	auto digest = unhex(sha256(bytes));
	BCRYPT_ALG_HANDLE alg = nullptr;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0)
		return false;
	std::vector<unsigned char> blob(sizeof(BCRYPT_ECCKEY_BLOB) + xy.size());
	BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32};
	std::memcpy(blob.data(), &header, sizeof(header));
	std::copy(xy.begin(), xy.end(), blob.begin() + sizeof(header));
	BCRYPT_KEY_HANDLE key = nullptr;
	bool ok = BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, blob.data(),
				      static_cast<ULONG>(blob.size()), 0) >= 0;
	if (ok)
		ok = BCryptVerifySignature(key, nullptr, digest.data(), 32, signature.data(), 64, 0) >= 0;
	if (key)
		BCryptDestroyKey(key);
	BCryptCloseAlgorithmProvider(alg, 0);
	return ok;
#else
	std::vector<unsigned char> point{4};
	point.insert(point.end(), xy.begin(), xy.end());
	char group[] = "prime256v1";
	OSSL_PARAM params[] = {OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, group, 0),
			       OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, point.data(), point.size()),
			       OSSL_PARAM_construct_end()};
	std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
		EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr), EVP_PKEY_CTX_free);
	EVP_PKEY *raw = nullptr;
	if (!ctx || EVP_PKEY_fromdata_init(ctx.get()) != 1 ||
	    EVP_PKEY_fromdata(ctx.get(), &raw, EVP_PKEY_PUBLIC_KEY, params) != 1)
		return false;
	std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(raw, EVP_PKEY_free);
	std::vector<unsigned char> der{0x30, 0};
	for (std::size_t start : {std::size_t(0), std::size_t(32)}) {
		auto end = start + 32;
		while (start < end - 1 && signature[start] == 0)
			++start;
		bool pad = (signature[start] & 128) != 0;
		der.push_back(2);
		der.push_back(static_cast<unsigned char>(end - start + (pad ? 1 : 0)));
		if (pad)
			der.push_back(0);
		der.insert(der.end(), signature.begin() + start, signature.begin() + end);
	}
	der[1] = static_cast<unsigned char>(der.size() - 2);
	std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> md(EVP_MD_CTX_new(), EVP_MD_CTX_free);
	return md && EVP_DigestVerifyInit(md.get(), nullptr, EVP_sha256(), nullptr, key.get()) == 1 &&
	       EVP_DigestVerify(md.get(), der.data(), der.size(), reinterpret_cast<const unsigned char *>(bytes.data()),
				bytes.size()) == 1;
#endif
}
static std::vector<std::string> ids(const Json &j, bool empty = false)
{
	require(j.is_array() && j.size() <= 2048 && (empty || !j.empty()), "Invalid ID list");
	std::vector<std::string> out;
	std::set<std::string> unique;
	for (const auto &v : j) {
		auto s = string(v);
		identifier(s);
		require(unique.insert(s).second, "Duplicate ID");
		out.push_back(s);
	}
	return out;
}
static Artifact artifact(const Json &j)
{
	fields(j, {"id", "path", "format", "bytes", "sha256", "licenses", "origin"});
	Artifact a;
	a.id = string(j.at("id"));
	identifier(a.id);
	a.path = string(j.at("path"));
	relative_path(a.path);
	a.format = string(j.at("format"));
	require(a.format == "onnx" || a.format == "export-json" || a.format == "safetensors" || a.format == "npz" ||
			a.format == "manifest-json",
		"Unsupported artifact format");
	a.bytes = uint64(j.at("bytes"));
	require(a.bytes > 0 && a.bytes <= (1ULL << 40), "Invalid artifact size");
	a.sha256 = string(j.at("sha256"));
	require(hex_string(a.sha256, 64), "Invalid SHA256");
	a.licences = ids(j.at("licenses"));
	auto &o = j.at("origin");
	if (o.contains("repo")) {
		fields(o, {"repo", "revision", "file"});
		auto repo = string(o.at("repo"));
		relative_path(repo);
		require(std::count(repo.begin(), repo.end(), '/') == 1, "Expected HF publisher/repo");
		a.revision = string(o.at("revision"));
		require(hex_string(a.revision, 40), "HF revision must be commit SHA");
		auto file = string(o.at("file"));
		relative_path(file);
		a.url = "https://huggingface.co/" + repo + "/resolve/" + a.revision + "/" + file;
	} else {
		fields(o, {"url", "revision"}, {"md5_base64"});
		a.url = string(o.at("url"));
		https_url(a.url);
		a.revision = string(o.at("revision"));
		require(a.revision == a.sha256, "Direct URL revision must equal content SHA256");
		if (o.contains("md5_base64")) {
			auto md5 = string(o.at("md5_base64"));
			require(md5.size() == 24 && md5.ends_with("==") &&
					md5.find_first_not_of(
						"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") ==
						md5.npos,
				"Invalid publisher MD5");
		}
	}
	return a;
}
std::uint64_t Manifest::peak_additional_bytes() const
{
	std::set<std::string> seen;
	std::uint64_t total = 0;
	for (const auto &[id, a] : artifacts)
		if (seen.insert(a.sha256).second)
			total = checked_add(total, a.bytes);
	total = checked_add(checked_add(checked_add(checked_add(total, staged_graph_bytes), bound_bytes),
					workspace_bytes),
			    engine_bytes);
	return checked_add(total, std::max({margin_bytes, std::uint64_t(1) << 30, total / 10}));
}
std::map<std::string, Artifact> parse_source_inventory(std::string_view bytes)
{
	auto j = strict_json(bytes);
	fields(j, {"schema_version", "sources"}, {"$comment"});
	require(uint64(j.at("schema_version")) == 1 && j.at("sources").is_array() && j.at("sources").size() <= 2048,
		"Invalid source inventory");
	std::map<std::string, Artifact> out;
	for (auto &v : j.at("sources")) {
		auto a = artifact(v);
		require(a.format == "safetensors" || a.format == "npz", "Unsafe source container");
		require(out.emplace(a.id, a).second, "Duplicate source");
	}
	return out;
}
Manifest parse_manifest(std::string_view bytes, std::string_view expected)
{
	require(hex_string(expected, 64) && sha256(bytes) == expected, "Manifest digest mismatch",
		ErrorCode::hash_mismatch);
	auto j = strict_json(bytes);
	fields(j,
	       {"schema_version", "id", "revision", "display_name", "publisher", "description", "pipeline",
		"export_contract", "provenance", "licenses", "artifacts", "graphs", "budgets", "profiles",
		"compatibility"},
	       {"$comment"});
	require(uint64(j.at("schema_version")) == 1 && uint64(j.at("export_contract")) == 1,
		"Unsupported manifest version");
	Manifest m;
	m.raw = bytes;
	m.digest = expected;
	m.key = {string(j.at("id")), string(j.at("revision"))};
	identifier(m.key.id);
	require(hex_string(m.key.revision, 40), "Package revision must be immutable commit SHA");
	m.display_name = string(j.at("display_name"));
	m.publisher = string(j.at("publisher"));
	m.pipeline = string(j.at("pipeline"));
	m.description = string(j.at("description"));
	require(m.pipeline == "ck2" || m.pipeline == "ck1" || m.pipeline == "hint" || m.pipeline == "synthetic",
		"Unsupported pipeline");
	auto &p = j.at("provenance");
	fields(p, {"source_url", "export_revision"});
	https_url(string(p.at("source_url")));
	require(hex_string(string(p.at("export_revision")), 40), "Invalid exporter revision");
	std::set<std::string> licences;
	require(j.at("licenses").is_array() && !j.at("licenses").empty() && j.at("licenses").size() <= 64,
		"Invalid licences");
	for (auto &l : j.at("licenses")) {
		fields(l, {"id", "text_sha256"});
		LicenceAcceptance a{string(l.at("id")), string(l.at("text_sha256"))};
		identifier(a.licence_id);
		require(hex_string(a.text_sha256, 64) && licences.insert(a.licence_id).second,
			"Invalid or duplicate licence");
		m.licences.push_back(a);
	}
	require(j.at("artifacts").is_array() && !j.at("artifacts").empty() && j.at("artifacts").size() <= 2048,
		"Invalid artifacts");
	std::set<std::string> paths;
	for (auto &v : j.at("artifacts")) {
		auto a = artifact(v);
		std::string folded = a.path;
		std::transform(folded.begin(), folded.end(), folded.begin(),
			       [](unsigned char c) { return char(std::tolower(c)); });
		require(folded != "manifest.json" && folded != "ready.json" && !folded.starts_with("manifest.json/") &&
				!folded.starts_with("ready.json/"),
			"Reserved package path");
		require(paths.insert(folded).second, "Overlapping artifact paths");
		for (auto &l : a.licences)
			require(licences.contains(l), "Unresolved licence");
		require(m.artifacts.emplace(a.id, a).second, "Duplicate artifact");
	}
	for (auto &a : paths)
		for (auto &b : paths)
			if (a != b)
				require(!b.starts_with(a + "/"), "File/directory path collision");
	require(j.at("graphs").is_array() && !j.at("graphs").empty() && j.at("graphs").size() <= 256, "Invalid graphs");
	std::set<std::string> graph_ids, used;
	for (auto &v : j.at("graphs")) {
		fields(v, {"id", "role", "graph", "metadata", "external_data", "weights_bytes", "weights_sha256",
			   "sources", "opset", "precision", "interface", "ir_version", "color_contract", "parity"});
		Graph g;
		g.id = string(v.at("id"));
		identifier(g.id);
		require(graph_ids.insert(g.id).second, "Duplicate graph");
		g.role = string(v.at("role"));
		require(g.role == "moge" || g.role == "rvm" || g.role == "stage1" || g.role == "stage2" ||
				g.role == "refiner" || g.role == "hint",
			"Unknown graph role");
		g.graph = string(v.at("graph"));
		g.metadata = string(v.at("metadata"));
		require(m.artifacts.contains(g.graph) && m.artifacts.at(g.graph).format == "onnx" &&
				m.artifacts.contains(g.metadata) && m.artifacts.at(g.metadata).format == "export-json",
			"Invalid graph/metadata references");
		auto graph_path = fs::path(m.artifacts.at(g.graph).path),
		     meta_path = fs::path(m.artifacts.at(g.metadata).path);
		require(graph_path.extension() == ".onnx" &&
				meta_path == fs::path(graph_path).replace_extension(".json"),
			"Expected graph.onnx and graph.json pair");
		g.external_data = string(v.at("external_data"));
		relative_path(g.external_data);
		auto folded_external = g.external_data;
		std::transform(folded_external.begin(), folded_external.end(), folded_external.begin(),
			       [](unsigned char c) { return char(std::tolower(c)); });
		require(fs::path(g.external_data) == fs::path(graph_path).replace_extension(".weights") &&
				paths.insert(folded_external).second,
			"Invalid external-data location");
		g.weights_bytes = uint64(v.at("weights_bytes"));
		require(g.weights_bytes > 0 && g.weights_bytes <= (1ULL << 40), "Invalid bound size");
		g.weights_sha256 = string(v.at("weights_sha256"));
		require(hex_string(g.weights_sha256, 64), "Invalid bound digest");
		g.sources = ids(v.at("sources"));
		for (auto &id : g.sources) {
			require(m.artifacts.contains(id), "Unknown source");
			auto f = m.artifacts.at(id).format;
			require(f == "safetensors" || f == "npz", "Unsafe weight container");
			used.insert(id);
		}
		require(uint64(v.at("opset")) == 18 && string(v.at("precision")) == "fp32",
			"Export contract requires opset18/fp32");
		require(uint64(v.at("ir_version")) > 0 && uint64(v.at("ir_version")) <= 100, "Invalid ONNX IR");
		(void)string(v.at("color_contract"));
		auto &parity = v.at("parity");
		fields(parity, {"report_url", "report_sha256", "status"});
		https_url(string(parity.at("report_url")));
		require(hex_string(string(parity.at("report_sha256")), 64), "Invalid parity report digest");
		auto status = string(parity.at("status"));
		require(status == "unverified" || status == "passed", "Invalid parity status");
		auto &iface = v.at("interface");
		fields(iface, {"inputs", "outputs"});
		for (auto name : {"inputs", "outputs"}) {
			auto &tensors = iface.at(name);
			require(tensors.is_array() && !tensors.empty() && tensors.size() <= 128,
				"Invalid declared interface");
			std::set<std::string> names;
			for (auto &t : tensors) {
				fields(t, {"name", "dtype", "shape"});
				require(names.insert(string(t.at("name"))).second, "Duplicate interface name");
				require(string(t.at("dtype")) == "float32" && t.at("shape").is_array() &&
						t.at("shape").size() <= 8,
					"Invalid interface tensor");
				std::uint64_t size = 4;
				for (auto &d : t.at("shape")) {
					auto n = uint64(d);
					require(n > 0 && n <= (1ULL << 40) / size, "Invalid interface shape");
					size *= n;
				}
			}
		}
		g.inputs = iface.at("inputs");
		g.outputs = iface.at("outputs");
		used.insert(g.graph);
		used.insert(g.metadata);
		m.bound_bytes = checked_add(m.bound_bytes, g.weights_bytes);
		m.graphs.push_back(g);
		m.staged_graph_bytes = checked_add(m.staged_graph_bytes, checked_add(m.artifacts.at(g.graph).bytes,
										     m.artifacts.at(g.metadata).bytes));
	}
	for (auto &a : paths)
		for (auto &b : paths)
			if (a != b)
				require(!b.starts_with(a + "/"), "Output file/directory collision");
	require(used.size() == m.artifacts.size(), "Unreferenced payload");
	auto &b = j.at("budgets");
	fields(b, {"bound_bytes", "workspace_bytes", "engine_bytes", "margin_bytes"});
	require(uint64(b.at("bound_bytes")) >= m.bound_bytes, "Bound budget too small");
	m.bound_bytes = uint64(b.at("bound_bytes"));
	m.workspace_bytes = uint64(b.at("workspace_bytes"));
	m.engine_bytes = uint64(b.at("engine_bytes"));
	m.margin_bytes = uint64(b.at("margin_bytes"));
	m.compatibility = j.at("compatibility");
	require(m.compatibility.is_array() && m.compatibility.size() <= 128, "Invalid compatibility records");
	for (auto &c : m.compatibility) {
		fields(c, {"backend", "required_features", "test_status", "evidence"});
		auto backend = string(c.at("backend"));
		require(backend == "cuda" || backend == "tensorrt" || backend == "tensorrt-rtx" ||
				backend == "directml" || backend == "migraphx" || backend == "cpu-diagnostic",
			"Unknown backend");
		(void)ids(c.at("required_features"), true);
		(void)string(c.at("evidence"));
		auto status = string(c.at("test_status"));
		require(status == "unverified" || status == "expected" || status == "build-only" ||
				status == "hardware-tested" || status == "community-tested",
			"Unknown compatibility status");
	}
	m.profiles = j.at("profiles");
	require(m.profiles.is_array() && !m.profiles.empty() && m.profiles.size() <= 64, "Invalid profiles");
	std::set<std::string> profile_ids;
	for (auto &p : m.profiles) {
		fields(p, {"id", "graphs", "grid_hw", "roi", "refiner_tiles", "settings_schema"});
		auto id = string(p.at("id"));
		identifier(id);
		require(profile_ids.insert(id).second && uint64(p.at("settings_schema")) == 1 &&
				p.at("roi").is_boolean(),
			"Invalid profile");
		for (auto &graph_id : ids(p.at("graphs")))
			require(graph_ids.contains(graph_id), "Unknown profile graph");
		auto &grid = p.at("grid_hw");
		require(grid.is_array() && grid.size() == 2, "Invalid grid");
		for (auto &n : grid)
			require(uint64(n) > 0 && uint64(n) <= 16384, "Invalid grid dimension");
		require(uint64(p.at("refiner_tiles")) <= 16384, "Invalid refiner tile count");
	}
	(void)m.peak_additional_bytes();
	return m;
}
Catalogue parse_catalogue(std::string_view bytes, std::string_view signature, std::string_view public_key,
			  std::uint64_t minimum)
{
	require(bytes.size() <= 1024 * 1024 && verify_signature(bytes, public_key, signature),
		"Invalid catalogue signature", ErrorCode::hash_mismatch);
	auto j = strict_json(bytes, 1024 * 1024);
	fields(j, {"schema_version", "sequence", "packages"}, {"$comment"});
	require(uint64(j.at("schema_version")) == 1, "Unsupported catalogue version");
	Catalogue c;
	c.sequence = uint64(j.at("sequence"));
	require(c.sequence >= minimum, "Catalogue rollback");
	require(j.at("packages").is_array() && j.at("packages").size() <= 1024, "Invalid package list");
	std::set<std::pair<std::string, std::string>> seen;
	for (auto &p : j.at("packages")) {
		fields(p, {"id", "revision", "manifest"});
		CatalogueEntry e;
		e.key = {string(p.at("id")), string(p.at("revision"))};
		identifier(e.key.id);
		require(hex_string(e.key.revision, 40), "Invalid package revision");
		require(seen.emplace(e.key.id, e.key.revision).second, "Duplicate package");
		e.manifest = artifact(p.at("manifest"));
		require(e.manifest.format == "manifest-json" && e.manifest.bytes <= 4 * 1024 * 1024,
			"Invalid manifest artifact");
		c.entries.push_back(e);
	}
	return c;
}
Manifest parse_manifest(std::string_view bytes, const CatalogueEntry &pin)
{
	require(bytes.size() == pin.manifest.bytes, "Manifest size mismatch", ErrorCode::hash_mismatch);
	auto m = parse_manifest(bytes, pin.manifest.sha256);
	require(m.key == pin.key, "Manifest package identity mismatch", ErrorCode::hash_mismatch);
	return m;
}
static std::uint64_t tensor(const Json &t)
{
	fields(t, {"name", "dtype", "shape"});
	(void)string(t.at("name"));
	require(string(t.at("dtype")) == "float32", "Unsupported tensor dtype");
	auto &shape = t.at("shape");
	require(shape.is_array() && shape.size() <= 8, "Invalid rank");
	std::uint64_t size = 4;
	for (auto &d : shape) {
		auto n = uint64(d);
		require(n > 0 && n <= (1ULL << 40) / size, "Invalid tensor shape");
		size *= n;
	}
	return size;
}
void validate_export_metadata(std::string_view bytes, const Graph &g, const Manifest &m)
{
	auto j = strict_json(bytes, 32 * 1024 * 1024);
	fields(j, {"graph", "opset", "inputs", "outputs", "weights", "sources"}, {"H", "W", "weights_sha256"});
	if (j.contains("weights_sha256"))
		require(string(j.at("weights_sha256")) == g.weights_sha256, "Exporter output hash mismatch");
	require(j.contains("H") == j.contains("W"), "Incomplete exporter grid");
	if (j.contains("H"))
		for (auto key : {"H", "W"})
			require(uint64(j.at(key)) > 0 && uint64(j.at(key)) <= 16384, "Invalid exporter grid");
	require(string(j.at("graph")) == fs::path(m.artifacts.at(g.graph).path).stem().string() &&
			uint64(j.at("opset")) == 18,
		"Graph metadata identity mismatch");
	for (auto name : {"inputs", "outputs"}) {
		auto &a = j.at(name);
		require(a.is_array() && !a.empty() && a.size() <= 128, "Invalid graph interface");
		std::set<std::string> names;
		for (auto &t : a) {
			(void)tensor(t);
			require(names.insert(string(t.at("name"))).second, "Duplicate tensor name");
		}
	}
	require(j.at("inputs") == g.inputs && j.at("outputs") == g.outputs,
		"Declared graph interface differs from exporter");
	auto &sources = j.at("sources");
	require(sources.is_object() && sources.size() == g.sources.size(), "Source list mismatch");
	for (auto &id : g.sources) {
		require(sources.contains(id), "Missing exporter source");
		auto &s = sources.at(id);
		auto &a = m.artifacts.at(id);
		fields(s, {"file", "format", "size", "sha256", "license"}, {"repo", "revision", "url", "note"});
		require(uint64(s.at("size")) == a.bytes && string(s.at("sha256")) == a.sha256 &&
				string(s.at("format")) == a.format,
			"Source identity mismatch");
		auto filename = string(s.at("file"));
		relative_path(filename);
		if (s.contains("repo")) {
			require(s.contains("revision") && !s.contains("url"), "Ambiguous source locator");
			require("https://huggingface.co/" + string(s.at("repo")) + "/resolve/" +
						string(s.at("revision")) + "/" + filename ==
					a.url,
				"Source locator mismatch");
		} else
			require(s.contains("url") && string(s.at("url")) == a.url, "Source locator mismatch");
	}
	auto &weights = j.at("weights");
	require(weights.is_array() && weights.size() <= 100000, "Invalid weights table");
	std::vector<std::pair<std::uint64_t, std::uint64_t>> spans;
	std::set<std::string> names;
	for (auto &w : weights) {
		fields(w, {"name", "dtype", "shape", "offset", "length", "source"});
		Json t = {{"name", w.at("name")}, {"dtype", w.at("dtype")}, {"shape", w.at("shape")}};
		auto length = tensor(t), off = uint64(w.at("offset"));
		require(names.insert(string(w.at("name"))).second, "Duplicate weight name");
		require(off % 64 == 0 && length == uint64(w.at("length")) &&
				checked_add(off, length) <= g.weights_bytes,
			"Invalid weight span");
		spans.emplace_back(off, off + length);
		auto &s = w.at("source");
		fields(s, {"file", "key", "ops"});
		auto &ops = s.at("ops");
		require(ops.is_array() && ops.size() <= 16, "Invalid operations");
		bool zero = s.at("file").is_null();
		if (zero)
			require(s.at("key").is_null() && ops.size() == 1, "Invalid zero source");
		else
			require(sources.contains(string(s.at("file"))) && !string(s.at("key")).empty(),
				"Unknown weight source");
		for (auto &op : ops) {
			require(op.is_array() && !op.empty(), "Invalid operation");
			auto kind = string(op.at(0));
			if (kind == "zeros")
				require(zero && (op.size() == 1 || (op.size() == 2 && op.at(1).is_null())),
					"Invalid zeros operation");
			else {
				require(!zero && string(s.at("file")) == "rvm", "Transforms permitted only for RVM");
				if (kind == "squeeze")
					require(op.size() == 2 && uint64(op.at(1)) < 8, "Invalid squeeze");
				else if (kind == "merge")
					require(op.size() == 3 && uint64(op.at(1)) < uint64(op.at(2)) &&
							uint64(op.at(2)) <= 8,
						"Invalid merge");
				else if (kind == "transpose") {
					require(op.size() == 2 && op.at(1).is_array() && op.at(1).size() <= 8,
						"Invalid transpose");
					std::set<std::uint64_t> perm;
					for (auto &v : op.at(1))
						require(uint64(v) < op.at(1).size() && perm.insert(uint64(v)).second,
							"Invalid permutation");
				} else
					require(false, "Unknown operation");
			}
		}
	}
	std::sort(spans.begin(), spans.end());
	std::uint64_t end = 0;
	for (auto [a, b] : spans) {
		require(a >= end, "Overlapping weights");
		end = b;
	}
	require(end == g.weights_bytes, "Bound extent mismatch");
}
} // namespace corridorkey::models
