#ifndef LIBFILEZILLA_JSON_HEADER
#define LIBFILEZILLA_JSON_HEADER

#include "string.hpp"

#include <map>

namespace fz {
class FZ_PUBLIC_SYMBOL json
{
public:
	enum node_type {
		none,
		object,
		array, // todo
		string,
		number
	};

	json() noexcept = default;

	explicit json(node_type t)
	    : type_(t)
	{}

	node_type type() const {
		return type_;
	}

	std::string string_value() const {
		return type_ == string ? value_ : "";
	}

	template<typename T>
	T number_value() const {
		return type_ == number ? to_integral<T>(value_) : T{};
	}

	void erase(std::string const& name) {
		children_.erase(name);
	}
	json& operator[](std::string const& name) {
		return children_[name];
	}

	uint64_t uint_value() const;

	bool set(std::string const& name, json const& j);
	bool set(std::string const& name, std::string_view const& s);
	bool set(std::string const& name, int64_t v);
	bool set(std::string const& name, uint64_t v);

	explicit operator bool() const { return type_ != none; }

	std::string to_string() const;

	static json parse(std::string_view const& v);

	void clear();

private:
	static json parse(char const*& p, char const* end, int depth);
	node_type type_{none};

	// todo variant
	std::map<std::string, json, std::less<>> children_;
	std::string value_;
};
}

#endif
