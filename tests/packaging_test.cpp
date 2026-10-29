// SPDX-License-Identifier: GPL-3.0-or-later
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "packaging/runtime_install.hpp"
#include <cstdlib>

using namespace corridorkey;
using namespace corridorkey::models;
namespace {
std::string catalogue()
{
	Json licences = {{"schema_version", 1}, {"licenses", Json::array()}};
	return Json{{"schema_version", 1},
		    {"licenses", licences},
		    {"bundles", {{"disabled-runtime", {{"enabled", false}, {"reason", "Missing vendor notices"}}}}}}
		.dump();
}
} // namespace
TEST_CASE("runtime catalogues require the installed executable trust anchor")
{
	auto raw = catalogue();
	CHECK_THROWS_WITH(packaging::runtime_package(raw, std::string(64, '0'), "disabled-runtime"),
			  "Runtime catalogue differs from the installed executable");
	CHECK_THROWS_WITH(packaging::runtime_package(raw, sha256(raw), "disabled-runtime"), "Missing vendor notices");
	CHECK_THROWS(packaging::runtime_package(raw, sha256(raw), "not-catalogued"));
}
TEST_CASE("runtime installation refuses to start before exact-text acceptance")
{
	const std::string text = "Independently authored runtime test licence.";
	const auto hash = sha256(text);
	packaging::RuntimePackage package;
	package.download.key = {"test-runtime", std::string(64, 'a')};
	package.download.digest = std::string(64, 'b');
	package.download.licences = {{"test-license", hash}};
	package.licences = LicenceRegistry::parse(Json{
		{"schema_version", 1},
		{"licenses",
		 Json::array({{{"id", "test-license"},
			       {"text", text},
			       {"text_sha256", hash},
			       {"source_url", "https://example.invalid/test-license"},
			       {"attribution", "Test fixture"},
			       {"scope", "test"}}})}}.dump());
	auto root = fs::current_path() / "runtime-acceptance-test";
	Cache cache(root);
	AcceptanceStore accepted(cache);
	CHECK_FALSE(accepted.contains(package.download, package.licences));
	CHECK_THROWS_WITH(packaging::install_runtime(package, cache, accepted),
			  "Accept all runtime licences before downloading");
	CHECK_FALSE(fs::exists(root / "bundles"));
}
TEST_CASE("vendor ZIP extraction selects exact members and checks sizes and CRC")
{
	const char *fixture = std::getenv("CK_RUNTIME_ZIP_FIXTURE");
	if (!fixture)
		return;
	auto root = fs::path(fixture);
	packaging::extract_runtime_member(root / "valid.zip", "vendor/bin/library.dll", root / "output.dll", 100000);
	CHECK(fs::file_size(root / "output.dll") == 100000);
	packaging::extract_runtime_member(root / "valid.zip", "large.dll", root / "large.dll", 2 * 1024 * 1024);
	CHECK(fs::file_size(root / "large.dll") == 2 * 1024 * 1024);
	CHECK_THROWS(packaging::extract_runtime_member(root / "valid.zip", "vendor/bin/library.dll",
						       root / "bad-size.dll", 99999));
	CHECK_THROWS(packaging::extract_runtime_member(root / "valid.zip", "missing.dll", root / "missing.dll", 1));
	CHECK_THROWS(packaging::extract_runtime_member(root / "duplicate.zip", "vendor/bin/library.dll",
						       root / "duplicate.dll", 100000));
	CHECK_THROWS(packaging::extract_runtime_member(root / "corrupt.zip", "vendor/bin/library.dll",
						       root / "corrupt.dll", 100000));
	CHECK_FALSE(fs::exists(root.parent_path() / "escape.dll"));
}
