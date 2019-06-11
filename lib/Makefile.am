lib_LTLIBRARIES = libfilezilla.la

libfilezilla_la_SOURCES = \
	buffer.cpp \
	encode.cpp \
	encryption.cpp \
	event.cpp \
	event_handler.cpp \
	event_loop.cpp \
	file.cpp \
	hash.cpp \
	iputils.cpp \
	local_filesys.cpp \
	mutex.cpp \
	process.cpp \
	recursive_remove.cpp \
	signature.cpp \
	socket.cpp \
	socket_errors.cpp \
	string.cpp \
	thread.cpp \
	thread_pool.cpp \
	tls_info.cpp \
	tls_layer.cpp \
	tls_layer_impl.cpp \
	tls_system_trust_store.cpp \
	time.cpp \
	translate.cpp \
	uri.cpp \
	util.cpp \
	version.cpp

nobase_include_HEADERS = \
	libfilezilla/apply.hpp \
	libfilezilla/buffer.hpp \
	libfilezilla/encode.hpp \
	libfilezilla/encryption.hpp \
	libfilezilla/event.hpp \
	libfilezilla/event_handler.hpp \
	libfilezilla/event_loop.hpp \
	libfilezilla/file.hpp \
	libfilezilla/format.hpp \
	libfilezilla/hash.hpp \
	libfilezilla/iputils.hpp \
	libfilezilla/libfilezilla.hpp \
	libfilezilla/local_filesys.hpp \
	libfilezilla/logger.hpp \
	libfilezilla/mutex.hpp \
	libfilezilla/optional.hpp \
	libfilezilla/process.hpp \
	libfilezilla/recursive_remove.hpp \
	libfilezilla/shared.hpp \
	libfilezilla/signature.hpp \
	libfilezilla/socket.hpp \
	libfilezilla/string.hpp \
	libfilezilla/thread.hpp \
	libfilezilla/thread_pool.hpp \
	libfilezilla/time.hpp \
	libfilezilla/tls_info.hpp \
	libfilezilla/tls_layer.hpp \
	libfilezilla/tls_system_trust_store.hpp \
	libfilezilla/translate.hpp \
	libfilezilla/uri.hpp \
	libfilezilla/util.hpp \
	libfilezilla/version.hpp \
	libfilezilla/private/defs.hpp \
	libfilezilla/private/visibility.hpp \
	libfilezilla/private/windows.hpp \
	libfilezilla/glue/wx.hpp

libfilezilla_la_LDFLAGS = $(AM_LDFLAGS)
libfilezilla_la_LIBADD = $(libdeps)

if FZ_WINDOWS

windows/libfilezilla_rc.o: $(srcdir)/windows/libfilezilla.rc $(srcdir)/windows/manifest.xml windows/version.rc
	@if [ -f $@~ ]; then rm $@~; fi
	@WINDRES@ --include-dir=windows --include-dir=$(srcdir)/windows $(WINDRESFLAGS) $(srcdir)/windows/libfilezilla.rc -D GNU_WINDRES -D HAS_VERSIONRC $@~ || rm $@~
	@if [ -f $@ ]; then rm $@; fi
	mv $@~ $@
EXTRA_libfilezilla_la_DEPENDENCIES = windows/libfilezilla_rc.o
libfilezilla_la_LDFLAGS += -Wl,windows/libfilezilla_rc.o

else


libfilezilla_la_SOURCES += \
	glue/unix.cpp

nobase_include_HEADERS += \
	libfilezilla/glue/unix.hpp


endif

dist_noinst_HEADERS = \
	tls_layer_impl.hpp \
	tls_system_trust_store_impl.hpp

libfilezilla_la_CPPFLAGS = $(AM_CPPFLAGS)
libfilezilla_la_CPPFLAGS += -DBUILDING_LIBFILEZILLA
libfilezilla_la_CPPFLAGS += $(NETTLE_CFLAGS) $(GNUTLS_CFLAGS)

# Needed for version.hpp in out-of-tree builds
libfilezilla_la_CPPFLAGS += -I$(srcdir)/libfilezilla

libfilezilla_la_CXXFLAGS = $(AM_CXXFLAGS)
libfilezilla_la_CXXFLAGS += -fvisibility=hidden

libfilezilla_la_LDFLAGS += -no-undefined

libfilezilla_la_LIBADD += $(NETTLE_LIBS) $(HOGWEED_LIBS) $(GNUTLS_LIBS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libfilezilla.pc

DISTCLEANFILES = libfilezilla.pc windows/version.rc
CLEANFILES = windows/libfilezilla_rc.o

dist_noinst_DATA = libfilezilla.vcxproj
