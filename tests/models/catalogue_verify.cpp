// SPDX-License-Identifier: GPL-3.0-or-later
#include "models/cache.hpp"
#include "util/utf8_path.hpp"
#include <iostream>

using namespace corridorkey::models;

int main(int argc, char **argv)
{
	if (argc != 4)
		return 2;
	try {
		auto public_key = read_small_file(corridorkey::utf8_path(argv[3]));
		if (!public_key.empty() && public_key.back() == '\n')
			public_key.pop_back();
		if (!public_key.empty() && public_key.back() == '\r')
			public_key.pop_back();
		const auto signature = strict_json(read_small_file(corridorkey::utf8_path(argv[2])));
		fields(signature, {"signature"});
		(void)parse_catalogue(read_small_file(corridorkey::utf8_path(argv[1])),
				      string(signature.at("signature")), public_key, 0);
		return 0;
	} catch (const std::exception &) {
		std::cerr << "Catalogue verification failed\n";
		return 1;
	}
}
