// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
struct IDMLDevice;
struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;
struct ID3D12GraphicsCommandList;
struct IDXGIAdapter;
namespace corridorkey::dml {
class Context;
class Buffer {
public:
	Buffer(std::shared_ptr<Context>, std::size_t);
	~Buffer();
	Buffer(const Buffer &) = delete;
	Buffer &operator=(const Buffer &) = delete;
	std::size_t bytes() const { return bytes_; }
	const std::shared_ptr<Context> &context() const { return context_; }
	ID3D12Resource *resource() const;
	void upload(const void *, std::size_t);
	void download(void *, std::size_t) const;
	void copy(const Buffer &);
	void zero();

private:
	struct Impl;
	std::shared_ptr<Context> context_;
	std::unique_ptr<Impl> impl_;
	std::size_t bytes_;
};
// Each context belongs to one pipeline. Calls and fence waits are worker-only.
class Context : public std::enable_shared_from_this<Context> {
public:
	Context(const std::string &adapter_luid, const std::filesystem::path &directml_library);
	~Context();
	Context(const Context &) = delete;
	Context &operator=(const Context &) = delete;
	ID3D12Device *device() const;
	IDMLDevice *dml_device() const;
	ID3D12CommandQueue *queue() const;
	IDXGIAdapter *adapter() const;
	std::shared_ptr<Buffer> allocate(std::size_t);
	std::recursive_mutex &mutex();
	// Resources enter and leave commands in UAV state, shared textures in COMMON.
	ID3D12GraphicsCommandList *begin();
	void finish();
	void synchronize();
	bool failed() const noexcept;
	void compute(const char *kernel, unsigned count, const Buffer &src, const Buffer *second, Buffer &dst,
		     std::span<const unsigned> constants);
	void validate(const Buffer &, bool unit_range);
	void probe();

private:
	friend class Buffer;
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace corridorkey::dml
