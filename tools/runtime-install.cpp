// SPDX-License-Identifier: GPL-3.0-or-later
#include "packaging/runtime_install.hpp"
#include "util/utf8_path.hpp"
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace corridorkey;
int run(const std::vector<std::string> &args)
{
	try {
		if (args.size() != 5) {
			std::cerr
				<< "Usage: corridorkey-runtime <installed-catalogue.json> <runtime-cache-directory> <bundle-id> <show|install>\n"
				   "Use show to read the licences without downloading. Install asks for explicit acceptance.\n";
			return 2;
		}
		auto package = packaging::runtime_package(models::read_small_file(utf8_path(args[1])),
							  CK_RUNTIME_CATALOGUE_SHA256, args[3]);
#ifdef _WIN32
		models::require(package.recipe.at("platform") == "windows-x64",
				"Runtime package is for another platform");
#else
		models::require(package.recipe.at("platform") == "linux-x86_64",
				"Runtime package is for another platform");
#endif
		std::cout << models::licence_notices(package.download, package.licences) << std::endl;
		if (args[4] == "show")
			return 0;
		models::require(args[4] == "install", "Expected show or install");
		std::cout << "These libraries download directly from the pinned vendor URLs.\n"
			     "To accept all texts above and download, type I ACCEPT and press Enter: "
			  << std::flush;
		std::string response;
		std::getline(std::cin, response);
		if (response != "I ACCEPT") {
			std::cout << "No download started.\n";
			return 3;
		}
		models::Cache cache(utf8_path(args[2]));
		models::AcceptanceStore accepted(cache);
		accepted.accept(package.download, package.licences, package.download.licences);
		auto path = packaging::install_runtime(package, cache, accepted, {}, [](Progress p) {
			if (p.total_bytes && p.completed_bytes == *p.total_bytes)
				std::cout << "Downloaded " << p.completed_bytes << " bytes.\n";
		});
		std::cout << "Runtime installed. Restart OBS before selecting this bundle.\n" << path << '\n';
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Runtime setup: " << e.what() << '\n';
		return 1;
	}
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
{
	std::vector<std::string> args;
	for (int n = 0; n < argc; ++n) {
		int size = WideCharToMultiByte(CP_UTF8, 0, argv[n], -1, nullptr, 0, nullptr, nullptr);
		std::string text(static_cast<std::size_t>(size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, argv[n], -1, text.data(), size, nullptr, nullptr);
		text.pop_back();
		args.push_back(std::move(text));
	}
	return run(args);
}
#else
int main(int argc, char **argv)
{
	return run(std::vector<std::string>(argv, argv + argc));
}
#endif
