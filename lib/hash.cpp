#include "libfilezilla/libfilezilla.hpp"

#include "libfilezilla/hash.hpp"

#include <nettle/md5.h>
#include <nettle/sha2.h>
#include <nettle/hmac.h>

namespace fz {

namespace {
// In C++17, require ContiguousContainer
template<typename DataContainer>
std::vector<uint8_t> md5_impl(DataContainer const& in)
{
	static_assert(sizeof(typename DataContainer::value_type) == 1, "Bad container type");

	std::vector<uint8_t> ret;
	ret.resize(16);

	md5_ctx ctx_md5;
	nettle_md5_init(&ctx_md5);
	nettle_md5_update(&ctx_md5, in.size(), reinterpret_cast<uint8_t const*>(&in[0]));
	nettle_md5_digest(&ctx_md5, ret.size(), reinterpret_cast<uint8_t*>(&ret[0]));

	return ret;
}

template<typename DataContainer>
std::vector<uint8_t> sha256_impl(DataContainer const& in)
{
	static_assert(sizeof(typename DataContainer::value_type) == 1, "Bad container type");

	std::vector<uint8_t> ret;
	ret.resize(32);

	sha256_ctx ctx_sha256;
	nettle_sha256_init(&ctx_sha256);
	nettle_sha256_update(&ctx_sha256, in.size(), reinterpret_cast<uint8_t const*>(&in[0]));
	nettle_sha256_digest(&ctx_sha256, ret.size(), reinterpret_cast<uint8_t*>(&ret[0]));

	return ret;
}


template<typename KeyContainer, typename DataContainer>
std::vector<uint8_t> hmac_sha256_impl(KeyContainer const& key, DataContainer const& data)
{
	static_assert(sizeof(typename KeyContainer::value_type) == 1, "Bad container type");
	static_assert(sizeof(typename DataContainer::value_type) == 1, "Bad container type");

	std::vector<uint8_t> ret;

	hmac_sha256_ctx ctx;
	nettle_hmac_sha256_set_key(&ctx, key.size(), reinterpret_cast<uint8_t const*>(&key[0]));

	nettle_hmac_sha256_update(&ctx, data.size(), reinterpret_cast<uint8_t const*>(&data[0]));

	ret.resize(SHA256_DIGEST_SIZE);
	nettle_hmac_sha256_digest(&ctx, ret.size(), &ret[0]);

	return ret;
}
}

std::vector<uint8_t> md5(std::vector<uint8_t> const& data)
{
	return md5_impl(data);
}

std::vector<uint8_t> md5(std::string const& data)
{
	return md5_impl(data);
}

std::vector<uint8_t> sha256(std::vector<uint8_t> const& data)
{
	return sha256_impl(data);
}

std::vector<uint8_t> sha256(std::string const& data)
{
	return sha256_impl(data);
}

std::vector<uint8_t> hmac_sha256(std::string const& key, std::string const& data)
{
	return hmac_sha256_impl(key, data);
}

std::vector<uint8_t> hmac_sha256(std::vector<uint8_t> const& key, std::vector<uint8_t> const& data)
{
	return hmac_sha256_impl(key, data);
}

std::vector<uint8_t> hmac_sha256(std::vector<uint8_t> const& key, std::string const& data)
{
	return hmac_sha256_impl(key, data);
}

std::vector<uint8_t> hmac_sha256(std::string const& key, std::vector<uint8_t> const& data)
{
	return hmac_sha256_impl(key, data);
}

}
