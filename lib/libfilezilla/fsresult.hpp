#ifndef LIBFILEZILLA_FSRESULT_HEADER
#define LIBFILEZILLA_FSRESULT_HEADER

#include "private/visibility.hpp"

#include <stdint.h>

namespace fz {
/**
 * \brief Small class to return filesystem errors
 *
 * Note that now all system errors are recognized in all situations,
 * "other" is always a possible error value even if another category
 * would fit better.
 *
 * The raw error code isn't always available. If available, it is
 * the value of errno/GetLastError() when the failure occurred.
 */
class FZ_PUBLIC_SYMBOL result final
{
public:
	enum error {
		ok,

		/// Invalid arguments, syntax error
		invalid,

		/// Permission denied
		noperm,

		/// Requested file does not exist or is not a file
		nofile,

		/// Requested dir does not exist or is not a dir
		nodir,

		/// Out of disk space
		nospace,

		/// Some other error
		other
	};

#if FZ_WINDOWS
	typedef uint32_t raw_t; // DWORD alternative without windows.h
#else
	typedef int raw_t;
#endif

	explicit operator bool() const { return error_ == 0; }

	error error_{};

	raw_t raw_{};
};

}

#endif
