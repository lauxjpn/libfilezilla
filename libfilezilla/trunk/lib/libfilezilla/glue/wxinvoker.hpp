#ifndef LIBFILEZILLA_GLUE_WXINVOKER_HEADER
#define LIBFILEZILLA_GLUE_WXINVOKER_HEADER

#include "../invoker.hpp"

#include <wx/event.h>

namespace fz {

/// \private
template<typename... Args>
std::function<void(Args...)> do_make_invoker(wxEvtHandler& handler, std::function<void(Args...)> && f)
{
	return [&handler, cf = f](Args&&... args) {
		auto cb = [cf, targs = std::make_tuple(std::forward<Args>(args)...)] {
			std::apply(cf, targs);
		};
		handler.CallAfter(cb);
	};
}

/// /\brief Alternative version of fz::invoke that accepts wxEvtHandler
template<typename F>
auto make_invoker(wxEvtHandler& handler, F && f)
{
	return do_make_invoker(handler, std::function(std::forward<F>(f)));
}

}

#endif
