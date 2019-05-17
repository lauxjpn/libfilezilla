#ifndef LIBFILEZILLA_ENCRYPTION_HEADER
#define LIBFILEZILLA_ENCRYPTION_HEADER

/** \file
 * \brief Asymmetric encryption scheme using X25519
 *
 * See RFC 7748 for the X22519 specs.
 */

#include "libfilezilla.hpp"

#include <vector>
#include <string>

namespace fz {

/** \brief Represents a X25519 public key with associated salt
 *
 * \sa private_key
 */
class FZ_PUBLIC_SYMBOL public_key
{
public:
	/// Size in octets of key and salt.
	enum {
		key_size = 32,
		salt_size = 32
	};

	explicit operator bool() const {
		return key_.size() == key_size && salt_.size() == salt_size;
	}

	bool operator==(public_key const& rhs) const {
		return key_ == rhs.key_ && salt_ == rhs.salt_;
	}

	bool operator!=(public_key const& rhs) const {
		return !(*this == rhs);
	}

	bool operator<(public_key const& rhs) const {
		return key_ < rhs.key_ || (key_ == rhs.key_ && salt_ < rhs.salt_);
	}

	std::string to_base64() const;
	static public_key from_base64(std::string_view const& base64);

	std::vector<uint8_t> key_;
	std::vector<uint8_t> salt_;
};

/** \brief Represents a X25519 private key with associated salt
 *
 * \sa public_key
 */
class FZ_PUBLIC_SYMBOL private_key
{
public:
	/// Size in octets of key an salt.
	enum {
		key_size = 32,
		salt_size = 32
	};

	/// Generates a random private key
	static private_key generate();

	/// Derives a private key using PKBDF2-SHA256 from the given password and salt
	static private_key from_password(std::vector<uint8_t> const& password, std::vector<uint8_t> const& salt);
	static private_key from_password(std::string_view const& password, std::vector<uint8_t> const& salt)
	{
		return from_password(std::vector<uint8_t>(password.begin(), password.end()), salt);
	}

	explicit operator bool() const {
		return key_.size() == key_size && salt_.size() == salt_size;
	}

	std::vector<uint8_t> const& salt() const {
		return salt_;
	}

	/// Calculates the public key corresponding to the private key
	public_key pubkey() const;

	/// Calculates a shared secret using Elliptic Curve Diffie-Hellman on Curve25519 (X25519)
	std::vector<uint8_t> shared_secret(public_key const& pub) const;

	std::string to_base64() const;
	static private_key from_base64(std::string_view const& base64);

private:
	std::vector<uint8_t> key_;
	std::vector<uint8_t> salt_;
};

/** \brief Encrypt the plaintext to the given public key.
 *
 * \param authenticated if true, authenticated encryption is used.
 *
 * \par Encryption algorithm:
 *
 * Let \e M_pub be the key portion, S_e be the salt portion of the pub parameter and \e P be the plaintext.
 *
 * - First an ephemeral private key \e E_priv with corresponding public key \e E_pub and \e S_e is randomly generated
 * - Using ECDH on Curve25519 (X25519), a shared secret \e R is derived:\n
 *     <tt>R := X25519(E_priv, M_pub)</tt>
 * - From \e R, a symmetric AES256 key \e K and a nonce \e IV are derived:
 *   * <tt>K := SHA256(S_e || 0 || S || E_pub || M_pub || S_m)</tt>
 *   * <tt>IV := SHA256(S_e || 2 || S || E_pub || M_pub || S_m)</tt> if authenticated,\n
 *     <tt>IV := SHA256(S_e || 1 || S || E_pub || M_pub || S_m)</tt> otherwise
 * - The plaintext is encrypted into the ciphertext \e C' and authentication tag \e T using\n
 *   <tt>C', T := AES256-GCM(K, IV, P)</tt> if authenticated,\n
 *   <tt>C' := AES256-CTR(K, IV, P)</tt> T:='' otherwise
 * - The ciphertext \e C is returned, containing \e E_pub, \e S_e and \e T: \n
 *     <tt>C := E_pub || S_e || C' || T</tt>
 */
std::vector<uint8_t> FZ_PUBLIC_SYMBOL encrypt(std::vector<uint8_t> const& plain, public_key const& pub, bool authenticated = true);
std::vector<uint8_t> FZ_PUBLIC_SYMBOL encrypt(std::string_view const& plain, public_key const& pub, bool authenticated = true);
std::vector<uint8_t> FZ_PUBLIC_SYMBOL encrypt(uint8_t const* plain, size_t size, public_key const& pub, bool authenticated = true);

/** \brief Decrypt the ciphertext using the given private key.
 *
 * \param priv The private matching the public key that was originally used to encrypt the data
 * \param authenticated if true, authenticated encryption is used.
 *
 * \returns plaintext on success, empty container on failure
 *
 * \par Decryption algorithm:
 *
 * Let \e M_priv be the key portion and \e S_m be the salt portion of the priv parameter and \e C the ciphertext.
 *
 * - First \e C is split into \e E_pub, \e S_e, \e C' and \e T such that\n
 *   <tt>C: = E_pub || S_e || C1 || T</tt>
 * - \e M_pub is calculated from \e M_priv
 * - Using ECDH on Curve25519 (X25519), the shared secret \e R is recovered:\n
 *     <tt>R := X25519(M_priv, E_pub)</tt>
 * - From \e R, a symmetric AES256 key \e K and a nonce \e IV are derived:
 *   * <tt>K := SHA256(S_e || 0 || S || E_pub || M_pub || S_m)</tt>
 *   * <tt>IV := SHA256(S_e || 2 || S || E_pub || M_pub || S_m)</tt> if authenticated,\n
 *     <tt>IV := SHA256(S_e || 1 || S || E_pub || M_pub || S_m)</tt> otherwise
 * - The ciphertext is decrypted into the plaintext \e P using\n
 *   <tt>P, T' := AES256-GCM(K, IV, C')</tt> if authenticated,\n
 *   <tt>P := AES256-CTR(K, IV, C'), T:=''</tt> otherwise
 * - If the calculated \e T' matches \e T, then \e P is returned, otherwise decryption has failed and nothing is returned.
 */
std::vector<uint8_t> FZ_PUBLIC_SYMBOL decrypt(std::vector<uint8_t> const& chiper, private_key const& priv, bool authenticated = true);
std::vector<uint8_t> FZ_PUBLIC_SYMBOL decrypt(std::string_view const& chiper, private_key const& priv, bool authenticated = true);
std::vector<uint8_t> FZ_PUBLIC_SYMBOL decrypt(uint8_t const* cipher, size_t size, private_key const& priv, bool authenticated = true);

}
#endif
