#ifndef LIBFILEZILLA_WINDOWS_SECURITY_DESCRIPTOR_BUILDER_HEADER
#define LIBFILEZILLA_WINDOWS_SECURITY_DESCRIPTOR_BUILDER_HEADER

#include "../libfilezilla/libfilezilla.hpp"
#include "../libfilezilla/glue/windows.hpp"

#ifdef FZ_WINDOWS

#include <map>

namespace fz {
class security_descriptor_builder final
{
public:
	enum entity {
		self,
		administrators
	};

	security_descriptor_builder() = default;
	~security_descriptor_builder();

	security_descriptor_builder(security_descriptor_builder const&) = delete;
	security_descriptor_builder& operator=(security_descriptor_builder const&) = delete;

	void add(entity e, DWORD rights = GENERIC_ALL | STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL);

	ACL* get_acl();
	SECURITY_DESCRIPTOR* get_sd();

private:
	std::pair<PSID, bool> get_sid(entity e);
	bool init_user();

	std::map<entity, DWORD> rights_;

	TOKEN_USER* user_{};
	ACL* acl_{};
	SECURITY_DESCRIPTOR sd_{};
};
}

#endif
#endif
