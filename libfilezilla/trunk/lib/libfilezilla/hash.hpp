#ifndef LIBFILEZILLA_HASH_HEADER
#define LIBFILEZILLA_HASH_HEADER

/** \file
 * \brief Collection of cryptographic hash and MAC functions
 */

#include "libfilezilla.hpp"

#include <vector>
#include <string>

namespace fz {

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
