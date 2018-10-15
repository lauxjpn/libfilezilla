#include "libfilezilla/signature.hpp"

#include "libfilezilla/encode.hpp"
#include "libfilezilla/util.hpp"

#include <nettle/eddsa.h>

namespace fz {

std::string public_verification_key::to_base64() const
{
	auto raw = std::string(key_.cbegin(), key_.cend());
	return fz::base64_encode(raw);
}

public_verification_key public_verification_key::from_base64(std::string const& base64)
{
	public_verification_key ret;

	auto raw = fz::base64_decode(base64);
	if (raw.size() == key_size) {
		auto p = reinterpret_cast<uint8_t const*>(&raw[0]);
		ret.key_.assign(p, p + key_size);
	}

	return ret;
}

private_signing_key private_signing_key::generate()
{
	private_signing_key ret;

	ret.key_ = fz::random_bytes(key_size);
	return ret;
}

std::string private_signing_key::to_base64() const
{
	auto raw = std::string(key_.cbegin(), key_.cend());
	return fz::base64_encode(raw);
}

private_signing_key private_signing_key::from_base64(std::string const& base64)
{
	private_signing_key ret;

	auto raw = fz::base64_decode(base64);
	if (raw.size() == key_size) {
		auto p = reinterpret_cast<uint8_t const*>(&raw[0]);
		ret.key_.assign(p, p + key_size);
	}

	return ret;
}

public_verification_key private_signing_key::pubkey() const
{
	public_verification_key ret;

	if (*this) {
		ret.key_.resize(public_verification_key::key_size);
		nettle_ed25519_sha512_public_key(ret.key_.data(), key_.data());
	}

	return ret;
}


std::vector<uint8_t> sign(uint8_t const* message, size_t const size, private_signing_key const& priv)
{
	std::vector<uint8_t> ret;

	auto const pub = priv.pubkey();
	if (priv && pub && size) {
		ret.reserve(size + signature_size);
		ret.assign(message, message + size);
		ret.resize(size + signature_size);

		nettle_ed25519_sha512_sign(pub.key_.data(), priv.data().data(), size, ret.data(), ret.data() + size);
	}

	return ret;
}

std::vector<uint8_t> sign(std::vector<uint8_t> const& message, private_signing_key const& priv)
{
	return sign(message.data(), message.size(), priv);
}

std::vector<uint8_t> sign(std::string const& message, private_signing_key const& priv)
{
	return sign(reinterpret_cast<uint8_t const*>(message.c_str()), message.size(), priv);
}


// Verify the message. Returns true iff it has been signed by the private key corresponding to the passed public key
bool verify(uint8_t const* message, size_t const size, public_verification_key const& pub)
{
	if (!message || size < signature_size) {
		return false;
	}
	return nettle_ed25519_sha512_verify(pub.key_.data(), size, message, message + size - signature_size) == 1;
}

bool verify(std::vector<uint8_t> const& message, public_verification_key const& pub)
{
	return verify(message.data(), message.size(), pub);
}

bool verify(std::string const& message, public_verification_key const& pub)
{
	return verify(reinterpret_cast<uint8_t const*>(message.c_str()), message.size(), pub);
}

}
