#include "libfilezilla/libfilezilla.hpp"

#ifdef FZ_WINDOWS
  #include "libfilezilla/private/windows.hpp"
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mstcpip.h>
#endif

#include "libfilezilla/socket.hpp"

#include "libfilezilla/mutex.hpp"
#include "libfilezilla/thread_pool.hpp"

#ifndef FZ_WINDOWS
  #include "libfilezilla/glue/unix.hpp"

  #define mutex mutex_override // Sadly on some platforms system headers include conflicting names
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netdb.h>
  #include <fcntl.h>
  #include <unistd.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #if !defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE)
	#include <signal.h>
  #endif
  #include <poll.h>
  #undef mutex
  #if HAVE_EVENTFD
	#include <sys/eventfd.h>
  #endif
  #if HAVE_TCP_INFO
	#include <atomic>
  #endif
#endif

#include <assert.h>
#include <string.h>

// Fixups needed on FreeBSD
#if !defined(EAI_ADDRFAMILY) && defined(EAI_FAMILY)
  #define EAI_ADDRFAMILY EAI_FAMILY
#endif

#ifndef AI_NUMERICSERV
// Some systems like Windows or OS X don't know AI_NUMERICSERV.
#define AI_NUMERICSERV 0
#endif

#define WAIT_CONNECT 0x01
#define WAIT_READ	 0x02
#define WAIT_WRITE	 0x04
#define WAIT_ACCEPT  0x08
#define WAIT_EVENTCOUNT 4

namespace fz {

namespace {
// Union for strict aliasing-safe casting between
// the different address types
union sockaddr_u
{
	sockaddr_storage storage;
	sockaddr sockaddr_;
	sockaddr_in in4;
	sockaddr_in6 in6;
};

#if HAVE_TCP_INFO
// Attempting to set a high SND_RCVBUF can actually result in a smaller
// TCP receive window scale factor.
// Through TCP_INFO it is possible to detect this, and if it is the case,
// avoid setting TCP_RCVBUF
std::atomic<int> unmodified_rcv_wscale{};
std::atomic<int> modified_rcv_wscale{};

int get_rcv_wscale(int fd) {
	tcp_info i{};
	socklen_t len = sizeof(tcp_info);
	int res = getsockopt(fd, IPPROTO_TCP, TCP_INFO, &i, &len);
	if (res) {
		return 0;
	}
	return i.tcpi_rcv_wscale;
}

#endif
}

void remove_socket_events(event_handler * handler, socket_event_source const* const source)
{
	if (!handler) {
		return;
	}

	auto socket_event_filter = [&](event_loop::Events::value_type const& ev) -> bool {
		if (ev.first != handler) {
			return false;
		}
		else if (ev.second->derived_type() == socket_event::type()) {
			return std::get<0>(static_cast<socket_event const&>(*ev.second).v_) == source;
		}
		else if (ev.second->derived_type() == hostaddress_event::type()) {
			return std::get<0>(static_cast<hostaddress_event const&>(*ev.second).v_) == source;
		}
		return false;
	};

	handler->event_loop_.filter_events(socket_event_filter);
}

void change_socket_event_handler(event_handler * old_handler, event_handler * new_handler, socket_event_source const* const source)
{
	if (!old_handler) {
		return;
	}

	if (old_handler == new_handler) {
		return;
	}

	if (!new_handler) {
		remove_socket_events(old_handler, source);
	}
	else {
		auto socket_event_filter = [&](event_loop::Events::value_type & ev) -> bool {
			if (ev.first == old_handler) {
				if (ev.second->derived_type() == socket_event::type()) {
					if (std::get<0>(static_cast<socket_event const&>(*ev.second).v_) == source) {
						ev.first = new_handler;
					}
				}
				else if (ev.second->derived_type() == hostaddress_event::type()) {
					if (std::get<0>(static_cast<hostaddress_event const&>(*ev.second).v_) == source) {
						ev.first = new_handler;
					}
				}
			}
			return false;
		};

		old_handler->event_loop_.filter_events(socket_event_filter);
	}
}

namespace {
bool has_pending_event(event_handler * handler, socket_event_source const* const source, socket_event_flag event)
{
	bool ret = false;

	auto socket_event_filter = [&](event_loop::Events::value_type const& ev) -> bool {
		if (ev.first == handler && ev.second->derived_type() == socket_event::type()) {
			auto const& socket_ev = static_cast<socket_event const&>(*ev.second).v_;
			if (std::get<0>(socket_ev) == source && std::get<1>(socket_ev) == event) {
				ret = true;
			}
		}
		return false;
	};

	handler->event_loop_.filter_events(socket_event_filter);

	return ret;
}

#ifdef FZ_WINDOWS
static int convert_msw_error_code(int error)
{
	// Takes an MSW socket error and converts it into an equivalent POSIX error code.
	switch (error)
	{
	case WSAECONNREFUSED:
		return ECONNREFUSED;
	case WSAECONNABORTED:
		return ECONNABORTED;
	case WSAEINVAL:
		return EAI_BADFLAGS;
	case WSANO_RECOVERY:
		return EAI_FAIL;
	case WSAEAFNOSUPPORT:
		return EAI_FAMILY;
	case WSA_NOT_ENOUGH_MEMORY:
		return EAI_MEMORY;
	case WSANO_DATA:
		return EAI_NODATA;
	case WSAHOST_NOT_FOUND:
		return EAI_NONAME;
	case WSATYPE_NOT_FOUND:
		return EAI_SERVICE;
	case WSAESOCKTNOSUPPORT:
		return EAI_SOCKTYPE;
	case WSAEWOULDBLOCK:
		return EAGAIN;
	case WSAEMFILE:
		return EMFILE;
	case WSAEINTR:
		return EINTR;
	case WSAEFAULT:
		return EFAULT;
	case WSAEACCES:
		return EACCES;
	case WSAETIMEDOUT:
		return ETIMEDOUT;
	case WSAECONNRESET:
		return ECONNRESET;
	case WSAEHOSTDOWN:
		return EHOSTDOWN;
	case WSAENETUNREACH:
		return ENETUNREACH;
	case WSAEADDRINUSE:
		return EADDRINUSE;
	default:
		return error;
	}
}

int last_socket_error()
{
	return convert_msw_error_code(WSAGetLastError());
}
#else
int last_socket_error()
{
	int err = errno;
#if EWOULDBLOCK != EAGAIN
	if (err == EWOULDBLOCK) {
		err = EAGAIN;
	}
#endif
	return err;
}
#endif

int set_nonblocking(socket::socket_t fd)
{
	// Set socket to non-blocking.
#ifdef FZ_WINDOWS
	unsigned long nonblock = 1;
	int res = ioctlsocket(fd, FIONBIO, &nonblock);
	if (!res) {
		return 0;
	}
	else {
		return convert_msw_error_code(WSAGetLastError());
	}
#else
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1) {
		return errno;
	}
	int res = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if (res == -1) {
		return errno;
	}
	return 0;
#endif
}

int do_set_flags(socket::socket_t fd, int flags, int flags_mask, duration const& keepalive_interval)
{
	if (flags_mask & socket::flag_nodelay) {
		const int value = (flags & socket::flag_nodelay) ? 1 : 0;
		int res = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&value, sizeof(value));
		if (res != 0) {
			return last_socket_error();
		}
	}
	if (flags_mask & socket::flag_keepalive) {
#if FZ_WINDOWS
		tcp_keepalive v{};
		v.onoff = (flags & socket::flag_keepalive) ? 1 : 0;
		v.keepalivetime = static_cast<ULONG>(keepalive_interval.get_milliseconds());
		v.keepaliveinterval = 1000;
		DWORD tmp{};
		int res = WSAIoctl(fd, SIO_KEEPALIVE_VALS, &v, sizeof(v), nullptr, 0, &tmp, nullptr, nullptr);
		if (res != 0) {
			return last_socket_error();
		}
#else
		const int value = (flags & socket::flag_keepalive) ? 1 : 0;
		int res = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&value, sizeof(value));
		if (res != 0) {
			return last_socket_error();
		}
#ifdef TCP_KEEPIDLE
		int const idle = keepalive_interval.get_seconds();
		res = setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (const char*)&idle, sizeof(idle));
		if (res != 0) {
			return last_socket_error();
		}
#else
		(void)keepalive_interval;
#endif
#endif
	}

	return 0;
}

int do_set_buffer_sizes(socket::socket_t fd, int size_read, int size_write)
{
	int ret = 0;
	if (size_read >= 0) {
#if HAVE_TCP_INFO
		// Check if setting the buffer size shrinks the window scale factor
		int const mws = modified_rcv_wscale;
		if (!mws || mws >= unmodified_rcv_wscale)
#endif
		{
			int res = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&size_read, sizeof(size_read));
			if (res != 0) {
				ret = last_socket_error();
			}
		}
	}

	if (size_write >= 0) {
		int res = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&size_write, sizeof(size_write));
		if (res != 0) {
			ret = last_socket_error();
		}
	}

	return ret;
}

#ifdef FZ_WINDOWS
class winsock_initializer final
{
public:
	winsock_initializer()
	{
		WSADATA d{};
		initialized_ = WSAStartup((2 << 8) | 8, &d) == 0;
	}

	~winsock_initializer()
	{
		if (initialized_) {
			WSACleanup();
		}
	}

private:
	bool initialized_{};
};
#endif


void close_socket_fd(socket::socket_t& fd)
{
	if (fd != -1) {
#ifdef FZ_WINDOWS
		closesocket(fd);
#else
		close(fd);
#endif
		fd = -1;
	}
}
}

class socket_thread final
{
	friend class socket_base;
	friend class socket;
	friend class listen_socket;
public:
	explicit socket_thread(socket_base* base)
		: socket_(base)
		, mutex_(false)
	{
#ifdef FZ_WINDOWS
		static winsock_initializer init;
#endif
		for (int i = 0; i < WAIT_EVENTCOUNT; ++i) {
			triggered_errors_[i] = 0;
		}
	}

	~socket_thread()
	{
		thread_.join();
		destroy_sync();
	}

	int create_sync()
	{
#ifdef FZ_WINDOWS
		if (sync_event_ == WSA_INVALID_EVENT) {
			sync_event_ = WSACreateEvent();
		}
		if (sync_event_ == WSA_INVALID_EVENT) {
			return 1;
		}
#elif defined HAVE_EVENTFD
		if (event_fd_ == -1) {
			event_fd_ = eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK);
			if (event_fd_ == -1) {
				return errno;
			}
#ifndef HAVE_POLL
			if (event_fd_ >= FD_SETSIZE) {
				destroy_sync();
				return EMFILE;
			}
#endif
		}
#else
		if (pipe_[0] == -1) {
			if (!create_pipe(pipe_)) {
				return errno;
			}

#ifndef HAVE_POLL
			if (pipe_[0] >= FD_SETSIZE) {
				destroy_sync();
				return EMFILE;
			}
#endif
		}
#endif
		return 0;
	}


	void destroy_sync()
	{
#ifdef FZ_WINDOWS
		if (sync_event_ != WSA_INVALID_EVENT) {
			WSACloseEvent(sync_event_);
		}
#elif defined HAVE_EVENTFD
		if (event_fd_ != -1) {
			::close(event_fd_);
			event_fd_ = -1;
		}
#else
		if (pipe_[0] != -1) {
			::close(pipe_[0]);
			pipe_[0] = -1;
		}
		if (pipe_[1] != -1) {
			::close(pipe_[1]);
			pipe_[1] = -1;
		}
#endif
	}

	void set_socket(socket_base* pSocket)
	{
		scoped_lock l(mutex_);
		set_socket(pSocket, l);
	}

	void set_socket(socket_base* pSocket, scoped_lock const&)
	{
		socket_ = pSocket;

		host_.clear();
		port_.clear();

		waiting_ = 0;
	}

	int connect(std::string const& host, unsigned int port)
	{
		assert(socket_);
		if (!socket_) {
			return EINVAL;
		}

		host_ = host;
		if (host_.empty()) {
			return EINVAL;
		}

		// Connect method of socket ensures port is in range
		port_ = fz::to_string(port);

		return start();
	}

	int start()
	{
		if (thread_) {
			scoped_lock l(mutex_);
			assert(threadwait_);
			waiting_ = 0;
			wakeup_thread(l);
			return 0;
		}

		int res = create_sync();
		if (res) {
			return res;
		}

		thread_ = socket_->thread_pool_.spawn([this]() { entry(); });

		if (!thread_) {
			destroy_sync();
			return EMFILE;
		}

		return 0;
	}

	// Cancels select or idle wait
	void wakeup_thread()
	{
		scoped_lock l(mutex_);
		wakeup_thread(l);
	}

	void wakeup_thread(scoped_lock & l)
	{
		if (!thread_ || quit_) {
			return;
		}

		if (threadwait_) {
			threadwait_ = false;
			condition_.signal(l);
			return;
		}

#ifdef FZ_WINDOWS
		WSASetEvent(sync_event_);
#elif defined(HAVE_EVENTFD)
		uint64_t tmp = 1;

		int ret;
		do {
			ret = write(event_fd_, &tmp, 8);
		} while (ret == -1 && errno == EINTR);
#else
		char tmp = 0;

		int ret;
		do {
			ret = write(pipe_[1], &tmp, 1);
		} while (ret == -1 && errno == EINTR);
#endif
	}

protected:
	static socket::socket_t create_socket_fd(addrinfo const& addr)
	{
		socket::socket_t fd;
#if defined(SOCK_CLOEXEC) && !defined(FZ_WINDOWS)
		fd = ::socket(addr.ai_family, addr.ai_socktype | SOCK_CLOEXEC, addr.ai_protocol);
		if (fd == -1 && errno == EINVAL)
#endif
		{
			fd = ::socket(addr.ai_family, addr.ai_socktype, addr.ai_protocol);

#if !defined(FZ_WINDOWS)
			set_cloexec(fd);
#endif
		}

#if !defined(FZ_WINDOWS) && !defined(HAVE_POLL)
		if (fd >= FD_SETSIZE) {
			close(fd);
			errno = EMFILE;
			return -1;
		}
#endif

		if (fd != -1) {
#if defined(SO_NOSIGPIPE) && !defined(MSG_NOSIGNAL)
			// We do not want SIGPIPE if writing to socket.
			const int value = 1;
			setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(int));
#endif
			set_nonblocking(fd);
		}

		return fd;
	}

	int try_connect_host(addrinfo & addr, sockaddr_u const& bindAddr, scoped_lock & l)
	{
		if (socket_->evt_handler_) {
			socket_->evt_handler_->send_event<hostaddress_event>(socket_->ev_source_, socket::address_to_string(addr.ai_addr, addr.ai_addrlen));
		}

		socket_->fd_ = create_socket_fd(addr);
		if (socket_->fd_ == -1) {
			if (socket_->evt_handler_) {
				socket_->evt_handler_->send_event<socket_event>(socket_->ev_source_, addr.ai_next ? socket_event_flag::connection_next : socket_event_flag::connection, last_socket_error());
			}

			return 0;
		}

		if (bindAddr.sockaddr_.sa_family != AF_UNSPEC && bindAddr.sockaddr_.sa_family == addr.ai_family) {
			(void)::bind(socket_->fd_, &bindAddr.sockaddr_, sizeof(bindAddr));
		}

		auto* s = static_cast<socket*>(socket_);
		do_set_flags(socket_->fd_, s->flags_, s->flags_, s->keepalive_interval_);
		do_set_buffer_sizes(socket_->fd_, socket_->buffer_sizes_[0], socket_->buffer_sizes_[1]);

		int res = ::connect(socket_->fd_, addr.ai_addr, addr.ai_addrlen);
		if (res == -1) {
#ifdef FZ_WINDOWS
			// Map to POSIX error codes
			int error = WSAGetLastError();
			if (error == WSAEWOULDBLOCK) {
				res = EINPROGRESS;
			}
			else {
				res = last_socket_error();
			}
#else
			res = errno;
#endif
		}

		while (res == EINPROGRESS) {
			bool wait_successful;
			do {
				wait_successful = do_wait(WAIT_CONNECT, l);
				if ((triggered_ & WAIT_CONNECT)) {
					break;
				}
			} while (wait_successful);

			if (!wait_successful) {
				if (socket_) {
					close_socket_fd(socket_->fd_);
				}
				return -1;
			}
			triggered_ &= ~WAIT_CONNECT;

			res = triggered_errors_[0];
		}

		if (res) {
			if (socket_->evt_handler_) {
				socket_->evt_handler_->send_event<socket_event>(socket_->ev_source_, addr.ai_next ? socket_event_flag::connection_next : socket_event_flag::connection, res);
			}

			close_socket_fd(socket_->fd_);
		}
		else {
			static_cast<socket*>(socket_)->state_ = socket_state::connected;

#if HAVE_TCP_INFO
			if (socket_->buffer_sizes_[0] == -1 && !unmodified_rcv_wscale) {
				unmodified_rcv_wscale = get_rcv_wscale(socket_->fd_);
			}
			else if (socket_->buffer_sizes_[0] != -1 && !modified_rcv_wscale) {
				modified_rcv_wscale = get_rcv_wscale(socket_->fd_);
			}
#endif

			if (socket_->evt_handler_) {
				socket_->evt_handler_->send_event<socket_event>(socket_->ev_source_, socket_event_flag::connection, 0);
			}

			// We're now interested in all the other nice events
			waiting_ |= WAIT_READ | WAIT_WRITE;

			return 1;
		}

		return 0;
	}

	// Only call while locked
	bool do_connect(scoped_lock & l)
	{
		if (host_.empty() || port_.empty()) {
			static_cast<socket*>(socket_)->state_ = socket_state::failed;
			return false;
		}

		std::string host, port, bind;
		std::swap(host, host_);
		std::swap(port, port_);
		std::swap(bind, bind_);

		sockaddr_u bindAddr{};

		if (!bind.empty()) {
			// Convert bind address
			addrinfo bind_hints{};
			bind_hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;
			bind_hints.ai_socktype = SOCK_STREAM;
			addrinfo *bindAddressList{};
			int res = getaddrinfo(bind.empty() ? nullptr : bind.c_str(), "0", &bind_hints, &bindAddressList);
			if (!res && bindAddressList) {
				if (bindAddressList->ai_addr) {
					memcpy(&bindAddr.storage, bindAddressList->ai_addr, bindAddressList->ai_addrlen);
				}
				freeaddrinfo(bindAddressList);
			}
		}

		addrinfo hints{};
		hints.ai_family = socket_->family_;

		l.unlock();

		hints.ai_socktype = SOCK_STREAM;
#ifdef AI_IDN
		hints.ai_flags |= AI_IDN;
#endif

		addrinfo *addressList{};
		int res = getaddrinfo(host.c_str(), port.c_str(), &hints, &addressList);

		l.lock();

		if (should_quit()) {
			if (!res && addressList) {
				freeaddrinfo(addressList);
			}
			return false;
		}

		// If state isn't connecting, close() was called.
		// If host_ is set, close() was called and connect()
		// afterwards, state is back at connecting.
		// In either case, we need to abort this connection attempt.
		if (static_cast<socket*>(socket_)->state_ != socket_state::connecting || !host_.empty()) {
			if (!res && addressList) {
				freeaddrinfo(addressList);
			}
			return false;
		}

		if (res) {
#ifdef FZ_WINDOWS
			res = convert_msw_error_code(res);
#endif

			if (socket_->evt_handler_) {
				socket_->evt_handler_->send_event<socket_event>(socket_->ev_source_, socket_event_flag::connection, res);
			}
			static_cast<socket*>(socket_)->state_ = socket_state::failed;

			return false;
		}

		res = 0;
		for (addrinfo *addr = addressList; addr && !res; addr = addr->ai_next) {
			res = try_connect_host(*addr, bindAddr, l);
		}
		freeaddrinfo(addressList);
		if (res == 1) {
			return true;
		}

		if (socket_) {
			if (socket_->evt_handler_) {
				socket_->evt_handler_->send_event<socket_event>(socket_->ev_source_, socket_event_flag::connection, ECONNABORTED);
			}
			static_cast<socket*>(socket_)->state_ = socket_state::failed;
		}

		return false;
	}

	bool should_quit() const
	{
		return quit_ || !socket_;
	}

	// Call only while locked
	bool do_wait(int wait, scoped_lock & l)
	{
		waiting_ |= wait;

		for (;;) {
#ifdef FZ_WINDOWS
			int wait_events{};
			if (waiting_ & WAIT_CONNECT) {
				wait_events |= FD_CONNECT;
			}
			if (waiting_ & WAIT_READ) {
				wait_events |= FD_READ | FD_CLOSE;
			}
			if (waiting_ & WAIT_WRITE) {
				wait_events |= FD_WRITE;
			}
			if (waiting_ & WAIT_ACCEPT) {
				wait_events |= FD_ACCEPT;
			}
			WSAEventSelect(socket_->fd_, sync_event_, wait_events);
			l.unlock();
			WSAWaitForMultipleEvents(1, &sync_event_, false, WSA_INFINITE, false);

			l.lock();
			if (should_quit()) {
				return false;
			}

			WSANETWORKEVENTS events;
			int res = WSAEnumNetworkEvents(socket_->fd_, sync_event_, &events);
			if (res) {
				res = last_socket_error();
				return false;
			}

			if (waiting_ & WAIT_CONNECT) {
				if (events.lNetworkEvents & FD_CONNECT) {
					triggered_ |= WAIT_CONNECT;
					triggered_errors_[0] = convert_msw_error_code(events.iErrorCode[FD_CONNECT_BIT]);
					waiting_ &= ~WAIT_CONNECT;
				}
			}
			if (waiting_ & WAIT_READ) {
				if (events.lNetworkEvents & FD_READ) {
					triggered_ |= WAIT_READ;
					triggered_errors_[1] = convert_msw_error_code(events.iErrorCode[FD_READ_BIT]);
					waiting_ &= ~WAIT_READ;
				}
				if (events.lNetworkEvents & FD_CLOSE) {
					triggered_ |= WAIT_READ;
					int err = convert_msw_error_code(events.iErrorCode[FD_CLOSE_BIT]);
					if (err) {
						triggered_errors_[1] = err;
					}
					waiting_ &= ~WAIT_READ;
				}
			}
			if (waiting_ & WAIT_WRITE) {
				if (events.lNetworkEvents & FD_WRITE) {
					triggered_ |= WAIT_WRITE;
					triggered_errors_[2] = convert_msw_error_code(events.iErrorCode[FD_WRITE_BIT]);
					waiting_ &= ~WAIT_WRITE;
				}
			}
			if (waiting_ & WAIT_ACCEPT) {
				if (events.lNetworkEvents & FD_ACCEPT) {
					triggered_ |= WAIT_ACCEPT;
					triggered_errors_[3] = convert_msw_error_code(events.iErrorCode[FD_ACCEPT_BIT]);
					waiting_ &= ~WAIT_ACCEPT;
				}
			}

			if (triggered_ || !waiting_) {
				return true;
			}
#elif defined(HAVE_POLL)
			pollfd fds[2]{};
#ifdef HAVE_EVENTFD
			fds[0].fd = event_fd_;
#else
			fds[0].fd = pipe_[0];
#endif
			fds[0].events = POLLIN;
			fds[1].fd = socket_->fd_;
			fds[1].events = 0;

			if (waiting_ & (WAIT_READ|WAIT_ACCEPT)) {
				fds[1].events |= POLLIN;
			}
			if (waiting_ & (WAIT_WRITE | WAIT_CONNECT)) {
				fds[1].events |= POLLOUT;
			}

			l.unlock();

			int res = poll(fds, 2, -1);

			l.lock();

			if (res > 0 && fds[0].revents) {
				char buffer[100];
#ifdef HAVE_EVENTFD
				int damn_spurious_warning = read(event_fd_, buffer, 8);
#else
				int damn_spurious_warning = read(pipe_[0], buffer, 100);
#endif
				(void)damn_spurious_warning; // We do not care about return value and this is definitely correct!
			}

			if (quit_ || !socket_ || socket_->fd_ == -1) {
				return false;
			}

			if (!res) {
				continue;
			}
			if (res == -1) {
				res = errno;

				if (res == EINTR) {
					continue;
				}

				return false;
			}

			int const revents = fds[1].revents;
			if (waiting_ & WAIT_CONNECT) {
				if (revents & (POLLOUT|POLLERR|POLLHUP)) {
					int error;
					socklen_t len = sizeof(error);
					int getsockopt_res = getsockopt(socket_->fd_, SOL_SOCKET, SO_ERROR, &error, &len);
					if (getsockopt_res) {
						error = errno;
					}
					triggered_ |= WAIT_CONNECT;
					triggered_errors_[0] = error;
					waiting_ &= ~WAIT_CONNECT;
				}
			}
			else if (waiting_ & WAIT_ACCEPT) {
				if (revents & POLLIN) {
					triggered_ |= WAIT_ACCEPT;
					waiting_ &= ~WAIT_ACCEPT;
				}
			}
			else {
				if (waiting_ & WAIT_READ) {
					if (revents & (POLLIN|POLLHUP|POLLERR)) {
						triggered_ |= WAIT_READ;
						waiting_ &= ~WAIT_READ;
					}
				}
				if (waiting_ & WAIT_WRITE) {
					if (revents & (POLLOUT|POLLERR|POLLHUP)) {
						triggered_ |= WAIT_WRITE;
						waiting_ &= ~WAIT_WRITE;
					}
				}
			}

			if (triggered_ || !waiting_) {
				return true;
			}
#else
			fd_set readfds;
			fd_set writefds;
			FD_ZERO(&readfds);
			FD_ZERO(&writefds);

#ifdef HAVE_EVENTFD
			FD_SET(event_fd_, &readfds);
#else
			FD_SET(pipe_[0], &readfds);
#endif

			if (waiting_ & (WAIT_READ | WAIT_ACCEPT)) {
				FD_SET(socket_->fd_, &readfds);
			}
			if (waiting_ & (WAIT_WRITE | WAIT_CONNECT)) {
				FD_SET(socket_->fd_, &writefds);
			}

#ifdef HAVE_EVENTFD
			int maxfd = std::max(event_fd_, socket_->fd_) + 1;
#else
			int maxfd = std::max(pipe_[0], socket_->fd_) + 1;
#endif

			l.unlock();

			int res = select(maxfd, &readfds, &writefds, nullptr, nullptr);

			l.lock();

#ifdef HAVE_EVENTFD
			if (res > 0 && FD_ISSET(event_fd_, &readfds)) {
				char buffer[8];
				int damn_spurious_warning = read(event_fd_, buffer, 8);
#else
			if (res > 0 && FD_ISSET(pipe_[0], &readfds)) {
				char buffer[100];
				int damn_spurious_warning = read(pipe_[0], buffer, 100);
#endif
				(void)damn_spurious_warning; // We do not care about return value and this is definitely correct!
			}

			if (quit_ || !socket_ || socket_->fd_ == -1) {
				return false;
			}

			if (!res) {
				continue;
			}
			if (res == -1) {
				res = errno;

				if (res == EINTR) {
					continue;
				}

				return false;
			}

			if (waiting_ & WAIT_CONNECT) {
				if (FD_ISSET(socket_->fd_, &writefds)) {
					int error;
					socklen_t len = sizeof(error);
					int getsockopt_res = getsockopt(socket_->fd_, SOL_SOCKET, SO_ERROR, &error, &len);
					if (getsockopt_res) {
						error = errno;
					}
					triggered_ |= WAIT_CONNECT;
					triggered_errors_[0] = error;
					waiting_ &= ~WAIT_CONNECT;
				}
			}
			else if (waiting_ & WAIT_ACCEPT) {
				if (FD_ISSET(socket_->fd_, &readfds)) {
					triggered_ |= WAIT_ACCEPT;
					waiting_ &= ~WAIT_ACCEPT;
				}
			}
			else {
				if (waiting_ & WAIT_READ) {
					if (FD_ISSET(socket_->fd_, &readfds)) {
						triggered_ |= WAIT_READ;
						waiting_ &= ~WAIT_READ;
					}
				}
				if (waiting_ & WAIT_WRITE) {
					if (FD_ISSET(socket_->fd_, &writefds)) {
						triggered_ |= WAIT_WRITE;
						waiting_ &= ~WAIT_WRITE;
					}
				}
			}

			if (triggered_ || !waiting_) {
				return true;
			}
#endif
		}
	}

	void send_events()
	{
		if (!socket_ || !socket_->evt_handler_) {
			return;
		}
		if (triggered_ & WAIT_READ) {
			socket_->evt_handler_->send_event<socket_event>(socket_->ev_source_, socket_event_flag::read, triggered_errors_[1]);
			triggered_ &= ~WAIT_READ;
		}
		if (triggered_ & WAIT_WRITE) {
			socket_->evt_handler_->send_event<socket_event>(socket_->ev_source_, socket_event_flag::write, triggered_errors_[2]);
			triggered_ &= ~WAIT_WRITE;
		}
		if (triggered_ & WAIT_ACCEPT) {
			socket_->evt_handler_->send_event<socket_event>(socket_->ev_source_, socket_event_flag::connection, triggered_errors_[3]);
			triggered_ &= ~WAIT_ACCEPT;
		}
	}

	// Call only while locked
	bool idle_loop(scoped_lock & l)
	{
		if (quit_) {
			return false;
		}
		while (!socket_ || (!waiting_ && host_.empty())) {
			threadwait_ = true;
			condition_.wait(l);

			if (quit_) {
				return false;
			}
		}

		return true;
	}

	void entry()
	{
		scoped_lock l(mutex_);
		for (;;) {
			if (!idle_loop(l)) {
				break;
			}

			if (dynamic_cast<listen_socket*>(socket_)) {
				while (idle_loop(l)) {
					if (socket_->fd_ == -1) {
						waiting_ = 0;
						break;
					}
					if (!do_wait(0, l)) {
						break;
					}
					send_events();
				}
			}
			else {
				if (static_cast<socket*>(socket_)->state_ == socket_state::connecting) {
					if (!do_connect(l)) {
						continue;
					}
				}

				while (idle_loop(l)) {
					if (socket_->fd_ == -1) {
						waiting_ = 0;
						break;
					}
					bool res = do_wait(0, l);

					if (!res) {
						break;
					}

					send_events();
				}
			}
		}

		if (thread_) {
			quit_ = true;
		}
		else {
			l.unlock();
			delete this;
		}
		return;
	}

	socket_base* socket_{};

	std::string host_;
	std::string port_;
	std::string bind_;

	mutex mutex_;
	condition condition_;

	async_task thread_;

#ifdef FZ_WINDOWS
	// We wait on this using WSAWaitForMultipleEvents
	WSAEVENT sync_event_{WSA_INVALID_EVENT};
#elif defined(HAVE_EVENTFD)
	int event_fd_{-1};
#else
	// A pipe is used to unblock select
	int pipe_[2]{-1, -1};
#endif

	// The socket events we are waiting for
	int waiting_{};

	// The triggered socket events
	int triggered_{};
	int triggered_errors_[WAIT_EVENTCOUNT];

	bool quit_{};

	// Thread waits for instructions
	bool threadwait_{};
};

socket_base::socket_base(thread_pool& pool, event_handler* evt_handler, socket_event_source* ev_source)
	: thread_pool_(pool)
	, evt_handler_(evt_handler)
	, socket_thread_(new socket_thread(this))
	, ev_source_(ev_source)
{
	family_ = AF_UNSPEC;

	buffer_sizes_[0] = -1;
	buffer_sizes_[1] = -1;
}

void socket_base::detach_thread(scoped_lock & l)
{
	if (!socket_thread_) {
		return;
	}

	socket_thread_->set_socket(nullptr, l);
	if (socket_thread_->quit_) {
		socket_thread_->wakeup_thread(l);
		l.unlock();
		delete socket_thread_;
		socket_thread_ = nullptr;
	}
	else {
		if (!socket_thread_->thread_) {
			auto thread = socket_thread_;
			socket_thread_ = nullptr;
			l.unlock();
			delete thread;
		}
		else {
			socket_thread_->wakeup_thread(l);
			socket_thread_->thread_.detach();
			socket_thread_->quit_ = true;
			socket_thread_ = nullptr;
			l.unlock();
		}
	}
}

bool socket_base::do_set_event_handler(event_handler* pEvtHandler)
{
	if (!socket_thread_) {
		return false;
	}

	scoped_lock l(socket_thread_->mutex_);

	if (evt_handler_ == pEvtHandler) {
		return false;
	}

	change_socket_event_handler(evt_handler_, pEvtHandler, ev_source_);

	evt_handler_ = pEvtHandler;

	return true;
}

int socket_base::close()
{
	if (!socket_thread_) {
		close_socket_fd(fd_);
		return 0;
	}

	scoped_lock l(socket_thread_->mutex_);
	socket_t fd = fd_;
	fd_ = -1;

	socket_thread_->host_.clear();
	socket_thread_->port_.clear();

	socket_thread_->wakeup_thread(l);

	close_socket_fd(fd);
	if (dynamic_cast<socket*>(this)) {
		static_cast<socket*>(this)->state_ = socket_state::closed;
	}
	else {
		static_cast<listen_socket*>(this)->state_ = listen_socket_state::none;
	}

	socket_thread_->triggered_ = 0;
	for (int i = 0; i < WAIT_EVENTCOUNT; ++i) {
		socket_thread_->triggered_errors_[i] = 0;
	}

	if (evt_handler_) {
		remove_socket_events(evt_handler_, ev_source_);
		evt_handler_ = nullptr;
	}

	return 0;
}


std::string socket_base::address_to_string(sockaddr const* addr, int addr_len, bool with_port, bool strip_zone_index)
{
	char hostbuf[NI_MAXHOST];
	char portbuf[NI_MAXSERV];

	int res = getnameinfo(addr, addr_len, hostbuf, NI_MAXHOST, portbuf, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV);
	if (res) { // Should never fail
		return std::string();
	}

	std::string host = hostbuf;
	std::string port = portbuf;

	// IPv6 uses colons as separator, need to enclose address
	// to avoid ambiguity if also showing port
	if (addr->sa_family == AF_INET6) {
		if (strip_zone_index) {
			auto pos = host.find('%');
			if (pos != std::string::npos) {
				host = host.substr(0, pos);
			}
		}
		if (with_port) {
			host = "[" + host + "]";
		}
	}

	if (with_port) {
		return host + ":" + port;
	}
	else {
		return host;
	}
}

std::string socket_base::address_to_string(char const* buf, int buf_len)
{
	if (buf_len != 4 && buf_len != 16) {
		return std::string();
	}

	sockaddr_u addr;
	if (buf_len == 16) {
		memcpy(&addr.in6.sin6_addr, buf, buf_len);
		addr.in6.sin6_family = AF_INET6;
	}
	else {
		memcpy(&addr.in4.sin_addr, buf, buf_len);
		addr.in4.sin_family = AF_INET;
	}

	return address_to_string(&addr.sockaddr_, sizeof(addr), false, true);
}

std::string socket_base::local_ip(bool strip_zone_index) const
{
	sockaddr_storage addr;
	socklen_t addr_len = sizeof(addr);
	int res = getsockname(fd_, (sockaddr*)&addr, &addr_len);
	if (res) {
		return std::string();
	}

	return address_to_string((sockaddr *)&addr, addr_len, false, strip_zone_index);
}

address_type socket_base::address_family() const
{
	sockaddr_u addr;
	socklen_t addr_len = sizeof(addr);
	int res = getsockname(fd_, &addr.sockaddr_, &addr_len);
	if (res) {
		return address_type::unknown;
	}

	switch (addr.sockaddr_.sa_family)
	{
	case AF_INET:
		return address_type::ipv4;
	case AF_INET6:
		return address_type::ipv6;
	default:
		return address_type::unknown;
	}
}

int socket_base::local_port(int& error) const
{
	sockaddr_u addr;
	socklen_t addr_len = sizeof(addr);
	error = getsockname(fd_, &addr.sockaddr_, &addr_len);
	if (error) {
#ifdef FZ_WINDOWS
		error = convert_msw_error_code(error);
#endif
		return -1;
	}

	if (addr.storage.ss_family == AF_INET) {
		return ntohs(addr.in4.sin_port);
	}
	else if (addr.storage.ss_family == AF_INET6) {
		return ntohs(addr.in6.sin6_port);
	}

	error = EINVAL;
	return -1;
}

int socket_base::set_buffer_sizes(int size_receive, int size_send)
{
	if (!socket_thread_) {
		return ENOTCONN;
	}


	scoped_lock l(socket_thread_->mutex_);

#if HAVE_TCP_INFO
	// Explicitly ignore setting buffer size until after the unmodified window scale factor is known.
	if (unmodified_rcv_wscale)
#endif
	{
		if (size_receive < 0) {
			// Remember if we ever changd it
			buffer_sizes_[0] = (buffer_sizes_[0] == -1) ? -1 : -2;
		}
		else {
			buffer_sizes_[0] = size_receive;
		}
	}
	buffer_sizes_[1] = size_send;

	if (fd_ == -1) {
		return -1;
	}

	return do_set_buffer_sizes(fd_, size_receive, size_send);
}

bool socket_base::bind(std::string const& address)
{
	scoped_lock l(socket_thread_->mutex_);
	if (fd_ == -1) {
		socket_thread_->bind_ = address;
		return true;
	}

	return false;
}


socket_descriptor::~socket_descriptor()
{
	close_socket_fd(fd_);
}


listen_socket::listen_socket(thread_pool & pool, event_handler* evt_handler)
	: socket_base(pool, evt_handler, this)
	, socket_event_source(this)
{
}

listen_socket::~listen_socket()
{
	if (state_ != listen_socket_state::none) {
		close();
	}

	scoped_lock l(socket_thread_->mutex_);
	detach_thread(l);
}

int listen_socket::listen(address_type family, int port)
{
	if (state_ != listen_socket_state::none) {
		return EALREADY;
	}

	if (port < 0 || port > 65535) {
		return EINVAL;
	}

	switch (family)
	{
	case address_type::unknown:
		family_ = AF_UNSPEC;
		break;
	case address_type::ipv4:
		family_ = AF_INET;
		break;
	case address_type::ipv6:
		family_ = AF_INET6;
		break;
	default:
		return EINVAL;
	}

	{
		addrinfo hints = {};
		hints.ai_family = family_;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;

		std::string portstring = fz::to_string(port);

		addrinfo* addressList = nullptr;

		int res = getaddrinfo(socket_thread_->bind_.empty() ? nullptr : socket_thread_->bind_.c_str(), portstring.c_str(), &hints, &addressList);

		if (res) {
#ifdef FZ_WINDOWS
			return convert_msw_error_code(res);
#else
			return res;
#endif
		}

		for (addrinfo* addr = addressList; addr; addr = addr->ai_next) {
			fd_ = socket_thread::create_socket_fd(*addr);
			res = last_socket_error();

			if (fd_ == -1) {
				continue;
			}

			res = ::bind(fd_, addr->ai_addr, addr->ai_addrlen);
			if (!res) {
				break;
			}

			res = last_socket_error();
			close_socket_fd(fd_);
		}
		freeaddrinfo(addressList);
		if (fd_ == -1) {
			return res;
		}
	}

	int res = ::listen(fd_, 64);
	if (res) {
		res = last_socket_error();
		close_socket_fd(fd_);
		return res;
	}

	state_ = listen_socket_state::listening;

	socket_thread_->waiting_ = WAIT_ACCEPT;

	if (socket_thread_->start()) {
		state_ = listen_socket_state::none;
		close_socket_fd(fd_);
		return EMFILE;
	}

	return 0;
}

std::unique_ptr<socket> listen_socket::accept(int &error)
{
	socket_descriptor desc = fast_accept(error);
	if (!desc) {
		return std::unique_ptr<socket>();
	}

	auto ret = socket::from_descriptor(std::move(desc), thread_pool_, error);
	if (!ret) {
		error = ENOMEM;
	}
	return ret;
}

socket_descriptor listen_socket::fast_accept(int &error)
{
	if (!socket_thread_) {
		error = ENOTSOCK;
		return socket_descriptor();
	}

	socket_t fd;

	{
		scoped_lock l(socket_thread_->mutex_);
		socket_thread_->waiting_ |= WAIT_ACCEPT;
		socket_thread_->wakeup_thread(l);

#if HAVE_ACCEPT4
		fd = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
		if (fd == -1 && errno == ENOSYS)
#endif
		{
			fd = ::accept(fd_, nullptr, nullptr);
#if !defined(FZ_WINDOWS)
			set_cloexec(fd);
#endif
		}

#if !defined(FZ_WINDOWS) && !defined(HAVE_POLL)
		if (fd >= FD_SETSIZE) {
			::close(fd);
			error = EMFILE;
			return socket_descriptor();
		}
#endif
	}

	if (fd != -1) {
		do_set_buffer_sizes(fd, buffer_sizes_[0], buffer_sizes_[1]);
	}
	return socket_descriptor(fd);
}

listen_socket_state listen_socket::get_state() const
{
	if (!socket_thread_) {
		return listen_socket_state::none;
	}

	scoped_lock l(socket_thread_->mutex_);
	return state_;
}



socket::socket(thread_pool & pool, event_handler* evt_handler)
	: socket_base(pool, evt_handler, this)
	, socket_interface(this)
	, keepalive_interval_(duration::from_hours(2))
{
}

socket::~socket()
{
	close();

	scoped_lock l(socket_thread_->mutex_);
	detach_thread(l);
}

std::unique_ptr<socket> socket::from_descriptor(socket_descriptor && desc, thread_pool & pool, int & error)
{
	if (!desc) {
		error = ENOTSOCK;
		return nullptr;
	}

	socket_t fd = desc.detach();

#if defined(SO_NOSIGPIPE) && !defined(MSG_NOSIGNAL)
	// We do not want SIGPIPE if writing to socket.
	const int value = 1;
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(int));
#endif

	set_nonblocking(fd);

	auto pSocket = std::make_unique<socket>(pool, nullptr);
	if (!pSocket->socket_thread_) {
		error = ENOMEM;
		pSocket.reset();
	}
	else {
		pSocket->state_ = socket_state::connected;
		pSocket->fd_ = fd;
		pSocket->host_ = to_native(pSocket->peer_ip());
		pSocket->socket_thread_->waiting_ = WAIT_READ | WAIT_WRITE;
		if (pSocket->socket_thread_->start()) {
			error = ENOMEM;
			pSocket.reset();
		}
	}

	return pSocket;
}

int socket::connect(native_string const& host, unsigned int port, address_type family)
{
	if (state_ != socket_state::none) {
		return EISCONN;
	}

	if (port < 1 || port > 65535) {
		return EINVAL;
	}

	if (host.empty()) {
		return EINVAL;
	}

	int af{};

	switch (family)
	{
	case address_type::unknown:
		af = AF_UNSPEC;
		break;
	case address_type::ipv4:
		af = AF_INET;
		break;
	case address_type::ipv6:
		af = AF_INET6;
		break;
	default:
		return EINVAL;
	}

	family_ = af;
	state_ = socket_state::connecting;

	host_ = host;
	port_ = port;
	int res = socket_thread_->connect(to_utf8(host_), port_);
	if (res) {
		state_ = socket_state::failed;
		return res;
	}

	return 0;
}

socket_state socket::get_state() const
{
	if (!socket_thread_) {
		return socket_state::none;
	}

	scoped_lock l(socket_thread_->mutex_);
	return state_;
}

int socket::read(void* buffer, unsigned int size, int& error)
{
	if (!socket_thread_) {
		error = ENOTCONN;
		return -1;
	}

#if DEBUG_SOCKETEVENTS
	{
		scoped_lock l(socket_thread_->mutex_);
		assert(!(socket_thread_->waiting_ & WAIT_READ));
	}
#endif

	int res = recv(fd_, (char*)buffer, size, 0);

	if (res == -1) {
		error = last_socket_error();
		if (error == EAGAIN) {
			scoped_lock l(socket_thread_->mutex_);
			if (!(socket_thread_->waiting_ & WAIT_READ)) {
				socket_thread_->waiting_ |= WAIT_READ;
				socket_thread_->wakeup_thread(l);
			}
		}
	}
	else {
		error = 0;
	}

	return res;
}

int socket::write(void const* buffer, unsigned int size, int& error)
{
#ifdef MSG_NOSIGNAL
	const int flags = MSG_NOSIGNAL;
#else
	const int flags = 0;

#if !defined(SO_NOSIGPIPE) && !defined(FZ_WINDOWS)
	// Some systems have neither. Need to block signal
	struct sigaction old_action;
	struct sigaction action = {};
	action.sa_handler = SIG_IGN;
	int signal_set = sigaction(SIGPIPE, &action, &old_action);
#endif

#endif

#if DEBUG_SOCKETEVENTS
	if (socket_thread_) {
		scoped_lock l(socket_thread_->mutex_);
		assert(!(socket_thread_->waiting_ & WAIT_WRITE));
	}
#endif

	int res = send(fd_, (const char*)buffer, size, flags);

#if !defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE) && !defined(FZ_WINDOWS)
	// Restore previous signal handler
	if (!signal_set) {
		sigaction(SIGPIPE, &old_action, 0);
	}
#endif

	if (res == -1) {
		error = last_socket_error();
		if (error == EAGAIN) {
			scoped_lock l (socket_thread_->mutex_);
			if (!(socket_thread_->waiting_ & WAIT_WRITE)) {
				socket_thread_->waiting_ |= WAIT_WRITE;
				socket_thread_->wakeup_thread(l);
			}
		}
	}
	else {
		error = 0;
	}

	return res;
}

std::string socket::peer_ip(bool strip_zone_index) const
{
	sockaddr_storage addr;
	socklen_t addr_len = sizeof(addr);
	int res = getpeername(fd_, (sockaddr*)&addr, &addr_len);
	if (res) {
		return std::string();
	}

	return address_to_string((sockaddr *)&addr, addr_len, false, strip_zone_index);
}

int socket::peer_port(int& error) const
{
	sockaddr_u addr;
	socklen_t addr_len = sizeof(addr);
	error = getpeername(fd_, &addr.sockaddr_, &addr_len);
	if (error) {
#ifdef FZ_WINDOWS
		error = convert_msw_error_code(WSAGetLastError());
#else
		error = errno;
#endif
		return -1;
	}

	if (addr.storage.ss_family == AF_INET) {
		return ntohs(addr.in4.sin_port);
	}
	else if (addr.storage.ss_family == AF_INET6) {
		return ntohs(addr.in6.sin6_port);
	}

	error = EINVAL;
	return -1;
}

int socket::ideal_send_buffer_size()
{
	if (!socket_thread_) {
		return -1;
	}

	int size = -1;
#ifdef FZ_WINDOWS
	scoped_lock l(socket_thread_->mutex_);

	if (fd_ != -1) {
		// MSDN says this:
		// "Dynamic send buffering for TCP was added on Windows 7 and Windows
		// Server 2008 R2. By default, dynamic send buffering for TCP is
		// enabled unless an application sets the SO_SNDBUF socket option on
		// the stream socket"
		//
		// Guess what: It doesn't do it by itself. Programs need to
		// periodically and manually update SO_SNDBUF based on what
		// SIO_IDEAL_SEND_BACKLOG_QUERY returns.
#ifndef SIO_IDEAL_SEND_BACKLOG_QUERY
#define SIO_IDEAL_SEND_BACKLOG_QUERY 0x4004747b
#endif
		ULONG v{};
		DWORD outlen{};
		if (!WSAIoctl(fd_, SIO_IDEAL_SEND_BACKLOG_QUERY, nullptr, 0, &v, sizeof(v), &outlen, nullptr, nullptr)) {
			size = v;
		}
	}
#endif

	return size;
}

native_string socket::peer_host() const
{
	return host_;
}

void socket::retrigger(socket_event_flag event)
{
	if (!socket_thread_) {
		return;
	}

	if (event != socket_event_flag::read && event != socket_event_flag::write) {
		return;
	}

	scoped_lock l(socket_thread_->mutex_);

	auto s = state_;
	if (s != socket_state::connected && (s != socket_state::shut_down || event == socket_event_flag::write)) {
		return;
	}

	if (!evt_handler_ || has_pending_event(evt_handler_, this, event)) {
		return;
	}

	int const wait_flag = (event == socket_event_flag::read) ? WAIT_READ : WAIT_WRITE;
	if (!(socket_thread_->waiting_ & wait_flag)) {
		evt_handler_->send_event<socket_event>(this, event, 0);
	}
}

int socket::shutdown()
{
#ifdef FZ_WINDOWS
	int res = ::shutdown(fd_, SD_SEND);
#else
	int res = ::shutdown(fd_, SHUT_WR);
#endif
	if (res != 0) {
		return last_socket_error();
	}

	scoped_lock l(socket_thread_->mutex_);
	if (state_ == socket_state::connected) {
		state_ = socket_state::shut_down;
	}
	socket_thread_->waiting_ &= ~WAIT_WRITE;
	socket_thread_->triggered_ &= ~WAIT_WRITE;

	return 0;
}

void socket::set_event_handler(event_handler* pEvtHandler)
{
	bool changed = do_set_event_handler(pEvtHandler);

	if (changed && pEvtHandler) {
		scoped_lock l(socket_thread_->mutex_);
		if (state_ == socket_state::connected && !(socket_thread_->waiting_ & WAIT_WRITE) && !has_pending_event(evt_handler_, ev_source_, socket_event_flag::write)) {
			pEvtHandler->send_event<socket_event>(ev_source_, socket_event_flag::write, 0);
		}
		if ((state_ == socket_state::connected || state_ == socket_state::shut_down) && !(socket_thread_->waiting_ & WAIT_READ) && !has_pending_event(evt_handler_, ev_source_, socket_event_flag::read)) {
			pEvtHandler->send_event<socket_event>(ev_source_, socket_event_flag::read, 0);
		}
	}
}

void socket::set_keepalive_interval(duration const& d)
{
	if (!socket_thread_) {
		return;
	}

	if (d < duration::from_minutes(1)) {
		return;
	}

	scoped_lock l(socket_thread_->mutex_);

	keepalive_interval_ = d;
	if (fd_ != -1) {
		do_set_flags(fd_, flags_, flag_keepalive, keepalive_interval_);
	}
}

void socket::set_flags(int flags, bool enable)
{
	if (!socket_thread_) {
		return;
	}

	scoped_lock l(socket_thread_->mutex_);

	if (fd_ != -1) {
		do_set_flags(fd_, enable ? flags : 0, flags & (flags ^ flags_), keepalive_interval_);
	}
	if (enable) {
		flags_ |= flags;
	}
	else {
		flags_ &= ~flags;
	}
}

void socket::set_flags(int flags)
{
	if (!socket_thread_) {
		return;
	}

	scoped_lock l(socket_thread_->mutex_);

	if (fd_ != -1) {
		do_set_flags(fd_, flags, flags ^ flags_, keepalive_interval_);
	}
	flags_ = flags;
}

socket_layer::socket_layer(event_handler* handler, socket_interface& next_layer, bool event_passthrough)
	: socket_interface(next_layer.root())
	, event_handler_(handler)
	, next_layer_(next_layer)
	, event_passthrough_(event_passthrough)
{
	if (event_passthrough) {
		next_layer_.set_event_handler(handler);
	}
}

socket_layer::~socket_layer()
{
	next_layer_.set_event_handler(nullptr);
	remove_socket_events(event_handler_, this);
}

void socket_layer::set_event_handler(event_handler* handler)
{
	auto old = event_handler_;
	event_handler_ = handler;
	change_socket_event_handler(old, handler, this);

	if (event_passthrough_) {
		next_layer_.set_event_handler(handler);
	}
}

void socket_layer::forward_socket_event(socket_event_source* source, socket_event_flag t, int error)
{
	if (event_handler_) {
		(*event_handler_)(socket_event(source, t, error));
	}
}

void socket_layer::forward_hostaddress_event(socket_event_source* source, std::string const& address)
{
	if (event_handler_) {
		(*event_handler_)(hostaddress_event(source, address));
	}
}

void socket_layer::set_event_passthrough()
{
	event_passthrough_ = true;
	next_layer_.set_event_handler(event_handler_);
}

int socket_layer::shutdown_read()
{
	return next_layer_.shutdown_read();
}
}
