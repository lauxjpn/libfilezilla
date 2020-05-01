#ifndef LIBFILEZILLA_LOCAL_FILESYS_HEADER
#define LIBFILEZILLA_LOCAL_FILESYS_HEADER

#include "libfilezilla.hpp"
#include "time.hpp"

#ifdef FZ_WINDOWS
#include "private/windows.hpp"
#else
#include <dirent.h>
#endif

/** \file
 * \brief Declares local_filesys class to enumerate local files and query their metadata such as type, size and modification time.
 */
namespace fz {

/**
 * \brief Small class to return filesystem errors
 */
class FZ_PUBLIC_SYMBOL result final
{
public:
	enum error {
		ok,

		/// Permission denied
		noperm,

		/// Requested file does not exist or is not a file
		nofile,

		/// Requested dir does not exist or is not a dir
		nodir,

		/// Some other error
		other
	};

	explicit operator bool() const { return error_ == 0; }

	error error_{};
};

/**
 * \brief This class can be used to enumerate the contents of local directories and to query
 * the metadata of files.
 *
 * This class is aware of symbolic links. Under Windows it can handle reparse points as well.
 */
class FZ_PUBLIC_SYMBOL local_filesys final
{
public:
	local_filesys() = default;
	~local_filesys();

	local_filesys(local_filesys const&) = delete;
	local_filesys& operator=(local_filesys const&) = delete;

	/// Types of files. While 'everything is a file', a filename can refer to a file proper, a directory or a symbolic link.
	enum type {
		unknown = -1,
		file,
		dir,
		link
	};

	/// The system's preferred path separator
	static char const path_separator;

	/// \brief Checks whether given character is a path separator.
	///
	/// On most systems, the forward slash is the only separator. The exception is Windows where both forward and backward slashes are separators, with the latter being preferred.
	static inline bool is_separator(wchar_t c) {
#ifdef FZ_WINDOWS
		return c == '/' || c == '\\';
#else
		return c == '/';
#endif
	}

	/// \brief get_file_type return the type of the passed path.
	///
	/// Can optionally follow symbolic links.
	static type get_file_type(native_string const& path, bool follow_links = false);

	/**
	 * \brief Gets the info for the passed arguments.
	 *
	 * Follows symbolic links and stats the target by default, sets is_link to true if path was
	 * a link.
	 *
	 * The returned type can only be \c type::link if \c follow_links is \c false.
	 */
	static type get_file_info(native_string const& path, bool &is_link, int64_t* size, datetime* modification_time, int* mode, bool follow_links = true);

	/// Gets size of file, returns -1 on error.
	static int64_t get_size(native_string const& path, bool *is_link = nullptr);

	/// \brief Begins enumerating a directory.
	///
	/// \param dirs_only If true, only directories are enumerated.
	result begin_find_files(native_string path, bool dirs_only = false);

	/// Gets the next file in the directory. Call until it returns false.
	bool get_next_file(native_string& name);

	/**
	 * \brief Gets the next file in the directory. Call until it returns false.
	 *
	 * Stores the metadata in any non-null arguments. Follows symbolic links.
	 *
	 * \param is_link will be set to true iff file is a symbolic link.
	 * \param t will receive the type of file, after following any symbolic links. Cannot return \c type::link.
	 *
	 */
	bool get_next_file(native_string& name, bool &is_link, type & t, int64_t* size, datetime* modification_time, int* mode);

	/// Ends enumerating files. Automatically called in the destructor.
	void end_find_files();

	static datetime get_modification_time(native_string const& path);
	static bool set_modification_time(native_string const& path, const datetime& t);

	/// Get the target path of a symbolic link
	static native_string get_link_target(native_string const& path);

private:
	// State for directory enumeration
	bool dirs_only_{};

#ifdef FZ_WINDOWS
	WIN32_FIND_DATA m_find_data{};
	HANDLE m_hFind{INVALID_HANDLE_VALUE};
	bool has_next_{};
	native_string m_find_path;
#else
	DIR* dir_{};
#endif
};

}

#endif
