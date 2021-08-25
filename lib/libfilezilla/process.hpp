#ifndef LIBFILEZILLA_PROCESS_HEADER
#define LIBFILEZILLA_PROCESS_HEADER

#include "libfilezilla.hpp"

/** \file
 * \brief Header for the \ref fz::process "process" class
 */

#include <vector>

#ifdef FZ_WINDOWS
#include "glue/windows.hpp"
#endif

namespace fz {
class impersonation_token;

/** \brief The process class manages an asynchronous process with redirected IO.
 *
 * No console window is being created.

 * To use, spawn the process and, since it's blocking, call read from a different thread.
 *
 */
class FZ_PUBLIC_SYMBOL process final
{
public:
	process();
	~process();

	process(process const&) = delete;
	process& operator=(process const&) = delete;

	/** \brief Start the process
	 *
	 * This function takes care of properly quoting and escaping the the program's path and its arguments.
	 * Fails if process has already been spawned.
	 *
	 * \param cmd The path of the program to execute
	 * \param args The command-line arguments for the process.
	 *
	 * \note May return \c true even if the process cannot be started. In that case, trying to read from the process
	 * will fail with an error or EOF.
	 */
	bool spawn(native_string const& cmd, std::vector<native_string> const& args = std::vector<native_string>(), bool redirect_io = true);

	bool spawn(std::vector<native_string> const& command_with_args, bool redirect_io = true);

#ifndef FZ_WINDOWS
	/**
	 * \brief Allows passing additional file descriptors to the process
	 *
	 * This function only exists on *nix, it is not needed on Windows where
	 * DuplicateHandle() can be used instead with the target process as argument.
	 */
	bool spawn(native_string const& cmd, std::vector<native_string> const& args, std::vector<int> const& extra_fds, bool redirect_io = true);

	bool spawn(impersonation_token const& it, native_string const& cmd, std::vector<native_string> const& args, std::vector<int> const& extra_fds, bool redirect_io = true);
#endif

	/** \brief Stops the spawned process
	 *
	 * This function doesn't actually kill the process, it merely closes the pipes.
	 *
	 * Blocks until the process has quit.
	 */
	void kill();

	/** \brief Read data from process
	 *
	 * This function blocks
	 *
	 * \return >0 Number of octets read, can be less than requested
	 * \return 0 on EOF
	 * \return -1 on error.
	 */
	int read(char* buffer, unsigned int len);

	/** \brief Write data data process
	 *
	 * This function blocks
	 *
	 * \return true if all octets have been written.
	 * \return false on error.
	 */
	bool write(char const* buffer, unsigned int len);

	inline bool write(std::string_view const& s) {
		return write(s.data(), static_cast<unsigned int>(s.size()));
	}

#if FZ_WINDOWS
	/** \brief
	 * Returns the HANDLE of the process.
	 */
	HANDLE handle() const;
#endif

private:
	class impl;
	impl* impl_;
};


/** \brief Starts a detached process
 *
 * This function takes care of properly quoting and escaping the the program's path and its arguments.
 *
 * \param cmd_with_args The full path of the program to execute and any additional arguments
 *
 * \note May return \c true even if the process cannot be started.
 */
bool FZ_PUBLIC_SYMBOL spawn_detached_process(std::vector<native_string> const& cmd_with_args);

}

#endif
