#ifndef LIBFILEZILLA_GLUE_WX_HEADER
#define LIBFILEZILLA_GLUE_WX_HEADER

#include <wx/string.h>

#include "../format.hpp"
#include "../string.hpp"

namespace fz {
template<typename T, typename = std::enable_if_t<std::is_same_v<wxString, typename std::decay_t<T>>>>
inline std::wstring to_wstring(T const& s) {
	return s.ToStdWstring();
}

template<>
inline wxString str_tolower_ascii(wxString const& s)
{
	wxString ret = s;
	// wxString is just broken, can't even use range-based for loops with it.
	for (auto it = ret.begin(); it != ret.end(); ++it) {
		*it = tolower_ascii(static_cast<wxChar>(*it));
	}
	return ret;
}

template<typename T, typename = std::enable_if_t<std::is_same_v<wxString, typename std::decay_t<T>>>>
inline native_string to_native(T const& in)
{
	return to_native(in.ToStdWstring());
}

template<typename T, typename = std::enable_if_t<std::is_same_v<wxString, typename std::decay_t<T>>>>
inline std::string to_utf8(T const& s)
{
	return to_utf8(s.ToStdWstring());
}

template<typename... Args>
std::wstring sprintf(wxString const& fmt, Args&&... args)
{
	return sprintf(fmt.ToStdWstring(), std::forward<Args>(args)...);
}

}

#endif
