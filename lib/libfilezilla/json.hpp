#ifndef LIBFILEZILLA_JSON_HEADER
#define LIBFILEZILLA_JSON_HEADER

#include "string.hpp"

#include <map>
#include <variant>

namespace fz {

enum class json_type {
	none,
	null,
	object,
	array,
	string,
	number,
	boolean
};

class FZ_PUBLIC_SYMBOL json final
{
public:
	json() noexcept = default;
	json(json const&) = default;
	json(json &&) noexcept = default;

	explicit json(json_type t)
	{
		set_type(t);
	}

	json_type type() const {
		return type_;
	}

	std::string string_value() const {
		return type_ == json_type::string ? std::get<0>(value_) : "";
	}

	template<typename T>
	T number_value() const {
		return type_ == json_type::number ? to_integral<T>(std::get<0>(value_)) : T{};
	}

	bool bool_value() const {
		return type_ == json_type::boolean ? std::get<3>(value_) : false;
	}

	void erase(std::string const& name);

	json const& operator[](std::string const& name) const;
	json& operator[](std::string const& name);

	json const& operator[](size_t i) const;
	json& operator[](size_t i);

	size_t children() const;

	template<typename Bool, std::enable_if_t<std::is_same_v<bool, typename std::decay_t<Bool>>, int> = 0>
	json& operator=(Bool b) {
		type_ = json_type::boolean;
		value_ = b;
		return *this;
	}

	template<typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<bool, typename std::decay_t<T>>, int> = 0>
	json& operator=(T n) {
		type_ = json_type::number;
		value_ = fz::to_string(n);
		return *this;
	}

	json& operator=(std::string_view const& v);

	json& operator=(json const&) = default;
	json& operator=(json &&) noexcept = default;

	explicit operator bool() const { return type_ != json_type::none; }

	std::string to_string(bool pretty = false, size_t depth = 0) const;

	static json parse(std::string_view const& v, size_t max_depth = 20);

	void clear();

private:
	bool check_type(json_type t);
	void set_type(json_type t);

	static json parse(char const*& p, char const* end, size_t max_depth);

	typedef std::variant<std::string, std::map<std::string, json, std::less<>>, std::vector<json>, bool> value_type;
	value_type value_;
	json_type type_{json_type::none};
};
}

#endif
