#ifndef LIBFILEZILLA_INVOKER_HEADER
#define LIBFILEZILLA_INVOKER_HEADER

/** \file
 * \brief Declares fz::make_invoker and assorted machinery
 */

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

/**
 * \brief Wraps the passed function, so that it is always invoked in the context of the loop.
 *
 * Returns a std::function with the same arguments as the passed function.
 * The returned function can be called in any thread, as result the passed function is called
 * asynchronously with the same arguments in the loop's thread.
 */
template<typename F>
auto make_invoker(event_loop& loop, F && f)
{
	return [handler = thread_invoker(loop), cf = std::forward<F>(f)](auto &&... args) mutable -> decltype(f(std::forward<decltype(args)>(args)...), void())
	{
		auto cb = [cf = std::move(cf), targs = std::make_tuple(std::forward<decltype(args)>(args)...)] {
			std::apply(cf, targs);
		};
		handler.send_event<invoker_event>(std::move(cb));
	};
}

template<typename F>
auto make_invoker(event_handler& h, F && f)
{
	return make_invoker(h.event_loop_, std::forward<F>(f));
}


typedef std::function<void(std::function<void()>)> invoker_factory;

/**
 * \brief Creates an invoker factory.
 *
 * It is slower than building an invoker directly. Only use this
 * if the abstraction is needed.
 */
invoker_factory FZ_PUBLIC_SYMBOL get_invoker_factory(event_loop& loop);

/**
 * \brief Creates an invoker using the given factory
 *
 * Allows creating invokers independent of fz::event_loop,
 * useful when interfacing with third-party event loops
 * such as GUI frameworks, see also \ref glue/wxinvoker.hpp
 */
template<typename F>
auto make_invoker(invoker_factory const& inv, F && f)
{
	return [inv, cf = std::forward<F>(f)](auto &&... args) mutable -> decltype(f(std::forward<decltype(args)>(args)...), void())
	{
		auto cb = [cf = std::move(cf), targs = std::make_tuple(std::forward<decltype(args)>(args)...)] {
			std::apply(cf, targs);
		};
		inv(cb);
	};
}

}

#endif
