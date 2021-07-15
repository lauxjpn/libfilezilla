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
		number,
		boolean
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
		if (type_ == none) {
			type_ = object;
		}
		return children_[name];
	}

	json const& operator[](std::string const& name) const;

	bool set(std::string const& name, json const& j);
	bool set(std::string const& name, std::string_view const& s);
	template<typename Bool, std::enable_if_t<std::is_same_v<bool, typename std::decay_t<Bool>>, int> = 0>
	bool set(std::string const& name, Bool b)
	{
		json j(boolean);
		j.value_ = b ? "true" : "false";
		return set(name, j);
	}

	bool set(std::string const& name, int64_t v);
	bool set(std::string const& name, uint64_t v);

	bool append(json const& j);
	bool append(std::string_view const& s);
	bool append(int64_t v);
	bool append(uint64_t v);

	template<typename Bool, std::enable_if_t<std::is_same_v<bool, typename std::decay_t<Bool>>, int> = 0>
	json& operator=(Bool b) {
		clear();
		type_ = boolean;
		value_ = b ? "true" : "false";
		return *this;
	}

	json& operator=(std::string_view const& v);

	explicit operator bool() const { return type_ != none; }

	std::string to_string() const;

	static json parse(std::string_view const& v);

	void clear();

private:
	static json parse(char const*& p, char const* end, int depth);
	node_type type_{none};

	// todo variant
	std::map<std::string, json, std::less<>> children_;
	std::vector<json> entries_;
	std::string value_;
};
}

#endif
