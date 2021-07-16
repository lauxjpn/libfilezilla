#include "libfilezilla/encode.hpp"
#include "libfilezilla/json.hpp"

#include "string.h"

namespace fz {
json const& json::operator[](std::string const& name) const
{
	static json const nil;
	auto it = children_.find(name);
	if (it == children_.end()) {
		return nil;
	}
	else {
		return it->second;
	}
}

bool json::set(std::string const& name, json const& j)
{
	if (!j || name.empty()) {
		return false;
	}

	if (type_ == json_type::none) {
		type_ = json_type::object;
	}
	else if (type_ != json_type::object) {
		return false;
	}

	children_[name] = j;

	return true;
}

bool json::set(std::string const& name, std::string_view const& s)
{
	if (name.empty()) {
		return false;
	}

	if (type_ == json_type::none) {
		type_ = json_type::object;
	}
	else if (type_ != json_type::object) {
		return false;
	}

	json j;
	j.type_ = json_type::string;
	j.value_ = s;
	children_[name] = std::move(j);

	return true;
}

bool json::set(std::string const& name, int64_t v)
{
	if (name.empty()) {
		return false;
	}

	if (type_ == json_type::none) {
		type_ = json_type::object;
	}
	else if (type_ != json_type::object) {
		return false;
	}

	json j;
	j.type_ = json_type::number;
	j.value_ = fz::to_string(v);
	children_[name] = std::move(j);
	return true;
}

bool json::set(std::string const& name, uint64_t v)
{
	if (name.empty()) {
		return false;
	}

	if (type_ == json_type::none) {
		type_ = json_type::object;
	}
	else if (type_ != json_type::object) {
		return false;
	}

	json j;
	j.type_ = json_type::number;
	j.value_ = fz::to_string(v);
	children_[name] = std::move(j);
	return true;
}

bool json::append(json const& j)
{
	if (!j) {
		return false;
	}

	if (type_ == json_type::none) {
		type_ = json_type::array;
	}
	else if (type_ != json_type::array) {
		return false;
	}

	entries_.push_back(j);

	return true;
}

bool json::append(std::string_view const& s)
{
	if (type_ == json_type::none) {
		type_ = json_type::array;
	}
	else if (type_ != json_type::array) {
		return false;
	}

	json j;
	j.type_ = json_type::string;
	j.value_ = s;
	entries_.emplace_back(std::move(j));

	return true;
}

bool json::append(int64_t v)
{
	if (type_ == json_type::none) {
		type_ = json_type::array;
	}
	else if (type_ != json_type::array) {
		return false;
	}

	json j;
	j.type_ = json_type::number;
	j.value_ = fz::to_string(v);
	entries_.emplace_back(std::move(j));
	return true;
}

bool json::append(uint64_t v)
{
	if (type_ == json_type::none) {
		type_ = json_type::array;
	}
	else if (type_ != json_type::array) {
		return false;
	}

	json j;
	j.type_ = json_type::number;
	j.value_ = fz::to_string(v);
	entries_.emplace_back(std::move(j));
	return true;
}

void json::clear()
{
	type_ = json_type::none;
	children_.clear();
	entries_.clear();
	value_.clear();
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

std::string json::to_string() const
{
	std::string ret;

	switch (type_) {
	case json_type::object:
		ret += '{';
		for (auto const& c : children_) {
			if (!c.second) {
				continue;
			}
			if (ret.size() > 1) {
				ret += ',';
			}
			ret += '"';
			json_append_escaped(ret, c.first);
			ret += "\":";
			ret += c.second.to_string();
		}
		ret += '}';
		break;
	case json_type::array:
		ret += '[';
		for (auto const& c : entries_) {
			if (!c) {
				continue;
			}
			if (ret.size() > 1) {
				ret += ',';
			}
			ret += c.to_string();
		}
		ret += ']';
		break;
	case json_type::boolean:
	case json_type::number:
		ret = value_;
		break;
	case json_type::null:
		ret = "null";
		break;
	case json_type::string:
		ret = '"';
		json_append_escaped(ret, value_);
		ret += '"';
		break;
	case json_type::none:
		break;
	}

	return ret;
}

json json::parse(std::string_view const& s)
{
	if (s.empty()) {
		return {};
	}

	auto p = s.data();
	return parse(p, s.data() + s.size(), 0);
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
std::pair<std::string, bool> json_unescape_string(char const*& p, char const* end)
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
		else {
			ret += c;
		}
	}

	return {};
}
}

json json::parse(char const*& p, char const* end, int depth)
{
	if (depth > 100) {
		return {};
	}

	skip_ws(p, end);
	if (p == end) {
		return {};
	}


	json j;
	if (*p == '"') {
		j.type_ = json_type::string;
		++p;
		auto [s, r] = json_unescape_string(p, end);
		if (!r) {
			return {};
		}

		j.value_ = std::move(s);
		return j;
	}
	else if (*p == '{') {
		j.type_ = json_type::object;
		++p;
		while (true) {
			skip_ws(p, end);
			if (p == end) {
				return {};
			}
			if (*p == '}') {
				++p;
				return j;
			}

			if (!j.children_.empty()) {
				if (*(p++) != ',') {
					return {};
				}
				skip_ws(p, end);
				if (p == end) {
					return {};
				}
				if (*p == '}') {
					++p;
					return j;
				}
			}

			if (*(p++) != '"') {
				return {};
			}
			auto [name, r] = json_unescape_string(p, end);
			if (!r || name.empty()) {
				return {};
			}

			skip_ws(p, end);
			if (p == end || *(p++) != ':') {
				return {};
			}

			auto v = parse(p, end, ++depth);
			if (!v) {
				return {};
			}
			j.children_[name] = std::move(v);
		}
	}
	else if (*p == '[') {
		j.type_ = json_type::array;
		++p;
		while (true) {
			skip_ws(p, end);
			if (p == end) {
				return {};
			}
			if (*p == ']') {
				++p;
				return j;
			}

			if (!j.entries_.empty()) {
				if (*(p++) != ',') {
					return {};
				}
				skip_ws(p, end);
				if (p == end) {
					return {};
				}
				if (*p == ']') {
					++p;
					return j;
				}
			}

			auto v = parse(p, end, ++depth);
			if (!v) {
				return {};
			}
			j.entries_.emplace_back(std::move(v));
		}
	}
	else if ((*p >= '0' && *p <= '9') || *p == '-') {
		j.type_ = json_type::number;
		j.value_ = *(p++);

		while (p < end && *p >= '0' && *p <= '9') {
			j.value_ += *(p++);
		}
		return j;
	}
	else if (end - p >= 4 && !memcmp(p, "null", 4)) {
		j.type_ = json_type::null;
		p += 4;
		return j;
	}
	else if (end - p >= 4 && !memcmp(p, "true", 4)) {
		j.type_ = json_type::boolean;
		j.value_ = "true";
		p += 4;
		return j;
	}
	else if (end - p >= 5 && !memcmp(p, "false", 5)) {
		j.type_ = json_type::boolean;
		j.value_ = "false";
		p += 5;
		return j;
	}

	return {};
}

json& json::operator=(std::string_view const& v)
{
	clear();
	type_ = json_type::string;
	value_ = v;
	return *this;
}
}
