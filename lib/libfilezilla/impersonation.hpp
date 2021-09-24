#ifndef LIBFILEZILLA_IMPERSONATION_HEADER
#define LIBFILEZILLA_IMPERSONATION_HEADER

#include "string.hpp"

#if FZ_UNIX || FZ_WINDOWS

#include <memory>

namespace fz {

#if FZ_UNIX
enum class impersonation_flag
{
	pwless
};
#endif

class impersonation_token_impl;
class FZ_PUBLIC_SYMBOL impersonation_token final
{
public:
	enum type {
		pwless
	};

	impersonation_token();

	impersonation_token(impersonation_token&&) noexcept;
	impersonation_token& operator=(impersonation_token&&) noexcept;

	explicit impersonation_token(fz::native_string const& username, fz::native_string const& password);

#if FZ_UNIX
	explicit impersonation_token(fz::native_string const& username, impersonation_flag flag);
#endif

	~impersonation_token() noexcept;

	explicit operator bool() const {
		return impl_.operator bool();
	}

	bool operator==(impersonation_token const&) const;

	fz::native_string username() const;

private:
	friend class impersonation_token_impl;
	std::unique_ptr<impersonation_token_impl> impl_;
};

#if FZ_UNIX
// Applies to the entire current process
bool FZ_PUBLIC_SYMBOL set_process_impersonation(impersonation_token const& token);
#endif

}
#endif

#endif
