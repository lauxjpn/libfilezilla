#include "libfilezilla/invoker.hpp"

namespace fz {
thread_invoker::thread_invoker(event_loop& loop)
	: event_handler(loop)
{
}

thread_invoker::~thread_invoker()
{
	remove_handler();
}

void thread_invoker::operator()(fz::event_base const& ev)
{
	if (ev.derived_type() == invoker_event::type()) {
		auto const& cb = std::get<0>(static_cast<invoker_event const&>(ev).v_);
		if (cb) {
			cb();
		}
	}
}
}
