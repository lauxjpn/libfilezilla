#include "libfilezilla/encode.hpp"
#include "libfilezilla/util.hpp"

#include "test_utils.hpp"

#include <string.h>

class util_test final : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(util_test);
	CPPUNIT_TEST(test_random);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp() {}
	void tearDown() {}

	void test_random();
};

CPPUNIT_TEST_SUITE_REGISTRATION(util_test);

void util_test::test_random()
{
	auto const first = fz::hex_encode<std::string>(fz::random_bytes(64));
	auto const second = fz::hex_encode<std::string>(fz::random_bytes(64));

	CPPUNIT_ASSERT(!first.empty());
	CPPNNIT_ASSERT(first != second);
}
