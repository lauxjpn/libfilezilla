#include "libfilezilla/event.hpp"

#include <map>
#include <libfilezilla/mutex.hpp>

namespace fz {

size_t get_unique_type_id(std::type_info const& id)
{
	std::string name = id.name();

	static fz::mutex m;
	
	fz::scoped_lock l(m);

	static std::map<std::string, size_t> eventTypes;

	auto it = eventTypes.find(name);
	if (it == eventTypes.end()) {
		eventTypes.insert(std::make_pair(name, eventTypes.size()));
		return eventTypes.size() - 1;
	}
	else {
		return it->second;
	}
}

}
