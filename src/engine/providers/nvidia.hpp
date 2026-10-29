// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace corridorkey::nvidia {
using Pointer = unsigned long long;
class Context;
class Buffer {
public:
	Buffer(std::shared_ptr<Context>, std::size_t bytes);
	~Buffer();
	Buffer(const Buffer &) = delete;
	Buffer &operator=(const Buffer &) = delete;
	Pointer pointer() const { return pointer_; }
	std::size_t bytes() const { return bytes_; }
	const std::shared_ptr<Context> &context() const { return context_; }
	void upload(const void *, std::size_t);
	void download(void *, std::size_t) const;
	void copy(const Buffer &);
	void zero();

private:
	std::shared_ptr<Context> context_;
	Pointer pointer_ = 0;
	std::size_t bytes_;
};
// Every operation is worker-only. No CUDA import library or runtime DLL dependency.
class Context : public std::enable_shared_from_this<Context> {
public:
	explicit Context(int ordinal);
	~Context();
	Context(const Context &) = delete;
	Context &operator=(const Context &) = delete;
	int ordinal() const;
	std::shared_ptr<Buffer> allocate(std::size_t bytes);
	void launch(const char *name, unsigned count, std::span<void *> arguments);
	void synchronize();
	void ensure_kernels();
	void *stream() const;
	void start_timer();
	double stop_timer();
	void validate(const Buffer &, bool unit_range);
	void *symbol(const char *) const;
	class Scope {
	public:
		explicit Scope(const Context &);
		~Scope();

	private:
		const Context &context_;
	};
	static void check(int code, std::string_view operation);

private:
	friend class Buffer;
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace corridorkey::nvidia
