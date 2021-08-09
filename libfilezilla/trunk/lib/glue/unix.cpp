#include "../libfilezilla/glue/unix.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

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

void FZ_PUBLIC_SYMBOL disable_sigpipe()
{
	std::once_flag flag;
	std::call_once(flag, [](){ signal(SIGPIPE, SIG_IGN); });
}
}
