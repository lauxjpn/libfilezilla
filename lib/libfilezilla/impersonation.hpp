#ifndef LIBFILEZILLA_IMPERSONATION_HEADER
#define LIBFILEZILLA_IMPERSONATION_HEADER

#include "string.hpp"

#if FZ_UNIX

#include <memory>

namespace fz {

enum class impersonation_flag
{
	pwless
};

class impersonation_token_impl;
class FZ_PUBLIC_SYMBOL impersonation_token final
{
public:
	enum type {
		pwless
	};

	impersonation_token();

	explicit impersonation_token(fz::native_string const& username, fz::native_string const& password);
	explicit impersonation_token(fz::native_string const& username, impersonation_flag flag);

	~impersonation_token() noexcept;

	explicit operator bool() const {
		return impl_.operator bool();
	}

	fz::native_string username() const;

private:
	friend class impersonation_token_impl;
	std::unique_ptr<impersonation_token_impl> impl_;
};

// Applies to the entire current process
bool FZ_PUBLIC_SYMBOL set_process_impersonation(impersonation_token const& token);

}
#endif

#endif
