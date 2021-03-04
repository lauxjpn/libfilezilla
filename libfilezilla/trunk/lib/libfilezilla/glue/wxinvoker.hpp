#ifndef LIBFILEZILLA_GLUE_WXINVOKER_HEADER
#define LIBFILEZILLA_GLUE_WXINVOKER_HEADER

/** \file
 * \brief Glue to create invokers using the event system of wxWidgets.
 */

#include "../invoker.hpp"

#include <wx/event.h>

namespace fz {

/// \brief Alternative version of fz::invoke that accepts wxEvtHandler
template<typename F>
auto make_invoker(wxEvtHandler& handler, F && f)
{
	return [&handler, cf = std::forward<F>(f)](auto &&... args) mutable -> decltype(f(std::forward<decltype(args)>(args)...), void()) 
	{
		auto cb = [cf = std::move(cf), targs = std::make_tuple(std::forward<decltype(args)>(args)...)] {
			std::apply(cf, targs);
		};
		handler.CallAfter(cb);
	};
}

/// \brief Returns an invoker factory utilizing the event system of of wx.
inline invoker_factory get_invoker_factory(wxEvtHandler& handler)
{
	return [&handler](std::function<void()> const& cb) mutable {
		handler.CallAfter(cb);
	};
}

}

#endif
