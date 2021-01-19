#ifndef LIBFILEZILLA_VISIBILITY_HELPER_HEADER
#define LIBFILEZILLA_VISIBILITY_HELPER_HEADER

#include "private/defs.hpp"

// Symbol visibility. There are two main cases: Building a library and using it.
// For building, symbols need to be marked as export, for using it they
// need to be imported.

// Two cases when building: Windows, other platform
#ifdef FZ_WINDOWS

  // Under Windows we can either use Visual Studio or a proper compiler
  #ifdef _MSC_VER
    #ifdef DLL_EXPORT
      #define FZ_EXPORT_PUBLIC __declspec(dllexport)
    #else
      #define FZ_EXPORT_PUBLIC
    #endif
    #define FZ_EXPORT_PRIVATE
  #else
    #ifdef DLL_EXPORT
      #define FZ_EXPORT_PUBLIC __declspec(dllexport)
      #define FZ_EXPORT_PRIVATE
    #else
      #define FZ_EXPORT_PUBLIC __attribute__((visibility("default")))
      #define FZ_EXPORT_PRIVATE __attribute__((visibility("hidden")))
    #endif
  #endif

#else

  #define FZ_EXPORT_PUBLIC __attribute__((visibility("default")))
  #define FZ_EXPORT_PRIVATE __attribute__((visibility("hidden")))

#endif


// Under MSW it makes a difference whether we use a static library or a DLL
#if defined(FZ_WINDOWS)
  #define FZ_IMPORT_SHARED __declspec(dllimport)
#else
  #define FZ_IMPORT_SHARED
#endif
#define FZ_IMPORT_STATIC

#endif
