#include "../lib/libfilezilla/buffer.hpp"

#include "test_utils.hpp"

#include <string.h>

class buffer_test final : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(buffer_test);
	CPPUNIT_TEST(test_simple);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp() {}
	void tearDown() {}

	void test_simple();
};

CPPUNIT_TEST_SUITE_REGISTRATION(buffer_test);

void buffer_test::test_simple()
{
	fz::buffer buf;
	buf.append("foo");
	buf.append("bar");

	ASSERT_EQUAL(size_t(6), buf.size());

	buf.consume(3);
	buf.append("baz");

	ASSERT_EQUAL(size_t(6), buf.size());

	fz::buffer buf2;
	memcpy(buf2.get(42), "barbaz", 6);
	buf2.add(6);

	CPPUNIT_ASSERT(buf == buf2);
}
