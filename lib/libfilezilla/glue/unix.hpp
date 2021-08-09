#ifndef LIBFILEZILLA_GLUE_UNIX_HEADER
#define LIBFILEZILLA_GLUE_UNIX_HEADER

/** \file
 * \brief Platform specific glue for Unix(-like) platforms, including macOS.
 */

#include "../libfilezilla.hpp"

#ifndef FZ_WINDOWS

namespace fz {

/// Sets FD_CLOEXEC on file descriptor
bool FZ_PUBLIC_SYMBOL set_cloexec(int fd);

/**
 * \brief Creates a pipe with FD_CLOEXEC set
 *
 * If present uses pipe2 so it's set atomically, falls
 * back to fcntl.
 *
 * On failure sets fds to -1.
 */
bool FZ_PUBLIC_SYMBOL create_pipe(int fds[2], bool require_atomic_creation = false);

/** \brief Disables SIGPIPE
 *
 * \note This is implicitly called by create_pipe and when a socket is created.
 */
void FZ_PUBLIC_SYMBOL disable_sigpipe();

}

#else
#error This file is not for Windows
#endif

#endif
