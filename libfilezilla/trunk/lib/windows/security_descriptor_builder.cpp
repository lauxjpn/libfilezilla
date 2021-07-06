#include "security_descriptor_builder.hpp"

#ifdef FZ_WINDOWS

namespace fz {
security_descriptor_builder::~security_descriptor_builder()
{
	delete[] acl_;
	delete[] user_;
}

void security_descriptor_builder::add(entity e, DWORD rights)
{
	delete[] acl_;
	acl_ = nullptr;
	rights_[e] = rights;
}

ACL* security_descriptor_builder::get_acl()
{
	if (acl_) {
		return acl_;
	}

	if (!init_user()) {
		return nullptr;
	}

	DWORD needed = static_cast<DWORD>(sizeof(ACL) + (sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + SECURITY_MAX_SID_SIZE) * rights_.size());

	ACL* acl = reinterpret_cast<ACL*>(new uint8_t[needed]);
	if (InitializeAcl(acl, needed, ACL_REVISION)) {
		for (auto it = rights_.cbegin(); acl && it != rights_.cend(); ++it) {
			auto [sid, del] = get_sid(it->first);
			if (!sid || !AddAccessAllowedAce(acl, ACL_REVISION, it->second, sid)) {
				delete[] acl;
				acl = nullptr;
			}
			if (del) {
				delete[] sid;
			}
		}
		acl_ = acl;
	}
	else {
		delete[] acl;
	}

	if (acl_) {
		InitializeSecurityDescriptor(&sd_, SECURITY_DESCRIPTOR_REVISION);
		SetSecurityDescriptorDacl(&sd_, TRUE, acl_, FALSE);
		SetSecurityDescriptorOwner(&sd_, user_->User.Sid, FALSE);
		SetSecurityDescriptorGroup(&sd_, NULL, FALSE);
		SetSecurityDescriptorSacl(&sd_, FALSE, NULL, FALSE);
	}

	return acl_;
}

SECURITY_DESCRIPTOR* security_descriptor_builder::get_sd()
{
	init_user();
	get_acl();
	if (!user_ || !acl_) {
		return nullptr;
	}

	return &sd_;
}

std::pair<PSID, bool> security_descriptor_builder::get_sid(entity e)
{
	if (e == self) {
		init_user();
		return { user_ ? user_->User.Sid : nullptr, false };
	}
	else {
		PSID p = new unsigned char[SECURITY_MAX_SID_SIZE];
		DWORD l = SECURITY_MAX_SID_SIZE;
		if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, p, &l)) {
			delete[] p;
			return { nullptr, false };
		}
		return { p, true };
	}
}

bool security_descriptor_builder::init_user()
{
	if (user_) {
		return true;
	}

	HANDLE token{ INVALID_HANDLE_VALUE };
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return false;
	}

	DWORD needed{};
	GetTokenInformation(token, TokenUser, NULL, 0, &needed);
	if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
		TOKEN_USER* user = reinterpret_cast<TOKEN_USER*>(new uint8_t[needed]);
		if (!GetTokenInformation(token, TokenUser, user, needed, &needed)) {
			delete[] user;
		}
		else {
			user_ = user;
		}
	}

	CloseHandle(token);

	return user_ != nullptr;
}
}
#endif