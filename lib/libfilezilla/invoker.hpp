#ifndef LIBFILEZILLA_INVOKER_HEADER
#define LIBFILEZILLA_INVOKER_HEADER

#include "event_handler.hpp"

namespace fz {

/// \private
struct invoker_event_type{};

/// \private
typedef simple_event<invoker_event_type, std::function<void()>> invoker_event;

/// \private
class FZ_PUBLIC_SYMBOL thread_invoker final : public event_handler
{
public:
	thread_invoker(event_loop& loop);
	
	virtual ~thread_invoker();
	virtual void operator()(event_base const& ev) override;
};

/// \private
template<typename... Args>
std::function<void(Args...)> do_make_invoker(event_loop& loop, std::function<void(Args...)> && f)
{
	return [handler = thread_invoker(loop), f](Args&&... args) {
		auto cb = [f, args = std::make_tuple(std::forward<Args>(args)...)] {
			std::apply(f, args);
		};
		handler.send_event(invoker_event)(cb);
	};
}

/**
 * \brief Wraps the passed function, so that it is always invoked in the context of the loop.
 *
 * Returns a std::function with the same arguments than the passed function.
 * The returned function can be called in any thread and as result the passed function is called
 * asynchronously with the same arguments in the loop's thread.
 */
template<typename F>
auto make_invoker(event_loop& loop, F && f)
{
	return do_make_invoker(loop, std::function(std::forward<F>(f)));
}
template<typename F>
auto make_invoker(event_handler& h, F && f)
{
	return do_make_invoker(h.event_loop_, std::function(std::forward<F>(f)));
}

}

#endif