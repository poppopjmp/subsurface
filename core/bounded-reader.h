// SPDX-License-Identifier: GPL-2.0
//
// A minimal bounds-checked cursor over a byte buffer.
//
// The binary importers were written against files produced by real dive
// computers, so they read a length out of the file and then trust it. The input
// they actually get is whatever the user opened, which may well have been mailed
// to them by somebody else. This type makes the checked read the easy one to
// write: every accessor validates before it reads, and the first overrun latches
// the reader into a failed state so a caller can check once at the end of a block
// instead of after every field.
//
// Reads on a failed reader return zero and do not move the position, so a partly
// parsed record contains defined values rather than whatever was next in memory.

#ifndef BOUNDED_READER_H
#define BOUNDED_READER_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

class BoundedReader {
public:
	BoundedReader(const unsigned char *buf, size_t size) : buf_(buf), size_(buf ? size : 0)
	{
	}

	bool ok() const { return ok_; }
	size_t pos() const { return pos_; }
	size_t size() const { return size_; }
	size_t remaining() const { return (ok_ && pos_ <= size_) ? size_ - pos_ : 0; }

	// Written so that neither side of the comparison can underflow.
	bool has(size_t n) const { return ok_ && pos_ <= size_ && n <= size_ - pos_; }

	void fail() { ok_ = false; }

	bool skip(size_t n)
	{
		if (!has(n))
			return fail_and_return_false();
		pos_ += n;
		return true;
	}

	bool seek(size_t p)
	{
		if (!ok_ || p > size_)
			return fail_and_return_false();
		pos_ = p;
		return true;
	}

	uint8_t u8()
	{
		if (!has(1))
			return fail_and_return_zero();
		return buf_[pos_++];
	}

	uint16_t u16_le()
	{
		if (!has(2))
			return fail_and_return_zero();
		uint16_t v = (uint16_t)(buf_[pos_] | (buf_[pos_ + 1] << 8));
		pos_ += 2;
		return v;
	}

	uint32_t u32_le()
	{
		if (!has(4))
			return fail_and_return_zero();
		uint32_t v = (uint32_t)buf_[pos_] | ((uint32_t)buf_[pos_ + 1] << 8) |
			     ((uint32_t)buf_[pos_ + 2] << 16) | ((uint32_t)buf_[pos_ + 3] << 24);
		pos_ += 4;
		return v;
	}

	// Read without advancing, for the places that have to look ahead to decide
	// which layout follows.
	uint32_t peek_u32_le() const
	{
		if (!has(4))
			return 0;
		return (uint32_t)buf_[pos_] | ((uint32_t)buf_[pos_ + 1] << 8) |
		       ((uint32_t)buf_[pos_ + 2] << 16) | ((uint32_t)buf_[pos_ + 3] << 24);
	}

	// memcpy rather than a cast: the buffer has no alignment guarantee.
	float f32()
	{
		if (!has(4))
			return fail_and_return_zero();
		float v;
		memcpy(&v, buf_ + pos_, 4);
		pos_ += 4;
		return v;
	}

	std::string str(size_t n)
	{
		if (!has(n))
			return fail_and_return_string();
		std::string s((const char *)buf_ + pos_, n);
		pos_ += n;
		return s;
	}

	// Raw access for the callers that still index a region themselves. Returns
	// nullptr unless the whole region is inside the buffer, so the caller cannot
	// accidentally get a pointer it is not allowed to walk.
	const unsigned char *region(size_t n)
	{
		if (!has(n))
			return nullptr;
		return buf_ + pos_;
	}

private:
	bool fail_and_return_false() { ok_ = false; return false; }
	uint32_t fail_and_return_zero() { ok_ = false; return 0; }
	std::string fail_and_return_string() { ok_ = false; return std::string(); }

	const unsigned char *buf_;
	size_t size_;
	size_t pos_ = 0;
	bool ok_ = true;
};

#endif // BOUNDED_READER_H
