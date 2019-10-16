#ifndef LIBFILEZILLA_RATE_LIMITED_LAYER_HEADER
#define LIBFILEZILLA_RATE_LIMITED_LAYER_HEADER

/** \file
 * \brief A rate-limited socket layer
 */

#include "rate_limiter.hpp"
#include "socket.hpp"

namespace fz {

/**
 * \brief A rate-limited socket layer.
 *
 * This socket layer is a bucket that can be added to a \sa rate_limiter.
 */
class FZ_PUBLIC_SYMBOL rate_limited_layer final : public socket_layer, private bucket
{
public:
	rate_limited_layer(event_handler* handler, socket_interface& next_layer, rate_limiter * limiter = nullptr);
	virtual ~rate_limited_layer();

	virtual int read(void* buffer, unsigned int size, int& error) override;
	virtual int write(void const* buffer, unsigned int size, int& error) override;

	virtual socket_state get_state() const override {
		return next_layer_.get_state();
	}

	virtual int connect(native_string const& host, unsigned int port, address_type family = address_type::unknown) override {
		return next_layer_.connect(host, port, family);
	}

	virtual int shutdown() override {
		return next_layer_.shutdown();
	}

	virtual void set_event_handler(event_handler* handler) override {
		scoped_lock l(mtx_);
		socket_layer::set_event_handler(handler);
	}

protected:
	virtual void wakeup(direction::type d) override;
};

}

#endif
