#ifndef LIBFILEZILLA_INVOKER_HEADER
#define LIBFILEZILLA_INVOKER_HEADER

#include "event_handler.hpp"

namespace fz {
class FZ_PUBLIC_SYMBOL invoker_base
{
public:
	virtual ~invoker_base() = default;
	
	// Contract: Must be asychronous
	virtual void operator()(std::function<void()> const& cb) = 0;
	virtual void operator()(std::function<void()> && cb) = 0;
};

/// \private
struct invoker_event_type{};

/// \private
typedef simple_event<invoker_event_type, std::function<void()>> invoker_event;

class FZ_PUBLIC_SYMBOL invoker final : public event_handler, public invoker_base
{
	invoker(fz::event_loop const& loop);
	virtual ~invoker();

	invoker(invoker const&) = delete;
	invoker& operator=(invoker const&) = delete;

	virtual void operator()(std::function<void()> const& cb) override {
		send_event<invoker_event>(cb);
	}

	virtual void operator()(std::function<void()> && cb) override {
		send_event<invoker_event>(cb);
	}

private:
	virtual void operator()(fz::event_base const& ev) override;
};
}

#endif