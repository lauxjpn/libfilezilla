#include "libfilezilla/socket.hpp"
#include "libfilezilla/thread_pool.hpp"
#include "libfilezilla/hash.hpp"
#include "libfilezilla/util.hpp"

#include "test_utils.hpp"

#include <string.h>

class socket_test final : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(socket_test);
	CPPUNIT_TEST(test_duplex);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp() {}
	void tearDown() {}

	void test_duplex();
};

CPPUNIT_TEST_SUITE_REGISTRATION(socket_test);

namespace {
struct base : public fz::event_handler
{
	base(fz::event_loop & loop)
		: fz::event_handler(loop)
	{
	}

	void fail(int line, int error = 0)
	{
		fz::scoped_lock l(m_);
		s_.reset();
		if (failed_.empty()) {
			failed_ = fz::to_string(line);
			if (error) {
				failed_ += ' ';
				failed_ += fz::to_string(error);
			}
		}
		cond_.signal(l);
	}

	void check_done()
	{
		if (shut_ && eof_) {
			fz::scoped_lock l(m_);
			cond_.signal(l);
			s_.reset();
		}
	}

	void on_socket_event_base(fz::socket_event_source * source, fz::socket_event_flag type, int error)
	{
		if (error) {
			fail(__LINE__, error);
		}
		else if (type == fz::socket_event_flag::read) {
			for (int i = 0; i < fz::random_number(1, 20); ++i) {
				unsigned char buf[1024];

				int error;
				int r = s_->read(buf, 1024, error);
				if (!r) {
					eof_ = true;
					check_done();
					return;
				}
				else if (r == -1) {
					if (error != EAGAIN) {
						fail(__LINE__, error);
					}
					return;
				}
				else {
					received_hash_.update(buf, r);
				}
			}

			send_event(new fz::socket_event(s_.get(), fz::socket_event_flag::read, 0));
		}
		else if (type == fz::socket_event_flag::write) {
			if (sent_ > 1024 * 1024 * 10 && (fz::monotonic_clock::now() - start_) > fz::duration::from_seconds(5)) {
				int res = s_->shutdown();
				if (res && res != EAGAIN) {
					fail(__LINE__, res);
				}
				else if (!res) {
					shut_ = true;
					check_done();
				}

				return;
			}
			for (int i = 0; i < fz::random_number(1, 20); ++i) {
				auto buf = fz::random_bytes(1024);
				int error;
				int sent = s_->write(buf.data(), buf.size(), error);
				if (sent <= 0) {
					if (error != EAGAIN) {
						fail(__LINE__, error);
					}
					return;
				}
				else {
					sent_ += sent;
					sent_hash_.update(buf.data(), sent);
				}
			}
			send_event(new fz::socket_event(s_.get(), fz::socket_event_flag::write, 0));
		}
	}

	fz::hash_accumulator sent_hash_{fz::hash_algorithm::md5};
	fz::hash_accumulator received_hash_{fz::hash_algorithm::md5};

	fz::mutex m_;
	fz::condition cond_;

	fz::thread_pool pool_;

	std::unique_ptr<fz::socket> s_;

	std::string failed_;
	bool eof_{};
	bool shut_{};
	int64_t sent_{};
	fz::monotonic_clock start_{fz::monotonic_clock::now()};
};

struct client final : public base
{
	client(fz::event_loop & loop)
		: base(loop)
	{
		s_ = std::make_unique<fz::socket>(pool_, this);
	}

	virtual ~client() {
		remove_handler();
	}

	virtual void operator()(fz::event_base const& ev) override {
		fz::dispatch<fz::socket_event>(ev, this, &client::on_socket_event);
	}

	void on_socket_event(fz::socket_event_source * source, fz::socket_event_flag type, int error)
	{
		on_socket_event_base(source, type, error);
	}
};

struct server final : public base
{
	server(fz::event_loop & loop)
		: base(loop)
	{
		l_.bind("127.0.0.1");
		int res = l_.listen(fz::address_type::ipv4);
		if (res) {
			fail(__LINE__, res);
		}
	}

	virtual ~server() {
		remove_handler();
	}

	virtual void operator()(fz::event_base const& ev) override {
		fz::dispatch<fz::socket_event>(ev, this, &server::on_socket_event);
	}

	void on_socket_event(fz::socket_event_source * source, fz::socket_event_flag type, int error)
	{
		if (source == &l_) {
			if (s_) {
				fail(__LINE__);
			}
			else if (error) {
				fail(__LINE__, error);
			}
			else {
				int error;
				s_ = l_.accept(error);
				if (!s_) {
					fail(__LINE__, error);
				}
				s_->set_event_handler(this);
			}
		}
		else {
			on_socket_event_base(source, type, error);
		}
	}

	fz::listen_socket l_{pool_, this};
};
}

void socket_test::test_duplex()
{
	// Full duplex socket test of random data exchanged in both directions for 5 seconds.
	fz::event_loop server_loop;
	server s(server_loop);

	int error;
	int port  = s.l_.local_port(error);
	CPPUNIT_ASSERT(port != -1);

	fz::native_string ip = fz::to_native(s.l_.local_ip());
	CPPUNIT_ASSERT(!ip.empty());

	fz::event_loop client_loop;
	client c(client_loop);

	CPPUNIT_ASSERT(!c.s_->connect(ip, port));

	{
		fz::scoped_lock l(s.m_);
		s.cond_.wait(l);
	}
	{
		fz::scoped_lock l(c.m_);
		c.cond_.wait(l);
	}

	ASSERT_EQUAL(std::string(), c.failed_);
	ASSERT_EQUAL(std::string(), s.failed_);

	CPPUNIT_ASSERT(c.sent_hash_.digest() == s.received_hash_.digest());
	CPPUNIT_ASSERT(s.sent_hash_.digest() == c.received_hash_.digest());
}
