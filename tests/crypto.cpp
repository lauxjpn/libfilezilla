#include "libfilezilla/encryption.hpp"
#include "libfilezilla/util.hpp"

#include "test_utils.hpp"

#include <string.h>

class crypto_test final : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(crypto_test);
	CPPUNIT_TEST(test_encryption);
	CPPUNIT_TEST(test_encryption_with_password);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp() {}
	void tearDown() {}

	void test_encryption();
	void test_encryption_with_password();
};

CPPUNIT_TEST_SUITE_REGISTRATION(crypto_test);

void crypto_test::test_encryption()
{
	auto priv = fz::private_key::generate();
	priv.generate();

	auto const pub = priv.pubkey();

	std::string const plain = "Hello world";

	auto cipher = fz::encrypt(plain, pub);
	CPPUNIT_ASSERT(fz::decrypt(cipher, priv) == std::vector<uint8_t>(plain.cbegin(), plain.cend()));
}


void crypto_test::test_encryption_with_password()
{
	auto const salt = fz::random_bytes(fz::private_key::salt_size);

	std::string const plain = "Hello world";
	std::vector<uint8_t> cipher;

	{
		auto priv = fz::private_key::from_password("super secret", salt);
		CPPUNIT_ASSERT(priv);

		auto const pub = priv.pubkey();

		cipher = fz::encrypt(plain, pub);
	}


	{
		auto priv = fz::private_key::from_password("super secret", salt);
		CPPUNIT_ASSERT(priv);

		CPPUNIT_ASSERT(fz::decrypt(cipher, priv) == std::vector<uint8_t>(plain.cbegin(), plain.cend()));
	}

}
