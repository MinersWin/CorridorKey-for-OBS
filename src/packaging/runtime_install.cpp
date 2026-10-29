// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime_install.hpp"
#include "models/tensor_storage.hpp"
#include "util/utf8_path.hpp"
#include <array>
#include <algorithm>
#include <fstream>
#include <zlib.h>

namespace corridorkey::packaging {
using namespace models;
using storage::little;

RuntimePackage runtime_package(std::string_view bytes, std::string_view trusted, std::string_view id)
{
	require(sha256(bytes) == trusted, "Runtime catalogue differs from the installed executable",
		ErrorCode::hash_mismatch);
	auto j = strict_json(bytes);
	require(uint64(j.at("schema_version")) == 1, "Unsupported runtime catalogue");
	RuntimePackage out;
	out.licences = LicenceRegistry::parse(j.at("licenses").dump());
	require(j.at("bundles").contains(id), "Unknown runtime bundle");
	out.recipe = j.at("bundles").at(id);
	if (!out.recipe.at("enabled").get<bool>())
		throw Failure(ErrorCode::unsupported, string(out.recipe.at("reason")));
	auto &m = out.download;
	m.digest = sha256(std::string(bytes) + std::string(id));
	m.key = {std::string(id), m.digest};
	identifier(m.key.id);
	m.display_name = string(out.recipe.at("display_name"));
	m.publisher = "NVIDIA / Microsoft";
	for (const auto &licence : out.recipe.at("licenses")) {
		LicenceAcceptance a{string(licence.at("id")), string(licence.at("text_sha256"))};
		(void)out.licences.at(a);
		m.licences.push_back(std::move(a));
	}
	require(!m.licences.empty(), "Runtime bundle has no licence acceptance texts");
	for (const auto &a : out.recipe.at("artifacts")) {
		Artifact artifact;
		artifact.id = string(a.at("id"));
		identifier(artifact.id);
		artifact.url = string(a.at("url"));
		https_url(artifact.url);
		artifact.bytes = uint64(a.at("bytes"));
		artifact.sha256 = string(a.at("sha256"));
		require(artifact.bytes > 0 && hex_string(artifact.sha256, 64), "Invalid runtime archive pin");
		artifact.path = artifact.id + ".zip";
		artifact.format = "runtime-zip";
		artifact.revision = artifact.sha256;
		for (const auto &l : m.licences)
			artifact.licences.push_back(l.licence_id);
		require(m.artifacts.emplace(artifact.id, std::move(artifact)).second, "Duplicate runtime artifact");
	}
	return out;
}

void extract_runtime_member(const fs::path &archive, std::string_view member, const fs::path &destination,
			    std::uint64_t expected_bytes, std::stop_token stop)
{
	// Extract only an exact catalogue-owned member to a catalogue-owned destination.
	// Archive names never become output paths. Large vendor DLLs exceed NPZ's 256 MiB limit.
	storage::File file(archive);
	require(file.size >= 22 && expected_bytes <= (std::uint64_t{8} << 30), "Invalid runtime ZIP size");
	auto tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(file.size, 65557));
	auto tail = file.read(file.size - tail_size, tail_size);
	std::size_t pos = tail.size() - 22;
	for (;;) {
		if (little(tail, pos, 4) == 0x06054b50 && pos + 22 + little(tail, pos + 20, 2) == tail.size())
			break;
		require(pos > 0, "Missing runtime ZIP directory");
		--pos;
	}
	require(little(tail, pos + 4, 2) == 0 && little(tail, pos + 6, 2) == 0, "Split runtime ZIP forbidden");
	auto count = little(tail, pos + 10, 2), offset = little(tail, pos + 16, 4);
	require(count < 65535 && offset != 0xffffffff, "ZIP64 directory requires a reviewed runtime recipe");
	std::uint64_t start = 0, compressed = 0;
	unsigned method = 0;
	std::uint32_t crc = 0;
	bool found = false;
	for (std::uint64_t n = 0; n < count; ++n) {
		storage::cancelled(stop);
		auto h = file.read(offset, std::size_t{46});
		require(little(h, 0, 4) == 0x02014b50, "Invalid runtime ZIP directory entry");
		auto name_size = little(h, 28, 2), extra = little(h, 30, 2), comment = little(h, 32, 2);
		auto name = file.read(offset + 46, static_cast<std::size_t>(name_size));
		if (name == member) {
			require(!found && !(little(h, 8, 2) & 1), "Ambiguous or encrypted runtime member");
			found = true;
			compressed = little(h, 20, 4);
			auto bytes = little(h, 24, 4), local = little(h, 42, 4);
			require(bytes == expected_bytes && compressed != 0xffffffff && local != 0xffffffff,
				"Runtime member size/ZIP64 mismatch");
			method = static_cast<unsigned>(little(h, 10, 2));
			crc = static_cast<std::uint32_t>(little(h, 16, 4));
			auto lh = file.read(local, std::size_t{30});
			require(little(lh, 0, 4) == 0x04034b50 && little(lh, 8, 2) == method,
				"Invalid local ZIP header");
			start = local + 30 + little(lh, 26, 2) + little(lh, 28, 2);
		}
		offset = checked_add(offset, 46 + name_size + extra + comment);
	}
	require(found && (method == 0 || method == 8), "Missing or unsupported runtime member");
	fs::create_directories(destination.parent_path());
	std::ofstream output(destination, std::ios::binary | std::ios::trunc);
	require(bool(output), "Cannot create runtime file", ErrorCode::io_failure);
	std::array<char, 65536> input{}, expanded{};
	z_stream stream{};
	if (method == 8)
		require(inflateInit2(&stream, -MAX_WBITS) == Z_OK, "Runtime inflate initialization failed");
	struct End {
		z_stream &s;
		bool active;
		~End()
		{
			if (active)
				inflateEnd(&s);
		}
	} end{stream, method == 8};
	std::uint64_t written = 0;
	uLong checksum = crc32(0, Z_NULL, 0);
	bool finished = method == 0;
	auto emit = [&](const char *p, std::size_t size) {
		written = checked_add(written, size);
		require(written <= expected_bytes, "Runtime ZIP expanded beyond declared size");
		checksum = crc32(checksum, reinterpret_cast<const Bytef *>(p), static_cast<uInt>(size));
		output.write(p, static_cast<std::streamsize>(size));
		require(bool(output), "Runtime write failed", ErrorCode::io_failure);
	};
	int result = Z_OK;
	while (method == 0 ? compressed != 0 : result != Z_STREAM_END) {
		storage::cancelled(stop);
		if (!stream.avail_in && compressed) {
			auto size = static_cast<std::size_t>(std::min<std::uint64_t>(compressed, input.size()));
			file.read(start, std::span<char>(input.data(), size));
			start += size;
			compressed -= size;
			if (method == 0) {
				emit(input.data(), size);
				continue;
			}
			stream.next_in = reinterpret_cast<Bytef *>(input.data());
			stream.avail_in = static_cast<uInt>(size);
		}
		stream.next_out = reinterpret_cast<Bytef *>(expanded.data());
		stream.avail_out = static_cast<uInt>(expanded.size());
		auto before = stream.avail_in;
		result = inflate(&stream, Z_NO_FLUSH);
		require(result == Z_OK || result == Z_STREAM_END, "Corrupt runtime ZIP stream");
		auto count_out = expanded.size() - stream.avail_out;
		emit(expanded.data(), count_out);
		require(result == Z_STREAM_END || count_out || stream.avail_in < before,
			"Truncated runtime ZIP stream");
		if (result == Z_STREAM_END) {
			finished = true;
			require(!compressed && !stream.avail_in, "Trailing runtime ZIP bytes");
		}
	}
	require(finished && written == expected_bytes && checksum == crc, "Runtime ZIP integrity mismatch",
		ErrorCode::hash_mismatch);
	output.close();
	require(bool(output), "Runtime close failed", ErrorCode::io_failure);
}

fs::path install_runtime(const RuntimePackage &package, Cache &cache, const AcceptanceStore &accepted,
			 std::stop_token stop, std::function<void(Progress)> progress)
{
	require(accepted.contains(package.download, package.licences), "Accept all runtime licences before downloading",
		ErrorCode::licence_required);
	FileLock lock(cache.path("locks/runtime-" + package.download.key.id + ".lock"), false, true, stop);
	auto destination = cache.path("bundles/" + package.download.key.id);
	require(!fs::exists(destination), "This immutable runtime bundle already exists; restart OBS to use it",
		ErrorCode::unsupported);
	DownloadEngine engine(cache);
	for (const auto &[id, artifact] : package.download.artifacts)
		engine.fetch(package.download, artifact, package.licences, accepted, stop, progress);
	cache.check_space(uint64(package.recipe.at("expanded_bytes")));
	auto partial = cache.path("runtime-staging/" + package.download.key.id);
	// A prior interrupted installation is never exposed to the loader.
	if (fs::exists(partial))
		fs::remove_all(partial);
	fs::create_directories(partial);
	Json hashes = Json::object();
	for (const auto &entry : package.recipe.at("files")) {
		storage::cancelled(stop);
		auto target = string(entry.at("path"));
		relative_path(target);
		auto path = partial / utf8_path(target);
		const auto &artifact = package.download.artifacts.at(string(entry.at("artifact")));
		extract_runtime_member(cache.blob(artifact.sha256), string(entry.at("member")), path,
				       uint64(entry.at("bytes")), stop);
		auto hash = file_sha256(path, stop);
		require(hash == string(entry.at("sha256")), "Extracted runtime hash mismatch",
			ErrorCode::hash_mismatch);
		hashes[target] = hash;
	}
	for (const auto &entry : package.recipe.value("extra_files", Json::array())) {
		auto target = string(entry.at("path"));
		relative_path(target);
		auto text = string(entry.at("text"), 256 * 1024);
		require(sha256(text) == string(entry.at("sha256")), "Runtime licence snapshot hash mismatch",
			ErrorCode::hash_mismatch);
		atomic_write(partial / utf8_path(target), text);
		hashes[target] = sha256(text);
	}
	for (auto descriptor : package.recipe.at("descriptors")) {
		auto filename = string(descriptor.at("filename"));
		relative_path(filename);
		descriptor.erase("filename");
		descriptor["id"] = package.download.key.id;
		descriptor["files"] = hashes;
		atomic_write(partial / utf8_path(filename), descriptor.dump(2));
	}
	std::string notices = "# Runtime third-party notices\n\n";
	for (const auto &[id, artifact] : package.download.artifacts)
		notices += "- " + id + ": " + artifact.url + " (archive SHA-256 " + artifact.sha256 + ")\n";
	notices += "\nLicence and notice files included in this bundle:\n\n";
	for (const auto &[path, hash] : hashes.items())
		if (path.starts_with("licenses/"))
			notices += "- " + path + " (SHA-256 " + hash.get<std::string>() + ")\n";
	atomic_write(partial / "THIRD_PARTY_NOTICES.md", notices);
	atomic_write(partial / "BUNDLE_CONTENTS.json", Json{{"schema", 1},
							    {"id", package.download.key.id},
							    {"artifacts", package.recipe.at("artifacts")},
							    {"files", hashes}}
							       .dump(2));
	atomic_write(partial / "provenance.json", package.recipe.dump(2));
	storage::cancelled(stop);
	fs::create_directories(destination.parent_path());
	fs::rename(partial, destination);
	return destination;
}
} // namespace corridorkey::packaging
