#ifndef LIBFILEZILLA_HOSTNAME_LOOKUP_HEADER
#define LIBFILEZILLA_HOSTNAME_LOOKUP_HEADER

#include "libfilezilla.hpp"
#include "iputils.hpp"
#include "event_handler.hpp"

namespace fz {
class FZ_PUBLIC_SYMBOL hostname_lookup
{
public:
	hostname_lookup(thread_pool& pool, event_handler& evt_handler);
	~hostname_lookup();

	bool lookup(native_string const& host, address_type family = address_type::unknown);

private:
	class impl;
	impl* impl_{};
};

/// \private
struct hostname_lookup_event_type {};

typedef simple_event<hostname_lookup_event_type, hostname_lookup*, int, std::vector<std::string>> hostname_lookup_event;
}

#endif