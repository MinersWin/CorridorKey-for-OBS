// SPDX-License-Identifier: GPL-3.0-or-later
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "ui/tiny_fixture.hpp"
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
	REQUIRE(rows.size() == 5);
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
	for (auto forbidden : {"Private", "token", "private camera", "private-log", "C:\\"})
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
TEST_CASE("a build without W05 stops with a useful error and cannot publish an incomplete package")
{
	Temp temp;
	auto m = testing::manifest(fixture());
	ManagerController manager(temp.path, testing::licence_registry(), model_rows({m}, {}));
	manager.install(m.key, true, fixture());
	REQUIRE(until([&] { return manager.snapshot().rows[0].status.failure.code == ErrorCode::unsupported; }));
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
