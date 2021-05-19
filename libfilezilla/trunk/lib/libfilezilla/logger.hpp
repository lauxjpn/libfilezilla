#ifndef LIBFILEZILLA_LOGGER_HEADER
#define LIBFILEZILLA_LOGGER_HEADER

/** \file
 * \brief Interface for logging
 */

#include "format.hpp"

#include <atomic>

namespace fz {
namespace logmsg
{
	enum type : uint64_t
	{
		/// Generic status messages aimed at the user
		status        = 1ull,

		/// Error messages aimed at the user
		error         = 1ull << 1,

		/// Commands, aimed at the users
		command       = 1ull << 2,

		/// Replies, aimed at the users
		reply         = 1ull << 3,

		/// Debug messages aimed at developers
		debug_warning = 1ull << 4,
		debug_info    = 1ull << 5,
		debug_verbose = 1ull << 6,
		debug_debug   = 1ull << 7,


		private1     = 1ull << 31,
		private32    = 1ull << 63
	};
}

/**
 * \brief Abstract interface for logging strings.
 *
 * Each log message has a type. Logging can be enabled or disabled for each type.
 *
 * The actual string to log gets assembled from the format string and its
 * arguments only if the type is supposed to be logged.
 */
class logger_interface
{
public:
	logger_interface() = default;
	virtual ~logger_interface() = default;

	logger_interface(logger_interface const&) = delete;
	logger_interface& operator=(logger_interface const&) = delete;


	/// The one thing you need to override
	virtual void do_log(logmsg::type t, std::wstring && msg) = 0;

	/// The \arg fmt argument is a format string suitable for fz::sprintf
	template<typename String, typename...Args>
	void log(logmsg::type t, String&& fmt, Args&& ...args)
	{
		if (should_log(t)) {
			std::wstring formatted = fz::to_wstring(fz::sprintf(std::forward<String>(fmt), std::forward<Args>(args)...));
			do_log(t, std::move(formatted));
		}
	}

	/// Logs the raw string, it is not treated as format string
	template<typename String>
	void log_raw(logmsg::type t, String&& msg)
	{
		if (should_log(t)) {
			std::wstring formatted = fz::to_wstring(std::forward<String>(msg));
			do_log(t, std::move(formatted));
		}
	}

	bool should_log(logmsg::type t) const {
		return level_ & t;
	}

	/// Sets which message types should be logged
	virtual void set_all(logmsg::type t) {
		level_ = t;
	}

	/// Sets whether the given types should be logged
	void set(logmsg::type t, bool flag) {
		if (flag) {
			enable(t);
		}
		else {
			disable(t);
		}
	}

	/// Enables logging for the passed message types
	virtual void enable(logmsg::type t) {
		level_ |= t;
	}

	/// Disables logging for the passed message types
	virtual void disable(logmsg::type t) {
		level_ &= ~t;
	}

protected:
	std::atomic<uint64_t> level_{logmsg::status | logmsg::error | logmsg::command | logmsg::reply};
};
}

#endif

