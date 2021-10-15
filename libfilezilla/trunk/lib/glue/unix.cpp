#include "../libfilezilla/buffer.hpp"
#include "../libfilezilla/glue/unix.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <mutex>

namespace fz {

bool set_cloexec(int fd)
{
#ifdef FD_CLOEXEC
	if (fd != -1) {
		int flags = fcntl(fd, F_GETFD);
		if (flags >= 0) {
			return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) >= 0;
		}
	}
#else
	errno = ENOSYS;
#endif
	return false;
}

bool create_pipe(int fds[2], bool require_atomic_creation)
{
	disable_sigpipe();

	fds[0] = -1;
	fds[1] = -1;

#if HAVE_PIPE2
	int res = pipe2(fds, O_CLOEXEC);
	if (!res) {
		return true;
	}
	else if (errno != ENOSYS) {
		return false;
	}
	else
#endif
	{
		if (require_atomic_creation) {
			return false;
		}

		if (pipe(fds) != 0) {
			return false;
		}

		set_cloexec(fds[0]);
		set_cloexec(fds[1]);
	}

	return true;
}

void disable_sigpipe()
{
	std::once_flag flag;
	std::call_once(flag, [](){ signal(SIGPIPE, SIG_IGN); });
}

#if FZ_UNIX
bool create_socketpair(int fds[2])
{
	disable_sigpipe();

	bool ret = socketpair(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0, fds) == 0;
	if (!ret) {
		fds[0] = -1;
		fds[1] = -1;
	}
	return ret;
}

int send_fd(int socket, fz::buffer & buf, int fd, int & error)
{
	if (buf.empty()) {
		error = EINVAL;
		return -1;
	}
	if (socket < 0) {
		error = EBADFD;
		return -1;
	}

#ifdef MSG_NOSIGNAL
	const int flags = MSG_NOSIGNAL;
#else
	const int flags = 0;
#endif

	struct msghdr msg{};

	struct iovec iov{};
	iov.iov_base = buf.get();
	iov.iov_len = buf.size();
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	union {
		char buf[CMSG_SPACE(sizeof(int))];
		struct cmsghdr header;
	} cmsg_u; // For alignment reasons

	if (fd != -1) {
		msg.msg_control = cmsg_u.buf;
		msg.msg_controllen = sizeof(cmsg_u.buf);
		memset(msg.msg_control, 0, msg.msg_controllen);

		struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
	}


	int res{};
	do {
		res = sendmsg(socket, &msg, flags);
	}
	while (res == -1 && errno == EINTR);

	if (res > 0) {
		buf.consume(res);
		error = 0;
	}
	else {
		error = errno;
	}

	return res;
}

int read_fd(int socket, fz::buffer & buf, int & fd, int & error)
{
	fd = -1;
	if (socket < 0) {
		error = EBADFD;
		return -1;
	}

#ifdef MSG_NOSIGNAL
	const int flags = MSG_NOSIGNAL|MSG_CMSG_CLOEXEC;
#else
	const int flags = MSG_CMSG_CLOEXEC;
#endif

	struct msghdr msg{};

	struct iovec iov{};
	iov.iov_base = buf.get(16*1024);
	iov.iov_len = 16*1024;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	union {
		char buf[CMSG_SPACE(sizeof(int))];
		struct cmsghdr header;
	} cmsg_u; // For alignment reasons

	msg.msg_control = cmsg_u.buf;
	msg.msg_controllen = sizeof(cmsg_u.buf);

	int res{};
	do {
		res = recvmsg(socket, &msg, flags);
	}
	while (res == -1 && errno == EINTR);
	error = errno;

	if (res >= 0) {
		buf.add(res);
		error = 0;

		struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msg);
		if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS && cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
			memcpy(&fd, CMSG_DATA(cmsg), sizeof(int));
		};
	}
	else {
		error = errno;
	}

	return res;
}
#endif

}
