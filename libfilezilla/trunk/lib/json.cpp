#include "libfilezilla/json.hpp"
#include "libfilezilla/nonowning_buffer.hpp"

namespace fz {
bool json::set(std::string const& name, json const& j)
{
	if (!j || name.empty()) {
		return false;
	}

	if (type_ == none) {
		type_ = object;
	}
	else if (type_ != object) {
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

	if (type_ == none) {
		type_ = object;
	}
	else if (type_ != object) {
		return false;
	}

	json j;
	j.type_ = string;
	j.value_ = s;
	children_[name] = std::move(j);

	return true;
}

bool json::set(std::string const& name, int64_t v)
{
	if (name.empty()) {
		return false;
	}

	if (type_ == none) {
		type_ = object;
	}
	else if (type_ != object) {
		return false;
	}

	json j;
	j.type_ = number;
	j.value_ = fz::to_string(v);
	children_[name] = std::move(j);
	return true;
}

bool json::set(std::string const& name, uint64_t v)
{
	if (name.empty()) {
		return false;
	}

	if (type_ == none) {
		type_ = object;
	}
	else if (type_ != object) {
		return false;
	}

	json j;
	j.type_ = number;
	j.value_ = fz::to_string(v);
	children_[name] = std::move(j);
	return true;
}

void json::clear()
{
	type_ = none;
	children_.clear();
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
	case object:
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
	case number:
		ret = value_;
		break;
	case string:
		ret = '"';
		json_append_escaped(ret, value_);
		ret += '"';
		break;
	case none:
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
		case '\b':
		case '\f':
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
			switch (c) {
			    case 'r':
				    ret += '\r';
				    break;
			    case 'n':
				    ret += '\n';
				    break;
			    case 't':
				    ret += '\t';
				    break;
			    case 'v':
				    ret += '\v';
				    break;
			    case 'b':
				    ret += '\b';
				    break;
			    case '\\':
				    ret += '\\';
				    break;
			    case '"':
				    ret += '"';
				    break;
			    default:
				    p = end;
				    return {};
			}
		}
		else if (c == '"') {
			return {ret, true};
		}
		else if (c == '\\') {
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
		j.type_ = string;
		++p;
		auto [s, r] = json_unescape_string(p, end);
		if (!r) {
			return {};
		}

		j.value_ = std::move(s);
		return j;
	}
	else if (*p == '{') {
		j.type_ = object;
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
	else if ((*p >= '0' && *p <= '9') || *p == '-') {
		j.type_ = number;
		j.value_ = *(p++);

		while (p < end && *p >= '0' && *p <= '9') {
			j.value_ += *(p++);
		}
		return j;
	}

	return {};
}

}
