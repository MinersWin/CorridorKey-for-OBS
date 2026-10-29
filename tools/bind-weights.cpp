// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
#include "models/binder.hpp"
#include "models/tensor_storage.hpp"
#include "util/utf8_path.hpp"
#include <iostream>

int main(int argc, char **argv)
{
	using namespace corridorkey;
	using namespace corridorkey::models;
	try {
		if (argc == 4 && std::string_view(argv[1]) == "--inspect-npy") {
			storage::File file(utf8_path(argv[2]));
			auto tensor = storage::read_npy(file);
			require(tensor.dtype == argv[3], "Unexpected NPY dtype");
			return 0;
		}
		require(argc >= 4 && (argc - 4) % 2 == 0,
			"Usage: bind-weights <graph.json> <output.weights> <expected-sha256-or-dash> [source-id local-path]...");
		auto metadata = read_small_file(utf8_path(argv[1]), 32 * 1024 * 1024);
		std::map<std::string, fs::path> paths;
		for (int i = 4; i < argc; i += 2)
			require(paths.emplace(argv[i], utf8_path(argv[i + 1])).second, "Duplicate source argument");
		auto result = bind_weights(metadata, paths, utf8_path(argv[2]),
					   std::string_view(argv[3]) == "-" ? "" : argv[3]);
		std::cout << Json{{"bytes", result.bytes}, {"sha256", result.sha256}}.dump() << '\n';
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
