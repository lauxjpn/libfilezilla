#include "libfilezilla/encode.hpp"
#include "libfilezilla/json.hpp"

#include "string.h"

namespace fz {
void json::set_type(json_type t)
{
	type_ = t;
	switch (t) {
		case json_type::object:
			value_ = value_type{std::in_place_type<std::map<std::string, json, std::less<>>>};
			break;
		case json_type::array:
			value_ = value_type{std::in_place_type<std::vector<json>>};
			break;
		case json_type::boolean:
			value_ = false;
			break;
		default:
			value_ = value_type{std::in_place_type<std::string>};
			break;
	}
}

bool json::check_type(json_type t)
{
	if (type_ == t) {
		return true;
	}
	if (type_ == json_type::none) {
		set_type(t);
		return true;
	}

	return false;
}

void json::erase(std::string const& name)
{
	if (type_ == json_type::object) {
		std::get<1>(value_).erase(name);
	}
}

json const& json::operator[](std::string const& name) const
{
	static json const nil;
	if (type_ != json_type::object) {
		return nil;
	}
	auto const& m = std::get<1>(value_);
	auto it = m.find(name);
	if (it == m.end()) {
		return nil;
	}
	else {
		return it->second;
	}
}

json& json::operator[](std::string const& name)
{
	if (!check_type(json_type::object)) {
		static thread_local json nil;
		return nil;
	}
	return std::get<1>(value_)[name];
}

json const& json::operator[](size_t i) const
{
	static json const nil;
	if (type_ != json_type::array || i >= std::get<2>(value_).size()) {
		return nil;
	}
	return std::get<2>(value_)[i];
}

json& json::operator[](size_t i)
{
	if (!check_type(json_type::array)) {
		static thread_local json nil;
		return nil;
	}
	auto & v = std::get<2>(value_);
	if (v.size() <= i) {
		v.resize(i + 1);
	}
	return v[i];
}

size_t json::children() const
{
	if (type_ == json_type::array) {
		return std::get<2>(value_).size();
	}
	else if (type_ == json_type::object) {
		return std::get<1>(value_).size();
	}
	return 0;
}

void json::clear()
{
	type_ = json_type::none;
	value_ = value_type();
}

namespace {
void json_append_escaped(std::string& out, std::string const& s)
{
	out.reserve(s.size());
	for (auto & c : s) {
		switch (c) {
		case '\r':
			out += "\\r";
			break;
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\t':
			out += "\\t";
			break;
		case '\b':
			out += "\\b";
			break;
		case '\f':
			out += "\\f";
			break;
		default:
			out += c;
		}
	}
}
}

std::string json::to_string(bool pretty, size_t depth) const
{
	std::string ret;
	switch (type_) {
	case json_type::object: {
		ret += '{';
		if (pretty) {
			ret += '\n';
			ret.append(depth * 2 + 2, ' ');
		}
		bool first{true};
		for (auto const& c : std::get<1>(value_)) {
			if (!c.second) {
				continue;
			}
			if (first) {
				first = false;
			}
			else {
				ret += ',';
				if (pretty) {
					ret += '\n';
					ret.append(depth * 2 + 2, ' ');
				}
			}
			ret += '"';
			json_append_escaped(ret, c.first);
			ret += "\":";
			if (pretty) {
				ret += ' ';
			}
			ret += c.second.to_string(pretty, depth + 1);
		}
		if (pretty) {
			ret += '\n';
			ret.append(depth * 2, ' ');
		}
		ret += '}';
		break;
	}
	case json_type::array: {
		ret += '[';
		if (pretty) {
			ret += '\n';
			ret.append(depth * 2 + 2, ' ');
		}
		bool first = true;
		for (auto const& c : std::get<2>(value_)) {
			if (first) {
				first = false;
			}
			else {
				ret += ',';
				if (pretty) {
					ret += '\n';
					ret.append(depth * 2 + 2, ' ');
				}
			}
			if (!c) {
				ret += "null";
			}
			else {
				ret += c.to_string(pretty, depth + 1);
			}
		}
		if (pretty) {
			ret += '\n';
			ret.append(depth * 2, ' ');
		}
		ret += ']';
		break;
	}
	case json_type::boolean:
		ret = std::get<3>(value_) ? "true" : "false";
		break;
	case json_type::number:
		ret = std::get<0>(value_);
		break;
	case json_type::null:
		ret = "null";
		break;
	case json_type::string:
		ret = '"';
		json_append_escaped(ret, std::get<0>(value_));
		ret += '"';
		break;
	case json_type::none:
		break;
	}

	return ret;
}

json json::parse(std::string_view const& s, size_t max_depth)
{
	if (s.empty()) {
		return {};
	}

	auto p = s.data();
	return parse(p, s.data() + s.size(), max_depth);
}

namespace {
void skip_ws(char const*& p, char const* end)
{
	while (p < end) {
		char c = *p;
		switch (c) {
		case ' ':
		case '\r':
		case '\n':
		case '\t':
			++p;
			break;
		default:
			return;
		}
	}
}

// Leading " has already been consumed
// Consumes trailing "
std::pair<std::string, bool> json_unescape_string(char const*& p, char const* end, bool allow_null)
{
	std::string ret;

	bool in_escape{};
	while (p < end) {
		char c = *(p++);
		if (in_escape) {
			in_escape = false;
			switch (c) {
				case '"':
					ret += '"';
					break;
				case '\\':
					ret += '\\';
					break;
				case '/':
					ret += '/';
					break;
				case 'b':
					ret += '\b';
					break;
				case 'f':
					ret += '\f';
					break;
				case 'n':
					ret += '\n';
					break;
				case 'r':
					ret += '\r';
					break;
				case 't':
					ret += '\t';
					break;
				case 'u': {
					wchar_t u{};
					if (end - p < 4) {
						p = end;
						return {};
					}
					for (size_t i = 0; i < 4; ++i) {
						int h = hex_char_to_int(*(p++));
						if (h == -1) {
							p = end;
							return {};
						}
						u <<= 4;
						u += static_cast<wchar_t>(h);
					}
					if (!u && !allow_null) {
						p = end;
						return {};
					}
					auto u8 = fz::to_utf8(std::wstring_view(&u, 1));
					if (u8.empty()) {
						p = end;
						return {};
					}
					ret += u8;
					break;
				}
				default:
					p = end;
					return {};
			}
		}
		else if (c == '"') {
			return {ret, true};
		}
		else if (c == '\\') {
			in_escape = true;
		}
		else if (!c && !allow_null) {
			p = end;
			return {};
		}
		else {
			ret += c;
		}
	}

	return {};
}
}

json json::parse(char const*& p, char const* end, size_t max_depth)
{
	if (!max_depth) {
		return {};
	}

	skip_ws(p, end);
	if (p == end) {
		return {};
	}


	json j;
	if (*p == '"') {
		++p;
		auto [s, r] = json_unescape_string(p, end, false);
		if (!r) {
			return {};
		}

		j.type_ = json_type::string;
		j.value_ = std::move(s);
	}
	else if (*p == '{') {
		++p;

		std::map<std::string, json, std::less<>> children;
		while (true) {
			skip_ws(p, end);
			if (p == end) {
				return {};
			}
			if (*p == '}') {
				++p;
				break;
			}

			if (!children.empty()) {
				if (*(p++) != ',') {
					return {};
				}
				skip_ws(p, end);
				if (p == end) {
					return {};
				}
				if (*p == '}') {
					++p;
					break;
				}
			}

			if (*(p++) != '"') {
				return {};
			}
			auto [name, r] = json_unescape_string(p, end, false);
			if (!r || name.empty()) {
				return {};
			}

			skip_ws(p, end);
			if (p == end || *(p++) != ':') {
				return {};
			}

			auto v = parse(p, end, max_depth - 1);
			if (!v) {
				return {};
			}
			if (!children.emplace(std::move(name), std::move(v)).second) {
				return {};
			}
		}
		j.type_ = json_type::object;
		j.value_ = std::move(children);
	}
	else if (*p == '[') {
		++p;

		std::vector<json> children;
		while (true) {
			skip_ws(p, end);
			if (p == end) {
				return {};
			}
			if (*p == ']') {
				++p;
				break;
			}

			if (!children.empty()) {
				if (*(p++) != ',') {
					return {};
				}
				skip_ws(p, end);
				if (p == end) {
					return {};
				}
				if (*p == ']') {
					++p;
					break;
				}
			}

			auto v = parse(p, end, max_depth - 1);
			if (!v) {
				return {};
			}
			children.emplace_back(std::move(v));
		}
		j.type_ = json_type::array;
		j.value_ = std::move(children);
	}
	else if ((*p >= '0' && *p <= '9') || *p == '-') {
		std::string v;
		v = *(p++);
		while (p < end && *p >= '0' && *p <= '9') {
			v += *(p++);
		}
		j.type_ = json_type::object;
		j.value_ = std::move(v);
	}
	else if (end - p >= 4 && !memcmp(p, "null", 4)) {
		j.type_ = json_type::null;
		p += 4;
	}
	else if (end - p >= 4 && !memcmp(p, "true", 4)) {
		j.type_ = json_type::boolean;
		j.value_ = true;
		p += 4;
	}
	else if (end - p >= 5 && !memcmp(p, "false", 5)) {
		j.type_ = json_type::boolean;
		j.value_ = false;
		p += 5;
	}

	return j;
}

json& json::operator=(std::string_view const& v)
{
	type_ = json_type::string;
	value_ = std::string(v);
	return *this;
}
}
