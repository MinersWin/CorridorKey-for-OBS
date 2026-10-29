// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "models/binder.hpp"
#include <fstream>
#include <cstdlib>
#include "util/utf8_path.hpp"
using namespace corridorkey;
using namespace corridorkey::models;

namespace {
Json zeros(std::uint64_t bytes)
{
	Json row = {{"name", "z"},
		    {"dtype", "float32"},
		    {"shape", Json::array({bytes / 4})},
		    {"offset", 0},
		    {"length", bytes},
		    {"source", {{"file", nullptr}, {"key", nullptr}, {"ops", Json::array({Json::array({"zeros"})})}}}};
	return {{"graph", "api-test"},       {"opset", 18},
		{"inputs", Json::array()},   {"outputs", Json::array()},
		{"sources", Json::object()}, {"weights", Json::array({row})}};
}
struct Fixture {
	fs::path dir = fs::current_path() / "binder-api";
	fs::path output = dir / "api-test.weights";
	Fixture() { fs::create_directories(dir); }
	~Fixture()
	{
		for (auto suffix : {"", ".partial", ".tensor.tmp", ".ready.json", ".ready.json.partial", ".lock"}) {
			auto path = output;
			path += suffix;
			std::error_code ec;
			fs::remove(path, ec);
		}
	}
	bool exists(std::string_view suffix) const
	{
		auto path = output;
		path += suffix;
		return fs::exists(path);
	}
};
} // namespace
TEST_CASE("Binding cancellation cannot publish an output or stale receipt")
{
	Fixture f;
	auto bytes = zeros(2 * 1024 * 1024).dump();
	std::stop_source stop;
	std::uint64_t previous = 0;
	CHECK_THROWS_AS(bind_weights(bytes, {}, f.output, {}, stop.get_token(),
				     [&](Progress p) {
					     CHECK(p.completed_bytes >= previous);
					     CHECK(p.total_bytes == 2 * 1024 * 1024);
					     CHECK(p.can_cancel_now);
					     previous = p.completed_bytes;
					     if (previous)
						     stop.request_stop();
				     }),
			Failure);
	CHECK(previous == 1024 * 1024);
	CHECK_FALSE(f.exists(".ready.json"));
	CHECK_FALSE(f.exists(".partial"));
	auto result = bind_weights(zeros(4).dump(), {}, f.output);
	CHECK(result.sha256 == sha256(std::string(4, '\0')));
	CHECK(f.exists(".ready.json"));
	CHECK_THROWS_AS(bind_weights(zeros(4).dump(), {}, f.output, {}, {},
				     [](Progress) { throw std::runtime_error("UI callback failed"); }),
			std::runtime_error);
	CHECK_FALSE(f.exists(".ready.json"));
}
TEST_CASE("Orphans from an interrupted run are overwritten and verified before readiness")
{
	Fixture f;
	atomic_write(f.dir / "api-test.weights.partial", "unfinished");
	atomic_write(f.dir / "api-test.weights.tensor.tmp", "unfinished array");
	atomic_write(f.dir / "api-test.weights.ready.json.partial", "unfinished receipt");
	auto result = bind_weights(zeros(4).dump(), {}, f.output);
	CHECK(result.sha256 == sha256(std::string(4, '\0')));
	CHECK(f.exists(".ready.json"));
	CHECK_FALSE(f.exists(".partial"));
	CHECK_FALSE(f.exists(".tensor.tmp"));
	CHECK_FALSE(f.exists(".ready.json.partial"));
}
int main(int argc, char **argv)
{
	if (argc == 3 && std::string_view(argv[1]) == "--crash") {
		bind_weights(zeros(4096).dump(), {}, utf8_path(argv[2]), {}, {}, [](Progress p) {
			if (p.completed_bytes)
				std::_Exit(73); // Deliberately skip stack unwinding to test interrupted writes.
		});
		return 1;
	}
	return doctest::Context(argc, argv).run();
}
