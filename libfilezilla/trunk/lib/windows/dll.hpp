#ifndef LIBFILEZILLA_WINDOWS_DLL_HEADER
#define LIBFILEZILLA_WINDOWS_DLL_HEADER

#include "../libfilezilla/glue/windows.hpp"

namespace fz {
class dll final
{
public:
	explicit dll(wchar_t const* name)
	{
		h_ = LoadLibraryW(name);
	}

	~dll() {
		if (h_) {
			FreeLibrary(h_);
		}
	}

	dll(dll const&) = delete;
	dll& operator=(dll const&) = delete;

	explicit operator bool() const {
		return h_ != nullptr;
	}

	HMODULE h_{};
};
}

#endif

