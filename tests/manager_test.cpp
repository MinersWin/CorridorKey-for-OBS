// SPDX-License-Identifier: GPL-3.0-or-later
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "ui/tiny_fixture.hpp"
#include "models/activation.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>

using namespace corridorkey;
using namespace corridorkey::ui;
namespace {
models::fs::path fixture()
{
	return models::fs::current_path() / "manager-fixture";
}
struct Temp {
	models::fs::path path = models::fs::current_path() /
				("manager-test-" + std::to_string(Clock::now().time_since_epoch().count()));
	~Temp()
	{
		std::error_code ec;
		models::fs::remove_all(path, ec);
	}
};
template<typename Predicate> bool until(Predicate predicate)
{
	const auto end = Clock::now() + std::chrono::seconds(8);
	do {
		if (predicate())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	} while (Clock::now() < end);
	return false;
}
struct NoNetwork final : models::HttpClient {
	std::atomic<int> calls{0};
	void get(const models::HttpRequest &, const models::HeaderSink &, const models::ByteSink &,
		 std::stop_token) override
	{
		++calls;
		throw models::Failure(ErrorCode::io_failure, "Network is disabled in this test");
	}
};
} // namespace
TEST_CASE("every lifecycle state has deliberate actions; cancellation waits for its safe boundary")
{
	ModelRow row;
	row.manifest = models::Manifest{};
	for (auto state :
	     {ModelState::missing, ModelState::awaiting_acceptance, ModelState::failed, ModelState::update_available}) {
		row.status.state = state;
		CHECK(actions(row).install);
		CHECK(actions(row).import);
		CHECK_FALSE(actions(row).cancel);
	}
	for (auto state : {ModelState::downloading, ModelState::verifying, ModelState::binding, ModelState::building,
			   ModelState::warming}) {
		row.status.state = state;
		CHECK_FALSE(actions(row).install);
		CHECK(actions(row).cancel);
		CHECK_FALSE(actions(row).remove);
		CHECK_FALSE(state_name(state).empty());
	}
	row.status.progress.can_cancel_now = false;
	CHECK_FALSE(actions(row).cancel);
	row.status.progress.can_cancel_now = true;
	row.cancelling = true;
	CHECK_FALSE(actions(row).cancel);
	row.status.state = ModelState::ready;
	row.installed_bytes = 4;
	CHECK(actions(row).remove);
	CHECK_FALSE(actions(row).install);
	row.manifest.reset();
	row.status.state = ModelState::missing;
	CHECK_FALSE(actions(row).install);
	CHECK_FALSE(actions(row).import);
}
TEST_CASE("download rate resets on retry and handles unknown totals without arithmetic underflow")
{
	ModelRow row;
	row.key = {"test", "revision"};
	ViewModel view({row});
	const auto start = Clock::now();
	view.apply({{row.key, ModelState::downloading, {100, {}, true}, {}}}, start);
	view.apply({{row.key, ModelState::downloading, {300, {}, true}, {}}}, start + std::chrono::seconds(2));
	CHECK(view.rows()[0].bytes_per_second == doctest::Approx(100));
	view.apply({{row.key, ModelState::downloading, {0, {}, true}, {}}}, start + std::chrono::seconds(3));
	CHECK(view.rows()[0].bytes_per_second == 0);
	view.apply({{row.key, ModelState::failed, {}, error(ErrorCode::cancelled)}}, start + std::chrono::seconds(4));
	CHECK(view.rows()[0].bytes_per_second == 0);
	CHECK(view.first_run());
	view.rows()[0].installed_bytes = 4;
	CHECK_FALSE(view.first_run());
}
TEST_CASE("source inventory provides honest sizes and no future download slot")
{
	auto sources = models::parse_source_inventory(
		models::read_small_file(models::fs::path(CK_SOURCE_DIR) / "data/models/sources-v1.json"));
	auto rows = model_rows({}, sources);
	REQUIRE(rows.size() == 4);
	CHECK(rows[0].recommended);
	CHECK(rows[0].download_bytes == 3282961820ULL);
	for (auto &row : rows) {
		CHECK_FALSE(row.manifest);
		CHECK_FALSE(actions(row).install);
	}
}
TEST_CASE("diagnostics exclude local text, cache paths and arbitrary failure details")
{
	ManagerSnapshot s;
	ModelRow row;
	row.key = {"valid-id", std::string(40, '1')};
	row.name = "private camera";
	row.description = "C:\\Users\\Private\\token.txt";
	row.status.failure = {ErrorCode::io_failure, "secret-token"};
	s.notice = "private-log";
	s.rows.push_back(row);
	auto text = diagnostics(s, "0.1", "33.0", "6.6", "windows");
	for (auto forbidden : {"Private", "secret-token", "private camera", "private-log", "C:\\"})
		CHECK(text.find(forbidden) == std::string::npos);
	CHECK(text.find("<model-cache>") != std::string::npos);
}
TEST_CASE("licence refusal starts no transfer; accepted folder import publishes and leases prevent removal")
{
	Temp temp;
	auto m = testing::manifest(fixture());
	auto http = std::make_shared<NoNetwork>();
	ManagerController manager(temp.path, testing::licence_registry(), model_rows({m}, {}), testing::binder(), http);
	manager.install(m.key, false, fixture());
	REQUIRE(until([&] { return manager.snapshot().rows[0].status.failure.code == ErrorCode::licence_required; }));
	CHECK(http->calls == 0);
	CHECK_FALSE(models::fs::exists(temp.path / "receipts" / (m.digest + ".json")));
	manager.install(m.key, true, fixture());
	REQUIRE(until([&] { return manager.snapshot().rows[0].status.state == ModelState::ready; }));
	CHECK(http->calls == 0);
	models::Cache cache(temp.path);
	CHECK(models::AcceptanceStore(cache).contains(m, testing::licence_registry()));
	auto reader = cache.acquire(m);
	REQUIRE(reader);
	manager.remove(m.key);
	REQUIRE(until([&] { return !manager.snapshot().notice.empty(); }));
	CHECK(models::fs::exists(cache.revision(m.key) / "ready.json"));
	reader.reset();
	manager.remove(m.key);
	REQUIRE(until([&] { return !models::fs::exists(cache.revision(m.key)); }));
}
TEST_CASE("cancelling the binder never publishes; retry uses the existing receipt and verified blobs")
{
	Temp temp;
	auto m = testing::manifest(fixture());
	ManagerController manager(temp.path, testing::licence_registry(), model_rows({m}, {}),
				  testing::binder(std::chrono::seconds(2)));
	manager.install(m.key, true, fixture());
	REQUIRE(until([&] { return manager.snapshot().rows[0].status.state == ModelState::binding; }));
	manager.cancel(m.key);
	REQUIRE(until([&] { return manager.snapshot().rows[0].status.failure.code == ErrorCode::cancelled; }));
	CHECK_FALSE(models::fs::exists(temp.path / "packages" / m.key.id / m.key.revision / "ready.json"));
	manager.install(m.key, false, fixture());
	REQUIRE(until([&] { return manager.snapshot().rows[0].status.state == ModelState::ready; }));
}
TEST_CASE("the native binder rejects malformed containers and cannot publish an incomplete package")
{
	Temp temp;
	auto m = testing::manifest(fixture());
	ManagerController manager(temp.path, testing::licence_registry(), model_rows({m}, {}));
	manager.install(m.key, true, fixture());
	REQUIRE(until([&] { return manager.snapshot().rows[0].status.state == ModelState::failed; }));
	CHECK_FALSE(models::fs::exists(temp.path / "packages" / m.key.id / m.key.revision / "ready.json"));
}
TEST_CASE("a new catalogue revision retains verified installed revisions and offers an update")
{
	Temp temp;
	auto old = testing::manifest(fixture());
	{
		ManagerController initial(temp.path, testing::licence_registry(), model_rows({old}, {}),
					  testing::binder());
		initial.install(old.key, true, fixture());
		REQUIRE(until([&] { return initial.snapshot().rows[0].status.state == ModelState::ready; }));
	}
	auto json = models::strict_json(old.raw);
	json["revision"] = std::string(40, '2');
	auto raw = json.dump();
	auto updated = models::parse_manifest(raw, models::sha256(raw));
	ManagerController manager(temp.path, testing::licence_registry(), model_rows({updated}, {}), testing::binder());
	REQUIRE(until([&] { return manager.snapshot().rows[0].status.state == ModelState::update_available; }));
	auto snapshot = manager.snapshot();
	auto previous = std::find_if(snapshot.rows.begin(), snapshot.rows.end(),
				     [&](const auto &r) { return r.key == old.key; });
	REQUIRE(previous != snapshot.rows.end());
	CHECK(previous->status.state == ModelState::ready);
	CHECK(previous->installed_bytes > 0);
	CHECK_FALSE(previous->catalogued);
	CHECK_FALSE(actions(*previous).install);
	CHECK_FALSE(actions(*previous).import);
	CHECK(actions(*previous).remove);
	CHECK_FALSE(ViewModel(snapshot.rows).first_run());
	CHECK(actions(snapshot.rows[0]).primary == "Review update");
}

TEST_CASE("production catalogue is pinned, licence-complete and selects packages by grid and stage")
{
	const auto root = models::fs::path(CK_SOURCE_DIR) / "data/models";
	const auto bytes = models::read_small_file(root / "catalogue-v1.json");
	const auto catalogue = models::parse_installer_catalogue(bytes, models::sha256(bytes), 0);
	CHECK_THROWS(models::parse_catalogue(bytes, "", "", 0));
	CHECK_THROWS(models::parse_installer_catalogue(bytes + " ", models::sha256(bytes), 0));
	CHECK_THROWS(models::parse_installer_catalogue(bytes, models::sha256(bytes), catalogue.sequence + 1));
	auto licences = models::LicenceRegistry::parse(models::read_small_file(root / "licenses-v1.json"));
	std::vector<models::Manifest> manifests;
	for (const auto &pin : catalogue.entries) {
		auto manifest = models::parse_manifest(models::read_small_file(root / pin.manifest.path), pin);
		for (const auto &licence : manifest.licences)
			(void)licences.at(licence);
		CHECK((manifest.key.revision == "5d88114f169cb082cda238426af47feffb9724af" ||
		       manifest.key.revision == "082f53c75d275c10b1ddf3be2cb72c5ca559f30b"));
		manifests.push_back(std::move(manifest));
	}
	REQUIRE(manifests.size() == 6);
	Settings refined;
	for (const auto budget : {256u, 384u, 512u, 768u}) {
		refined.quality.refiner_max_tiles = budget;
		const auto *manifest = models::activation_refiner_manifest(manifests, refined);
		REQUIRE(manifest);
		CHECK(manifest->key.id == "refiner-v12");
	}
	refined.quality.refiner_max_tiles = 123;
	CHECK_FALSE(models::activation_refiner_manifest(manifests, refined));
	refined.quality.refiner_max_tiles = 384;
	refined.quality.refiner_enabled = false;
	CHECK_FALSE(models::activation_refiner_manifest(manifests, refined));
	refined.quality.refiner_enabled = true;
	refined.quality.model = ModelFamily::ck1;
	CHECK_FALSE(models::activation_refiner_manifest(manifests, refined));
	Temp admission;
	models::Cache cache(admission.path);
	CHECK(models::admit_installer_catalogue(cache, bytes, models::sha256(bytes)).sequence == catalogue.sequence);
	auto conflicting = models::strict_json(bytes);
	conflicting["$comment"] = "Different bytes at the same sequence";
	const auto conflict = conflicting.dump();
	CHECK_THROWS(models::admit_installer_catalogue(cache, conflict, models::sha256(conflict)));
	const auto release = models::strict_json(
		models::read_small_file(models::fs::path(CK_SOURCE_DIR) / "tests/fixtures/ck2-release-r3.json"));
	const auto ck1_release = models::strict_json(
		models::read_small_file(models::fs::path(CK_SOURCE_DIR) / "tests/fixtures/ck1-release-r4.json"));
	for (const auto &m : manifests)
		for (const auto &[id, a] : m.artifacts) {
			(void)id;
			if (a.format == "onnx" || a.format == "export-json")
				CHECK(a.sha256 ==
				      models::string((m.key.revision == "082f53c75d275c10b1ddf3be2cb72c5ca559f30b"
							      ? ck1_release
							      : release)
							     .at("sha256")
							     .at(models::fs::path(a.path).filename().string())));
		}
	Settings ck1;
	ck1.quality.model = ModelFamily::ck1;
	ck1.quality.grid = {384, 384};
	ck1.hint.source = HintSource::modnet;
	for (auto size : {256U, 384U, 512U}) {
		ck1.quality.grid = {size, size};
		CHECK(models::activation_manifest(manifests, ck1)->key.id == "ck1-green");
		ck1.quality.screen_color = ScreenColor::blue;
		CHECK(models::activation_manifest(manifests, ck1)->key.id == "ck1-blue");
		ck1.quality.screen_color = ScreenColor::green;
		CHECK(models::activation_hint_manifest(manifests, ck1)->key.id == "modnet");
		ck1.hint.source = HintSource::ck2_stage1;
		CHECK(models::activation_hint_manifest(manifests, ck1)->key.id == "ck2-balanced");
		ck1.hint.source = HintSource::modnet;
	}
	ck1.hint.source = HintSource::chroma;
	CHECK_FALSE(models::activation_hint_manifest(manifests, ck1));
	ck1.hint.source = HintSource::external_mask;
	CHECK_FALSE(models::activation_hint_manifest(manifests, ck1));
	Settings settings;
	REQUIRE(models::activation_manifest(manifests, settings));
	CHECK(models::activation_manifest(manifests, settings)->key.id == "ck2-balanced");
	settings.quality.profile = Profile::quality;
	CHECK(models::activation_manifest(manifests, settings)->key.id == "ck2-balanced");
	settings.quality.profile = Profile::facecam;
	settings.quality.grid = {512, 256};
	CHECK(models::activation_manifest(manifests, settings)->key.id == "ck2-facecam");
	settings.quality.profile = Profile::quality;
	CHECK_FALSE(models::activation_manifest(manifests, settings));
	settings.quality.grid = {1024, 1024};
	CHECK_FALSE(models::activation_manifest(manifests, settings));
	settings.quality.grid = {512, 512};
	settings.quality.model_revision = std::string(40, '1');
	CHECK_FALSE(models::activation_manifest(manifests, settings));
}
TEST_CASE("refined activation retains both package leases and identifies both revisions")
{
	struct Lease final : ModelReadLease {};
	auto base = std::make_shared<ReadyModel>();
	auto refinement = std::make_shared<ReadyModel>();
	base->lease = std::make_shared<Lease>();
	refinement->lease = std::make_shared<Lease>();
	std::weak_ptr<const ModelReadLease> base_lease = base->lease, refiner_lease = refinement->lease;
	base->manifest_sha256 = std::string(64, '1');
	refinement->manifest_sha256 = std::string(64, '2');
	base->graphs.push_back({"stage1nomask_256x512", {}, {}, {}, {}, {}});
	refinement->graphs.push_back({"refiner_b384", {}, {}, {}, {}, {}});
	auto combined = models::activation_model(base, refinement);
	CHECK(combined->graphs.size() == 2);
	CHECK(combined->manifest_sha256 != base->manifest_sha256);
	refinement->manifest_sha256 = std::string(64, '3');
	CHECK(models::activation_model(base, refinement)->manifest_sha256 != combined->manifest_sha256);
	base.reset();
	refinement.reset();
	CHECK_FALSE(base_lease.expired());
	CHECK_FALSE(refiner_lease.expired());
	combined.reset();
	CHECK(base_lease.expired());
	CHECK(refiner_lease.expired());
}
TEST_CASE("engine preparation is observable, cancellable and scoped to its filter lifetime")
{
	const ModelKey key{"ck2-balanced", std::string(40, '1')};
	std::stop_source first, second;
	preparation_state(901, key, ModelState::warming, first);
	auto tasks = preparation_tasks();
	REQUIRE(tasks.size() == 1);
	const auto started = tasks[0].started;
	preparation_state(901, key, ModelState::building, first);
	REQUIRE(preparation_tasks().size() == 1);
	CHECK(preparation_tasks()[0].started == started);
	CHECK(preparation_tasks()[0].status.state == ModelState::building);
	CHECK_FALSE(preparation_tasks()[0].status.progress.total_bytes);
	preparation_state(902, key, ModelState::building, second);
	cancel_preparation(key);
	CHECK(first.stop_requested());
	CHECK(second.stop_requested());
	preparation_finished(901);
	preparation_finished(902);
	CHECK(preparation_tasks().empty());
}

TEST_CASE("manager uses the native binder when no adapter is registered")
{
	Temp temp;
	auto original = testing::manifest(fixture());
	auto j = models::strict_json(original.raw);
	auto header = models::Json{{"bias", {{"dtype", "F32"}, {"shape", {1}}, {"data_offsets", {0, 4}}}}}.dump();
	while (header.size() % 8)
		header += ' ';
	std::string source(8, '\0');
	for (unsigned n = 0; n < 8; ++n)
		source[n] = static_cast<char>((static_cast<std::uint64_t>(header.size()) >> (8 * n)) & 255);
	source += header + "1234";
	const auto &g = original.graphs.front();
	auto metadata = models::strict_json(
		models::read_small_file(fixture() / "blobs" / original.artifacts.at(g.metadata).sha256));
	metadata["sources"]["test-source"]["size"] = source.size();
	metadata["sources"]["test-source"]["sha256"] = models::sha256(source);
	const auto meta = metadata.dump();
	for (auto &a : j["artifacts"]) {
		const auto id = a.at("id").get<std::string>();
		if (id != "test-source" && id != g.metadata)
			continue;
		const auto &bytes = id == "test-source" ? source : meta;
		a["bytes"] = bytes.size();
		a["sha256"] = models::sha256(bytes);
		a["origin"]["revision"] = models::sha256(bytes);
	}
	const auto raw = j.dump();
	auto m = models::parse_manifest(raw, models::sha256(raw));
	auto offline = temp.path / "offline";
	models::fs::create_directories(offline / "blobs");
	for (const auto &[id, a] : m.artifacts) {
		const auto bytes = id == "test-source" ? source
				   : id == g.metadata  ? meta
						       : models::read_small_file(fixture() / "blobs" / a.sha256);
		models::atomic_write(offline / "blobs" / a.sha256, bytes);
	}
	ManagerController manager(temp.path / "cache", testing::licence_registry(), model_rows({m}, {}));
	manager.install(m.key, true, offline);
	REQUIRE(until([&] { return manager.snapshot().rows[0].status.state == ModelState::ready; }));
	auto leased = models::Cache(temp.path / "cache").acquire(m);
	REQUIRE(leased);
	CHECK(models::read_small_file(leased->graphs.front().weights_path) == "1234");
	models::Cache(temp.path / "cache").select_current(m);
	CHECK_FALSE(models::Cache(temp.path / "cache").remove(m.key, true));
	CHECK(models::Cache(temp.path / "cache").current(m.key.id) == std::optional<ModelKey>{m.key});
	const auto metadata_path = models::Cache(temp.path / "cache").revision(m.key) / m.artifacts.at(g.metadata).path;
	models::atomic_write(metadata_path, meta + " ");
	CHECK_THROWS(models::Cache(temp.path / "cache").acquire(m));
	CHECK_FALSE(models::Cache(temp.path / "cache").remove(m.key));
	leased.reset();
	CHECK(models::Cache(temp.path / "cache").remove(m.key, true));
	CHECK_FALSE(models::Cache(temp.path / "cache").current(m.key.id));
}
