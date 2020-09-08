#ifndef LIBFILEZILLA_GLUE_WXINVOKER_HEADER
#define LIBFILEZILLA_GLUE_WXINVOKER_HEADER

#include "../invoker.hpp"

#include <wx/event.h>

namespace fz {
class wxinvoker final : public wxEvtHandler, public invoker_base
{
	wxinvoker(wxinvoker const&) = delete;
	wxinvoker& operator=(wxinvoker const&) = delete;

	virtual void operator()(std::function<void()> const& cb) override {
		CallAfter(cb);
	}

	virtual void operator()(std::function<void()> && cb) override {
		CallAfter(cb);
	}
};
}

#endif
