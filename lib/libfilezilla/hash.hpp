#ifndef LIBFILEZILLA_HASH_HEADER
#define LIBFILEZILLA_HASH_HEADER

/** \file
 * \brief Collection of cryptographic hash and MAC functions
 */

#include "libfilezilla.hpp"

#include <vector>
#include <string>

namespace fz {

/// List of supported hashing algorithms
enum class hash_algorithm
{
	md5,
	sha1,
	sha256,
	sha512
};

/// Accumulator for hashing large amounts of data
class FZ_PUBLIC_SYMBOL hash_accumulator final
{
public:
	/// Creates an initialized accumulator for the passed algorithm
	hash_accumulator(hash_algorithm algorithm);
	~hash_accumulator();

	hash_accumulator(hash_accumulator const&) = delete;
	hash_accumulator& operator=(hash_accumulator const&) = delete;

	void reinit();

	void update(std::string const& data);
	void update(std::vector<uint8_t> const& data);
	void update(uint8_t const* data, size_t size);
	void update(uint8_t in) {
		update(&in, 1);
	}

	/// Returns the raw digest and reinitalizes the accumulator
	std::vector<uint8_t> digest();

	operator std::vector<uint8_t>() {
		return digest();
	}

	template<typename T>
	hash_accumulator& operator<<(T && in) {
		update(std::forward<T>(in));
		return *this;
	}

	class impl;
private:
	impl* impl_;
};

/** \brief Standard MD5
 *
 * Insecure, avoid using this
 */
std::vector<uint8_t> FZ_PUBLIC_SYMBOL md5(std::string const& data);
std::vector<uint8_t> FZ_PUBLIC_SYMBOL md5(std::vector<uint8_t> const& data);

/// \brief Standard SHA256
std::vector<uint8_t> FZ_PUBLIC_SYMBOL sha256(std::string const& data);
std::vector<uint8_t> FZ_PUBLIC_SYMBOL sha256(std::vector<uint8_t> const& data);

/// \brief Standard HMAC using SHA256
std::vector<uint8_t> FZ_PUBLIC_SYMBOL hmac_sha256(std::string const& key, std::string const& data);
std::vector<uint8_t> FZ_PUBLIC_SYMBOL hmac_sha256(std::vector<uint8_t> const& key, std::vector<uint8_t> const& data);
std::vector<uint8_t> FZ_PUBLIC_SYMBOL hmac_sha256(std::vector<uint8_t> const& key, std::string const& data);
std::vector<uint8_t> FZ_PUBLIC_SYMBOL hmac_sha256(std::string const& key, std::vector<uint8_t> const& data);

}

#endif
