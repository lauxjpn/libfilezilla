#include "libfilezilla/invoker.hpp"

namespace fz {
invoker::~invoker()
{
	remove_handler();
}

void invoker::operator()(fz::event_base const& ev)
{
	if (ev.derived_type() == invoker_event::type()) {
		auto const& cb = std::get<0>(static_cast<invoker_event const&>(ev).v_);
		if (cb) {
			cb();
		}
	}
}
}
