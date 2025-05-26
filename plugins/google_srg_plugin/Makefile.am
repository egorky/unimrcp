# Required for plugins, provides UniMRCP build system variables like $(UNIMRCP_PLUGIN_INCLUDES)
# and rules for building plugins.
include $(top_srcdir)/build/rules/uniplugin.am

# AM_CPPFLAGS: C/C++ Preprocessor Flags
# Add include paths for Google Cloud, gRPC, Protobuf, and the plugin's own src directory (for .hpp file)
# These pkg-config variables (e.g., GOOGLE_CLOUD_CPP_SPEECH_CFLAGS) are expected to be set by configure.ac
AM_CPPFLAGS += \
    -I$(srcdir)/src \
    $(GOOGLE_CLOUD_CPP_SPEECH_CFLAGS) \
    $(GRPC_CFLAGS) \
    $(PROTOBUF_CFLAGS)

# AM_CXXFLAGS: C++ Compiler Flags
# THREAD_CFLAGS usually comes from UniMRCP's configure (e.g., from AX_PTHREAD)
# Add other C++ specific flags if needed. For C++17, this might be set in configure.ac
# via AX_CXX_COMPILE_STDCXX(17, noext, mandatory) or similar.
AM_CXXFLAGS = $(THREAD_CFLAGS)

# Define the plugin library to be built
# The name "googlesrg" should match the so-name used in unimrcpserver.xml.
# .la is for libtool archive.
plugin_LTLIBRARIES = googlesrg.la

# Source files for the plugin library
# List both C and C++ source files. Automake will use the CXXLD linker.
googlesrg_la_SOURCES = \
    src/google_srg_engine.c \
    src/google_stt_streamer.cpp

# Linker flags for the plugin library
# $(UNIMRCP_PLUGIN_OPTS) typically includes -module -avoid-version and possibly -no-undefined
# Add any other specific linker flags if necessary.
googlesrg_la_LDFLAGS = $(UNIMRCP_PLUGIN_OPTS)

# Libraries to link against
# These pkg-config variables (e.g., GOOGLE_CLOUD_CPP_SPEECH_LIBS) are expected to be set by configure.ac.
# $(UNIMRCP_PLUGIN_LIBS) should provide linkage for UniMRCP framework libraries.
# Ensure that these _LIBS variables include all necessary transitive dependencies
# (e.g., -lssl, -lcrypto, -lz for gRPC if not handled by its .pc file).
# pthread linkage is often handled by $(THREAD_LIBS) or within GRPC_LIBS.
googlesrg_la_LIBADD = \
    $(GOOGLE_CLOUD_CPP_SPEECH_LIBS) \
    $(GRPC_LIBS) \
    $(PROTOBUF_LIBS) \
    $(UNIMRCP_PLUGIN_LIBS)

# Explicitly link against libstdc++ because we have C++ code.
# This is often necessary if not automatically handled by the CXXLD linker or if
# the _LIBS variables from pkg-config do not implicitly link it.
# $(CXXLIBS) is an Autoconf variable that might contain this.
# If $(GRPC_LIBS) or $(GOOGLE_CLOUD_CPP_SPEECH_LIBS) already ensure C++ runtime linkage,
# this might be redundant but is generally safe.
googlesrg_la_LIBADD += $(CXXLIBS) # $(CXXLIBS) should be populated by AC_PROG_CXX

# Note for configure.ac:
# The main configure.ac script for UniMRCP must be updated to:
# 1. Call AC_PROG_CXX to enable C++ compilation and populate CXX, CXXFLAGS, CXXLIBS.
# 2. Use PKG_CHECK_MODULES for:
#    - google-cloud-cpp-speech (e.g., PKG_CHECK_MODULES([GOOGLE_CLOUD_CPP_SPEECH], [google-cloud-cpp-speech >= YOUR_MIN_VERSION]))
#    - grpc++ (e.g., PKG_CHECK_MODULES([GRPC], [grpc++ >= YOUR_MIN_VERSION]))
#    - protobuf (e.g., PKG_CHECK_MODULES([PROTOBUF], [protobuf >= YOUR_MIN_VERSION]))
# 3. Substitute the _CFLAGS and _LIBS variables (e.g., AC_SUBST(GOOGLE_CLOUD_CPP_SPEECH_CFLAGS)).
# 4. Ensure AX_PTHREAD (or similar) is used to set THREAD_CFLAGS and THREAD_LIBS if gRPC's .pc doesn't handle it.
# 5. Optionally, check for C++17 support if required by google-cloud-cpp, using AX_CXX_COMPILE_STDCXX.
#    Example: AX_CXX_COMPILE_STDCXX(17, [noext], [mandatory])
#    If C++17 is enforced, AM_CXXFLAGS might need adjustment based on configure.ac output.
#    If not using AX_CXX_COMPILE_STDCXX, AM_CXXFLAGS might need -std=c++17 directly.

# Standard clean rules are typically handled by uniplugin.am and Automake defaults.
# No need for custom clean-local unless there are specific generated files not covered.
