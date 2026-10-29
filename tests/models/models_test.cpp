// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "models/catalog.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <future>
#include <cstdlib>
using namespace corridorkey;
using namespace corridorkey::models;
namespace {
const std::string commit(40, '1');
const std::string license_text = "Synthetic fixture licence. No model data.";
Json registry_json()
{
	return {{"schema_version", 1},
		{"licenses", Json::array({{{"id", "LicenseRef-Test"},
					   {"text", license_text},
					   {"text_sha256", sha256(license_text)},
					   {"source_url", "https://example.invalid/license"},
					   {"attribution", "Test fixture authors"},
					   {"scope", "Synthetic bytes"}}})}};
}
Json artifact_json(std::string id, std::string path, std::string format, std::string bytes, std::string url)
{
	return {{"id", id},
		{"path", path},
		{"format", format},
		{"bytes", bytes.size()},
		{"sha256", sha256(bytes)},
		{"licenses", Json::array({"LicenseRef-Test"})},
		{"origin", {{"url", url}, {"revision", sha256(bytes)}}}};
}
struct Tiny {
	std::string graph = "independently authored graph fixture", weights = "1234", metadata;
	Json json;
	Manifest manifest;
	LicenceRegistry licenses = LicenceRegistry::parse(registry_json().dump());
	Tiny(std::string source = "1234", std::string source_url = "https://example.invalid/source")
	{
		auto source_hash = sha256(source);
		metadata =
			Json{{"graph", "tiny"},
			     {"opset", 18},
			     {"inputs",
			      Json::array(
				      {{{"name", "x"}, {"dtype", "float32"}, {"shape", Json::array({1, 1, 1, 1})}}})},
			     {"outputs",
			      Json::array(
				      {{{"name", "y"}, {"dtype", "float32"}, {"shape", Json::array({1, 1, 1, 1})}}})},
			     {"weights",
			      Json::array({{{"name", "bias"},
					    {"dtype", "float32"},
					    {"shape", Json::array({1})},
					    {"offset", 0},
					    {"length", 4},
					    {"source",
					     {{"file", "test-source"}, {"key", "bias"}, {"ops", Json::array()}}}}})},
			     {"sources",
			      {{"test-source",
				{{"url", source_url},
				 {"file", "source.safetensors"},
				 {"format", "safetensors"},
				 {"size", source.size()},
				 {"sha256", source_hash},
				 {"license", "Synthetic fixture"}}}}}}
				.dump();
		json = {{"schema_version", 1},
			{"id", "synthetic-test-only"},
			{"revision", commit},
			{"display_name", "Synthetic test fixture"},
			{"publisher", "Test authors"},
			{"pipeline", "synthetic"},
			{"export_contract", 1},
			{"provenance", {{"source_url", "https://example.invalid/tests"}, {"export_revision", commit}}},
			{"licenses", Json::array({{{"id", "LicenseRef-Test"}, {"text_sha256", sha256(license_text)}}})},
			{"artifacts", Json::array({artifact_json("graph", "graphs/tiny.onnx", "onnx", graph,
								 "https://example.invalid/graph"),
						   artifact_json("metadata", "graphs/tiny.json", "export-json",
								 metadata, "https://example.invalid/metadata"),
						   artifact_json("test-source", "sources/source.safetensors",
								 "safetensors", source, source_url)})},
			{"graphs", Json::array({{{"id", "tiny"},
						 {"role", "hint"},
						 {"graph", "graph"},
						 {"metadata", "metadata"},
						 {"external_data", "graphs/tiny.weights"},
						 {"weights_bytes", 4},
						 {"weights_sha256", sha256(weights)},
						 {"sources", Json::array({"test-source"})},
						 {"opset", 18},
						 {"precision", "fp32"}}})},
			{"budgets",
			 {{"bound_bytes", 4}, {"workspace_bytes", 1024}, {"engine_bytes", 1024}, {"margin_bytes", 0}}}};
		json["description"] = "Independently authored bytes for installation tests.";
		auto iface = strict_json(metadata);
		json["graphs"][0]["interface"] = {{"inputs", iface.at("inputs")}, {"outputs", iface.at("outputs")}};
		json["graphs"][0]["ir_version"] = 8;
		json["graphs"][0]["color_contract"] = "Synthetic scalar; no color transform.";
		json["graphs"][0]["parity"] = {{"report_url", "https://example.invalid/parity"},
					       {"report_sha256", sha256("synthetic parity")},
					       {"status", "unverified"}};
		json["profiles"] = Json::array({{{"id", "test"},
						 {"graphs", Json::array({"tiny"})},
						 {"grid_hw", Json::array({1, 1})},
						 {"roi", false},
						 {"refiner_tiles", 0},
						 {"settings_schema", 1}}});
		json["compatibility"] = Json::array({{{"backend", "cpu-diagnostic"},
						      {"required_features", Json::array()},
						      {"test_status", "unverified"},
						      {"evidence", "Synthetic fixture; never executes inference."}}});
		reparse();
	}
	void reparse()
	{
		auto raw = json.dump();
		manifest = parse_manifest(raw, sha256(raw));
	}
	void seed(Cache &c)
	{
		atomic_write(c.blob(manifest.artifacts.at("graph").sha256), graph);
		atomic_write(c.blob(manifest.artifacts.at("metadata").sha256), metadata);
	}
	void publish(Cache &c)
	{
		FileLock lock(c.revision_lock(manifest.key), false, true);
		seed(c);
		c.prepare(manifest);
		atomic_write(c.staging(manifest.key) / "graphs/tiny.weights", weights);
		c.publish(manifest);
	}
};
struct Temp {
	fs::path path;
	Temp()
	{
		static std::atomic<unsigned> sequence{0};
		path = fs::current_path() / ("models-test-" + std::to_string(Clock::now().time_since_epoch().count()) +
					     "-" + std::to_string(sequence++));
		fs::create_directories(path);
	}
	~Temp()
	{
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};
struct MemoryHttp final : HttpClient {
	std::map<std::string, std::string> files;
	std::atomic<unsigned> calls{0};
	void get(const HttpRequest &r, const HeaderSink &header, const ByteSink &sink, std::stop_token) override
	{
		++calls;
		auto bytes = files.at(r.url);
		HttpResponse response{200, {{"etag", "\"test\""}, {"content-length", std::to_string(bytes.size())}}};
		if (header(response))
			sink(bytes);
	}
};
DownloadOptions fast_options()
{
	DownloadOptions options;
	options.wait = [](std::chrono::milliseconds, std::stop_token s) {
		return !s.stop_requested();
	};
	return options;
}
void rejected(const Json &j)
{
	auto raw = j.dump();
	CHECK_THROWS_AS(parse_manifest(raw, sha256(raw)), Failure);
}
} // namespace
TEST_CASE("SHA256 vectors and bounded strict JSON")
{
	CHECK(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK_THROWS_AS(strict_json("{\"a\":1,\"a\":2}"), Failure);
	CHECK_THROWS_AS(strict_json("{\"a\":NaN}"), Failure);
	CHECK_THROWS_AS(strict_json("12345", 4), Failure);
	CHECK_THROWS_AS(strict_json(std::string(40, '[') + "0" + std::string(40, ']')), Failure);
	CHECK(uint64(strict_json("18446744073709551615")) == UINT64_MAX);
	CHECK_THROWS_AS(uint64(strict_json("18446744073709551616")), Failure);
	CHECK_THROWS_AS(uint64(strict_json("1.0")), Failure);
	CHECK_THROWS_AS(checked_add(UINT64_MAX, 1), Failure);
}
TEST_CASE("Manifest references paths formats sizes and budgets are closed")
{
	Tiny t;
	CatalogueEntry pin;
	pin.key = t.manifest.key;
	pin.manifest.sha256 = t.manifest.digest;
	pin.manifest.bytes = t.manifest.raw.size();
	CHECK_NOTHROW(parse_manifest(t.manifest.raw, pin));
	pin.key.revision = std::string(40, '2');
	CHECK_THROWS_AS(parse_manifest(t.manifest.raw, pin), Failure);
	CHECK(t.manifest.graphs.front().external_data == "graphs/tiny.weights");
	CHECK(t.manifest.peak_additional_bytes() > (1ULL << 30));
	CHECK_THROWS_AS(parse_manifest(t.manifest.raw, std::string(64, '0')), Failure);
	SUBCASE("unknown fields")
	{
		auto j = t.json;
		j["command"] = "execute";
		rejected(j);
	}
	SUBCASE("schema")
	{
		auto j = t.json;
		j["schema_version"] = 2;
		rejected(j);
	}
	SUBCASE("floating length")
	{
		auto j = t.json;
		j["artifacts"][0]["bytes"] = 1.0;
		rejected(j);
	}
	SUBCASE("bad hash")
	{
		auto j = t.json;
		j["artifacts"][0]["sha256"] = "<pending>";
		rejected(j);
	}
	SUBCASE("symbolic revision")
	{
		auto j = t.json;
		j["revision"] = "main";
		rejected(j);
	}
	SUBCASE("unsafe format")
	{
		auto j = t.json;
		j["artifacts"][2]["format"] = "pickle";
		rejected(j);
	}
	SUBCASE("unknown source")
	{
		auto j = t.json;
		j["graphs"][0]["sources"] = Json::array({"missing"});
		rejected(j);
	}
	SUBCASE("self cycle")
	{
		auto j = t.json;
		j["graphs"][0]["sources"] = Json::array({"graph"});
		rejected(j);
	}
	SUBCASE("duplicate IDs")
	{
		auto j = t.json;
		j["artifacts"].push_back(j["artifacts"][0]);
		rejected(j);
	}
	SUBCASE("license resolution")
	{
		auto j = t.json;
		j["artifacts"][0]["licenses"] = Json::array({"missing"});
		rejected(j);
	}
	SUBCASE("overflow")
	{
		auto j = t.json;
		j["budgets"]["workspace_bytes"] = UINT64_MAX;
		rejected(j);
	}
	SUBCASE("external data traversal")
	{
		auto j = t.json;
		j["graphs"][0]["external_data"] = "../outside";
		rejected(j);
	}
	SUBCASE("case alias")
	{
		auto j = t.json;
		j["artifacts"][2]["path"] = "GRAPHS/TINY.ONNX";
		rejected(j);
	}
	SUBCASE("credential URL")
	{
		auto j = t.json;
		j["artifacts"][2]["origin"]["url"] = "https://token@example.invalid/source";
		rejected(j);
	}
	SUBCASE("HTTP forbidden")
	{
		auto j = t.json;
		j["artifacts"][2]["origin"]["url"] = "http://example.invalid/source";
		rejected(j);
	}
	SUBCASE("reserved cache metadata")
	{
		auto j = t.json;
		j["artifacts"][2]["path"] = "ready.json";
		rejected(j);
	}
	for (auto path : {"/absolute", "../outside", "a/../outside", "C:/outside", "a\\outside", "a//b", "a/CON.txt",
			  "a/x.", "a/NUL"})
		CHECK_THROWS_AS(relative_path(path), Failure);
}
TEST_CASE("Exporter metadata uses aligned offsets and exact lengths")
{
	Tiny t;
	CHECK_NOTHROW(validate_export_metadata(t.metadata, t.manifest.graphs[0], t.manifest));
	auto m = strict_json(t.metadata);
	SUBCASE("overlap")
	{
		m["weights"].push_back(m["weights"][0]);
		m["weights"][1]["name"] = "different";
	}
	SUBCASE("offset alignment")
	{
		m["weights"][0]["offset"] = 1;
	}
	SUBCASE("wrong tensor size")
	{
		m["weights"][0]["length"] = 64;
	}
	SUBCASE("unsupported operation")
	{
		m["weights"][0]["source"]["ops"] = Json::array({Json::array({"eval", "code"})});
	}
	SUBCASE("source hash")
	{
		m["sources"]["test-source"]["sha256"] = std::string(64, '0');
	}
	SUBCASE("graph identity")
	{
		m["graph"] = "different";
	}
	CHECK_THROWS_AS(validate_export_metadata(m.dump(), t.manifest.graphs[0], t.manifest), Failure);
}
TEST_CASE("Bundled licence texts have exact hashes and MODNet remains separate")
{
	auto raw = read_small_file(fs::path(CK_SOURCE_DIR) / "data/models/licenses-v1.json");
	auto registry = LicenceRegistry::parse(raw);
	auto j = strict_json(raw);
	CHECK(j["licenses"].size() == 6);
	for (auto &r : j["licenses"])
		CHECK(registry.at({r["id"].get<std::string>(), r["text_sha256"].get<std::string>()}).digest ==
		      sha256(r["text"].get<std::string>()));
	j["licenses"][0]["text"] = "changed";
	CHECK_THROWS_AS(LicenceRegistry::parse(j.dump()), Failure);
}
TEST_CASE("Publisher source inventory contains no graph placeholders")
{
	auto sources = parse_source_inventory(read_small_file(fs::path(CK_SOURCE_DIR) / "data/models/sources-v1.json"));
	CHECK(sources.size() == 8);
	CHECK(sources.at("moge").format == "safetensors");
	CHECK(sources.at("rvm").bytes == 270086372);
	CHECK(sources.at("moge").revision == "3090937b74a09929079cf6c77ad1edd40a0a9aaf");
	auto registry =
		LicenceRegistry::parse(read_small_file(fs::path(CK_SOURCE_DIR) / "data/models/licenses-v1.json"));
	auto texts = strict_json(read_small_file(fs::path(CK_SOURCE_DIR) / "data/models/licenses-v1.json"));
	for (auto &[id, a] : sources)
		for (auto &licence : a.licences) {
			auto found = std::find_if(texts["licenses"].begin(), texts["licenses"].end(),
						  [&](auto &r) { return r["id"] == licence; });
			REQUIRE(found != texts["licenses"].end());
			CHECK_NOTHROW(registry.at({licence, (*found)["text_sha256"].template get<std::string>()}));
		}
}
TEST_CASE("Acceptance covers exact text package revision timestamp and all dependencies")
{
	Temp tmp;
	Cache cache(tmp.path);
	Tiny t;
	AcceptanceStore store(cache);
	CHECK_FALSE(store.contains(t.manifest, t.licenses));
	auto http = std::make_shared<MemoryHttp>();
	DownloadEngine engine(cache, http, fast_options());
	CHECK_THROWS_AS(engine.fetch(t.manifest, t.manifest.artifacts.at("test-source"), t.licenses, store), Failure);
	CHECK(http->calls == 0);
	CHECK_THROWS_AS(store.accept(t.manifest, t.licenses, {}), Failure);
	auto wrong = t.manifest.licences;
	wrong[0].text_sha256 = std::string(64, '0');
	CHECK_THROWS_AS(store.accept(t.manifest, t.licenses, wrong), Failure);
	store.accept(t.manifest, t.licenses, t.manifest.licences);
	CHECK(store.contains(t.manifest, t.licenses));
	auto receipt = strict_json(read_small_file(cache.path("receipts/" + t.manifest.digest + ".json")));
	CHECK(uint64(receipt.at("accepted_unix_seconds")) > 0);
	CHECK(receipt.at("local_context") == "local-user");
	t.json["revision"] = std::string(40, '2');
	t.reparse();
	CHECK_FALSE(store.contains(t.manifest, t.licenses));
}
TEST_CASE("Leases deletion crash-safe locks and explicit update selection")
{
	Temp tmp;
	Cache cache(tmp.path);
	Tiny old;
	old.publish(cache);
	cache.select_current(old.manifest);
	CHECK(cache.current(old.manifest.key.id) == old.manifest.key);
	Tiny next;
	next.json["revision"] = std::string(40, '2');
	next.reparse();
	next.publish(cache);
	CHECK(cache.current(old.manifest.key.id) == old.manifest.key);
	auto reader = cache.acquire(old.manifest);
	REQUIRE(reader);
	CHECK(reader->graphs.size() == 1);
	CHECK_FALSE(cache.remove(old.manifest.key));
	CHECK_FALSE(cache.prune_blobs({}));
	cache.select_current(next.manifest);
	CHECK_FALSE(cache.remove(old.manifest.key));
	reader.reset();
	CHECK(cache.remove(old.manifest.key));
	CHECK(cache.acquire(next.manifest));
	CHECK_FALSE(cache.remove(next.manifest.key));
	cache.clear_current(next.manifest.key.id);
	CHECK(cache.remove(next.manifest.key));
	CHECK(cache.prune_blobs({}));
	FileLock lock(cache.path("locks/test.lock"), false, false);
	CHECK(lock.owns_lock());
	FileLock contender(cache.path("locks/test.lock"), false, false);
	CHECK_FALSE(contender.owns_lock());
	std::stop_source cancel;
	cancel.request_stop();
	CHECK_THROWS_AS(FileLock(cache.path("locks/test.lock"), false, true, cancel.get_token()), Failure);
	CHECK_THROWS_AS(cache.check_space(UINT64_MAX), Failure);
	CHECK_THROWS_AS(cache.path("../outside"), Failure);
}
TEST_CASE("Unfinished and changed generations never become ready")
{
	Temp tmp;
	Cache cache(tmp.path);
	Tiny t;
	t.seed(cache);
	{
		FileLock lock(cache.revision_lock(t.manifest.key), false, true);
		cache.prepare(t.manifest);
	}
	CHECK_FALSE(cache.acquire(t.manifest));
	atomic_write(cache.staging(t.manifest.key) / "graphs/tiny.weights", "xxxx");
	CHECK_THROWS_AS(cache.publish(t.manifest), Failure);
	CHECK_FALSE(cache.acquire(t.manifest));
	atomic_write(cache.staging(t.manifest.key) / "graphs/tiny.weights", t.weights);
	cache.publish(t.manifest);
	CHECK(cache.acquire(t.manifest));
	atomic_write(cache.revision(t.manifest.key) / "graphs/tiny.weights", "xxxx");
	CHECK_THROWS_AS(cache.acquire(t.manifest), Failure);
}
TEST_CASE("Metadata readers see complete generations during atomic replacement")
{
	Temp temp;
	auto path = temp.path / "receipt.json";
	atomic_write(path, "old");
	std::atomic<bool> done{false};
	auto writer = std::async(std::launch::async, [&] {
		try {
			for (unsigned i = 0; i < 100; ++i)
				atomic_write(path, i % 2 ? "old" : "successor");
		} catch (...) {
			done = true;
			throw;
		}
		done = true;
	});
	for (unsigned i = 0; i < 200 || !done; ++i) {
		auto bytes = read_small_file(path);
		CHECK((bytes == "old" || bytes == "successor"));
	}
	writer.get();
}
TEST_CASE("Concurrent instances share one authenticated payload and failed hashes never finalize")
{
	Temp tmp;
	Cache cache(tmp.path);
	Tiny t;
	AcceptanceStore accepted(cache);
	accepted.accept(t.manifest, t.licenses, t.manifest.licences);
	auto http = std::make_shared<MemoryHttp>();
	auto &a = t.manifest.artifacts.at("test-source");
	http->files[a.url] = t.weights;
	DownloadEngine a_engine(cache, http, fast_options()), b_engine(cache, http, fast_options());
	auto first = std::async(std::launch::async, [&] { a_engine.fetch(t.manifest, a, t.licenses, accepted); });
	auto second = std::async(std::launch::async, [&] { b_engine.fetch(t.manifest, a, t.licenses, accepted); });
	first.get();
	second.get();
	CHECK(http->calls == 1);
	fs::remove(cache.blob(a.sha256));
	http->files[a.url] = "xxxx";
	CHECK_THROWS_AS(a_engine.fetch(t.manifest, a, t.licenses, accepted), Failure);
	CHECK_FALSE(fs::exists(cache.blob(a.sha256)));
}
TEST_CASE("ModelCatalog binds accepted downloads and publishes a verified revision")
{
	Temp tmp;
	Cache cache(tmp.path);
	std::string header = R"({"bias":{"dtype":"F32","shape":[1],"data_offsets":[0,4]}})";
	header.append((8 - header.size() % 8) % 8, ' ');
	std::string source(8, '\0');
	for (unsigned i = 0; i < 8; ++i)
		source[i] = static_cast<char>((std::uint64_t(header.size()) >> (8 * i)) & 255);
	source += header + "1234";
	Tiny t(source);
	auto http = std::make_shared<MemoryHttp>();
	http->files = {{"https://example.invalid/graph", t.graph},
		       {"https://example.invalid/metadata", t.metadata},
		       {"https://example.invalid/source", source}};
	CatalogueService service(cache, t.licenses, {t.manifest}, {}, http, fast_options());
	CHECK(service.request_install(t.manifest.key, {}, {}).code == ErrorCode::licence_required);
	CHECK(http->calls == 0);
	CHECK_FALSE(service.request_install(t.manifest.key, t.manifest.licences, {}));
	auto deadline = Clock::now() + std::chrono::seconds(5);
	while (Clock::now() < deadline && service.snapshot()[0].state != ModelState::ready &&
	       service.snapshot()[0].state != ModelState::failed)
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	CHECK(service.snapshot()[0].state == ModelState::ready);
	CHECK(service.acquire(t.manifest.key));
	CHECK(http->calls == 3);
}
TEST_CASE("W05 handoff verifies all staged outputs before publishing")
{
	Temp tmp;
	Cache cache(tmp.path);
	Tiny t;
	auto http = std::make_shared<MemoryHttp>();
	http->files = {{"https://example.invalid/graph", t.graph},
		       {"https://example.invalid/metadata", t.metadata},
		       {"https://example.invalid/source", t.weights}};
	CatalogueService service(
		cache, t.licenses, {t.manifest},
		[weights = t.weights](const Manifest &m, Cache &c, std::stop_token) {
			atomic_write(c.staging(m.key) / "graphs/tiny.weights", weights);
		},
		http, fast_options());
	CHECK_FALSE(service.request_install(t.manifest.key, t.manifest.licences, {}));
	auto deadline = Clock::now() + std::chrono::seconds(5);
	while (Clock::now() < deadline && service.snapshot()[0].state != ModelState::ready &&
	       service.snapshot()[0].state != ModelState::failed)
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	CHECK(service.snapshot()[0].state == ModelState::ready);
	CHECK(service.acquire(t.manifest.key));
}
namespace {
class FixtureHttp final : public HttpClient {
	std::string base_;
	std::shared_ptr<HttpClient> client_ = system_http_client(true);

public:
	explicit FixtureHttp(std::string base) : base_(std::move(base)) {}
	void get(const HttpRequest &r, const HeaderSink &head, const ByteSink &sink, std::stop_token stop) override
	{
		auto copy = r;
		require(copy.url.starts_with("https://fixture.invalid/"), "Unexpected fixture origin");
		copy.url = base_ + copy.url.substr(std::string("https://fixture.invalid").size());
		client_->get(copy, head, sink, stop);
	}
};
std::string network_payload()
{
	std::string data(256 * 1024, '\0');
	for (std::size_t i = 0; i < data.size(); ++i)
		data[i] = static_cast<char>(i % 251);
	return data;
}
int http_case(std::string base, std::string mode, fs::path root)
{
	Cache cache(root);
	Tiny t(network_payload(), "https://fixture.invalid/" + mode);
	AcceptanceStore store(cache);
	auto a = t.manifest.artifacts.at("test-source");
	if (mode != "unaccepted")
		store.accept(t.manifest, t.licenses, t.manifest.licences);
	auto options = fast_options();
	std::stop_source cancellation;
	unsigned waits = 0;
	options.wait = [&](std::chrono::milliseconds delay, std::stop_token stop) {
		++waits;
		if (mode == "rate")
			require(delay == std::chrono::seconds(2), "Retry-After was not honored");
		if (mode == "cancel-backoff") {
			cancellation.request_stop();
			return false;
		}
		return !stop.stop_requested();
	};
	if (mode == "disk")
		options.check_space = [](std::uint64_t) {
			throw Failure(ErrorCode::disk_full, "Simulated full disk");
		};
	if (mode == "disk-mid") {
		auto checks = std::make_shared<unsigned>(0);
		options.check_space = [checks](std::uint64_t) {
			if (++*checks >= 4)
				throw Failure(ErrorCode::disk_full, "Simulated midstream disk full");
		};
	}
	if (mode == "resume" || mode == "ignore" || mode == "changed" || mode == "bad-range" || mode == "range416" ||
	    mode == "no-validator") {
		auto partial = cache.path("partial/" + a.sha256 + ".partial");
		atomic_write(partial, network_payload().substr(0, 65536));
		atomic_write(cache.path("partial/" + a.sha256 + ".json"), Json{{"schema_version", 1},
									       {"url_sha256", sha256(a.url)},
									       {"sha256", a.sha256},
									       {"bytes", a.bytes},
									       {"validator", "\"v1\""}}
										  .dump());
	}
	DownloadEngine engine(cache, std::make_shared<FixtureHttp>(base), options);
	try {
		engine.fetch(t.manifest, a, t.licenses, store, cancellation.get_token(), [&](Progress p) {
			if (mode == "cancel" && p.completed_bytes >= 65536)
				cancellation.request_stop();
		});
		require(mode != "hash" && mode != "disk" && mode != "disk-mid" && mode != "cancel" &&
				mode != "cancel-backoff" && mode != "unaccepted" && mode != "bad-range" &&
				mode != "downgrade" && mode != "redirect-loop",
			"Expected failure did not occur");
		require(cache.has_blob(a), "Downloaded blob not authenticated");
		if (mode == "rate" || mode == "loss" || mode == "changed" || mode == "range416" ||
		    mode == "no-validator")
			require(waits >= 1, "Expected restart/backoff");
	} catch (const Failure &f) {
		ErrorCode expected = ErrorCode::none;
		if (mode == "hash")
			expected = ErrorCode::hash_mismatch;
		else if (mode == "disk" || mode == "disk-mid")
			expected = ErrorCode::disk_full;
		else if (mode == "cancel" || mode == "cancel-backoff")
			expected = ErrorCode::cancelled;
		else if (mode == "unaccepted")
			expected = ErrorCode::licence_required;
		else if (mode == "bad-range" || mode == "redirect-loop")
			expected = ErrorCode::io_failure;
		else if (mode == "downgrade")
			expected = ErrorCode::invalid_input;
		require(f.code == expected, "Unexpected HTTP fixture failure: " + std::string(f.what()));
		require(!fs::exists(cache.blob(a.sha256)), "Failed payload finalized");
		if (mode == "cancel") {
			require(fs::file_size(cache.path("partial/" + a.sha256 + ".partial")) > 0,
				"Cancellation lost partial");
			engine.fetch(t.manifest, a, t.licenses, store);
			require(cache.has_blob(a), "Cancelled download did not resume");
		}
	}
	return 0;
}
int offline_case(const fs::path &bundle, const fs::path &root, bool fail)
{
	Cache cache(root);
	Tiny t;
	AcceptanceStore store(cache);
	store.accept(t.manifest, t.licenses, t.manifest.licences);
	DownloadEngine engine(cache, std::make_shared<MemoryHttp>(), fast_options());
	bool failed = false;
	try {
		engine.import_offline(t.manifest, bundle, t.licenses, store);
		for (auto &[id, a] : t.manifest.artifacts)
			require(cache.has_blob(a), "Incomplete offline import");
	} catch (const Failure &) {
		if (!fail)
			throw;
		failed = true;
	}
	require(failed == fail, "Offline fixture result mismatch");
	return 0;
}
} // namespace
int main(int argc, char **argv)
{
	try {
		if (argc == 5 && std::string(argv[1]) == "--http-case")
			return http_case(argv[2], argv[3], argv[4]);
		if (argc == 5 && std::string(argv[1]) == "--offline-case")
			return offline_case(argv[2], argv[3], std::string(argv[4]) == "fail");
		if (argc == 3 && std::string(argv[1]) == "--write-fixture") {
			Tiny t;
			auto dir = fs::path(argv[2]);
			fs::create_directories(dir / "blobs");
			atomic_write(dir / "manifest.json", t.manifest.raw);
			atomic_write(dir / "blobs" / t.manifest.artifacts.at("graph").sha256, t.graph);
			atomic_write(dir / "blobs" / t.manifest.artifacts.at("metadata").sha256, t.metadata);
			atomic_write(dir / "blobs" / t.manifest.artifacts.at("test-source").sha256, t.weights);
			return 0;
		}
		if (argc == 3 && std::string(argv[1]) == "--hold-lock") {
			FileLock lock(fs::path(argv[2]), false, true);
			std::cout << "READY" << std::endl;
			std::string line;
			std::getline(std::cin, line);
			return 0;
		}
		if (argc == 3 && std::string(argv[1]) == "--try-lock") {
			FileLock lock(fs::path(argv[2]), false, false);
			return lock.owns_lock() ? 0 : 3;
		}
		if (argc == 3 && std::string(argv[1]) == "--hold-lease") {
			Cache cache(argv[2]);
			Tiny t;
			t.publish(cache);
			auto lease = cache.acquire(t.manifest);
			require(bool(lease), "Lease missing");
			std::cout << "READY" << std::endl;
			std::string line;
			std::getline(std::cin, line);
			return 0;
		}
		if (argc == 3 && std::string(argv[1]) == "--remove-model") {
			Cache cache(argv[2]);
			Tiny t;
			return cache.remove(t.manifest.key) ? 0 : 3;
		}
		if (argc == 3 && std::string(argv[1]) == "--validate-export") {
			auto sources = parse_source_inventory(
				read_small_file(fs::path(CK_SOURCE_DIR) / "data/models/sources-v1.json"));
			for (auto name : {"moge_256x512", "moge_512x512", "rvm_256x512", "rvm_512x512",
					  "stage1_256x512", "stage1_512x512", "stage2_256x512", "stage2_512x512",
					  "refiner_b256", "refiner_b384", "refiner_b512", "refiner_b768"}) {
				auto raw = read_small_file(fs::path(argv[2]) / (std::string(name) + ".json"));
				auto j = strict_json(raw);
				Manifest m;
				m.artifacts = sources;
				m.artifacts["graph"].path = std::string(name) + ".onnx";
				Graph g;
				g.graph = "graph";
				g.inputs = j.at("inputs");
				g.outputs = j.at("outputs");
				for (auto &[id, source] : j.at("sources").items())
					g.sources.push_back(id);
				for (auto &w : j.at("weights"))
					g.weights_bytes =
						std::max(g.weights_bytes,
							 checked_add(uint64(w.at("offset")), uint64(w.at("length"))));
				validate_export_metadata(raw, g, m);
				std::cout << "PASS " << name << '\n';
			}
			return 0;
		}
		if (argc == 4 && std::string(argv[1]) == "--verify-catalogue") {
			auto signature = strict_json(read_small_file(argv[3]));
			auto bytes = read_small_file(argv[2]);
			auto c = parse_catalogue(bytes, string(signature.at("signature")),
						 string(signature.at("public_xy")), 1);
			require(c.sequence == 1 && c.entries.size() == 1, "Unexpected signed fixture");
			(void)parse_manifest(
				read_small_file(fs::path(CK_SOURCE_DIR) / "tests/models/fixtures/package-v1.json"),
				c.entries[0]);
			Temp temp;
			Cache cache(temp.path);
			(void)admit_catalogue(cache, bytes, string(signature.at("signature")),
					      string(signature.at("public_xy")));
			require(uint64(strict_json(read_small_file(cache.path("receipts/catalogue.json")))
					       .at("sequence")) == 1,
				"Catalogue floor was not persisted");
			try {
				(void)parse_catalogue(bytes + " ", string(signature.at("signature")),
						      string(signature.at("public_xy")), 1);
				return 1;
			} catch (const Failure &) {
			}
			try {
				(void)parse_catalogue(bytes, string(signature.at("signature")),
						      string(signature.at("public_xy")), 2);
				return 1;
			} catch (const Failure &) {
			}
			return 0;
		}
		doctest::Context context;
		context.applyCommandLine(argc, argv);
		return context.run();
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
