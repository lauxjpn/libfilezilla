#include "libfilezilla/buffer.hpp"

#include <algorithm>
#include <cstdlib>

#include <string.h>

namespace fz {

buffer::buffer(size_t capacity)
{
	reserve(capacity);
}

buffer::buffer(buffer const& buf)
{
	if (buf.size_) {
		data_ = new unsigned char[buf.capacity_];
		memcpy(data_, buf.pos_, buf.size_);
		size_ = buf.size_;
		capacity_ = buf.capacity_;
		pos_ = data_;
	}
}

buffer::buffer(buffer && buf) noexcept
{
	data_ = buf.data_;
	buf.data_ = nullptr;
	pos_ = buf.pos_;
	buf.pos_ = nullptr;
	size_ = buf.size_;
	buf.size_ = 0;
	capacity_ = buf.capacity_;
	buf.capacity_ = 0;
}

unsigned char* buffer::get(size_t write_size)
{
	if (capacity_ - (pos_ - data_) - size_ < write_size) {
		if (capacity_ - size_ > write_size) {
			memmove(data_, pos_, size_);
			pos_ = data_;
		}
		else {
			if (std::numeric_limits<size_t>::max() - capacity_ < write_size) {
				std::abort();
			}
			size_t const cap = std::max({ size_t(1024), capacity_ * 2, capacity_ + write_size });
			unsigned char* d = new unsigned char[cap];
			if (size_) {
				memcpy(d, pos_, size_);
			}
			delete[] data_;
			capacity_ = cap;
			data_ = d;
			pos_ = d;
		}
	}
	return pos_ + size_;
}

buffer& buffer::operator=(buffer const& buf)
{
	if (this != &buf) {
		unsigned char* d{};
		if (buf.size_) {
			d = new unsigned char[buf.capacity_];
			memcpy(d, buf.pos_, buf.size_);
		}
		delete[] data_;
		data_ = d;
		size_ = buf.size_;
		capacity_ = buf.capacity_;
		pos_ = data_;
	}

	return *this;
}

buffer& buffer::operator=(buffer && buf) noexcept
{
	if (this != &buf) {
		delete[] data_;
		data_ = buf.data_;
		buf.data_ = nullptr;
		pos_ = buf.pos_;
		buf.pos_ = nullptr;
		size_ = buf.size_;
		buf.size_ = 0;
		capacity_ = buf.capacity_;
		buf.capacity_ = 0;
	}

	return *this;
}


void buffer::add(size_t added)
{
	if (capacity_ - (pos_ - data_) - size_ < added) {
		// Hang, draw and quarter the caller.
		std::abort();
	}
	size_ += added;
}

void buffer::consume(size_t consumed)
{
	if (consumed > size_) {
		std::abort();
	}
	if (consumed == size_) {
		pos_ = data_;
		size_ = 0;
	}
	else {
		size_ -= consumed;
		pos_ += consumed;
	}
}

void buffer::clear()
{
	size_ = 0;
	pos_ = data_;
}

void buffer::append(unsigned char const* data, size_t len)
{
	// Do the same initially as buffer::get would do, but don't delete the old pointer
	// until after appending in case of append from own memory
	unsigned char* old{};
	if (capacity_ - (pos_ - data_) - size_ < len) {
		if (capacity_ - size_ > len) {
			memmove(data_, pos_, size_);
			pos_ = data_;
		}
		else {
			if (std::numeric_limits<size_t>::max() - capacity_ < len) {
				std::abort();
			}
			size_t const cap = std::max({ size_t(1024), capacity_ * 2, capacity_ + len });
			unsigned char* d = new unsigned char[cap];
			if (size_) {
				memcpy(d, pos_, size_);
			}
			old = data_;
			capacity_ = cap;
			data_ = d;
			pos_ = d;
		}
	}

	memcpy(pos_ + size_, data, len);
	size_ += len;

	delete [] old;
}

void buffer::append(std::string_view const& str)
{
	append(reinterpret_cast<unsigned char const*>(str.data()), str.size());
}

void buffer::reserve(size_t capacity)
{
	if (capacity_ >= capacity) {
		return;
	}

	size_t const cap = std::max(size_t(1024), capacity);
	unsigned char* d = new unsigned char[cap];
	if (size_) {
		memcpy(d, pos_, size_);
	}
	delete[] data_;
	data_ = d;
	capacity_ = cap;
	pos_ = data_;
}

void buffer::resize(size_t size)
{
	if (!size) {
		clear();
	}
	else if (size < size_) {
		size_ = size;
	}
	else {
		size_t const diff = size - size_;
		memset(get(diff), 0, diff);
		size_ = size;
	}
}

bool buffer::operator==(buffer const& rhs) const
{
	if (size() != rhs.size()) {
		return false;
	}

	if (!size()) {
		return true;
	}

	return memcmp(get(), rhs.get(), size()) == 0;
}

}
