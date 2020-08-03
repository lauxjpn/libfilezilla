#include "libfilezilla/rate_limited_layer.hpp"

namespace fz {

rate_limited_layer::rate_limited_layer(event_handler* handler, socket_interface& next_layer, rate_limiter * limiter)
	: socket_layer(handler, next_layer, true)
{
	next_layer.set_event_handler(handler);
	if (limiter) {
		limiter->add(this);
	}
}

rate_limited_layer::~rate_limited_layer()
{
	remove_bucket();
	next_layer_.set_event_handler(nullptr);
}

void rate_limited_layer::wakeup(direction::type d)
{
	if (!event_handler_) {
		return;
	}

	if (d == direction::inbound) {
		event_handler_->send_event<socket_event>(this, socket_event_flag::read, 0);
	}
	else {
		event_handler_->send_event<socket_event>(this, socket_event_flag::write, 0);
	}
}

int rate_limited_layer::read(void* buffer, unsigned int size, int& error)
{
	auto const max = available(direction::inbound);
	if (!max) {
		error = EAGAIN;
		return -1;
	}

	static_assert(sizeof(size) <= sizeof(max));
	if (max < static_cast<std::decay_t<decltype(max)>>(size)) {
		size = static_cast<unsigned int>(max);
	}

	int read = next_layer_.read(buffer, size, error);
	if (read > 0 && max != rate::unlimited) {
		consume(direction::inbound, read);
	}

	return read;
}

int rate_limited_layer::write(void const* buffer, unsigned int size, int& error)
{
	auto const max = available(direction::outbound);
	if (!max) {
		error = EAGAIN;
		return -1;
	}

	static_assert(sizeof(size) <= sizeof(max));
	if (max < static_cast<std::decay_t<decltype(max)>>(size)) {
		size = static_cast<unsigned int>(max);
	}

	int written = next_layer_.write(buffer, size, error);
	if (written > 0 && max != rate::unlimited) {
		consume(direction::outbound, written);
	}

	return written;
}

}
