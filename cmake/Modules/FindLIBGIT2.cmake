# - Try to find the LibGit2 Library
# Once done this will define
#
#  LIBGIT2_FOUND - system has LibGit2
#  LIBGIT2_INCLUDE_DIR - the LibGit2 include directory
#  LIBGIT2_LIBRARIES
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#

# in cache already
IF ( LIBGIT2_INCLUDE_DIR AND LIBGIT2_LIBRARIES )
   SET( LIBGIT2_FIND_QUIETLY TRUE )
ENDIF ()

FIND_PATH( LIBGIT2_INCLUDE_DIR
NAMES git2.h
HINTS
    ${CMAKE_CURRENT_SOURCE_DIR}/../install-root/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libgit2/include
)

IF ( LIBGIT2_DYNAMIC )
    SET( LIBGIT2_SO libgit2.so )
ENDIF()

# "git2" resolves through CMAKE_FIND_LIBRARY_SUFFIXES and so prefers the shared
# library, which is what distributions ship. It has to come before the explicit
# libgit2.a: listing the static archive first meant we picked it even on systems
# that had a perfectly good shared libgit2 installed, and then failed to link
# because a static libgit2 also needs all of its own dependencies named.
FIND_LIBRARY( LIBGIT2_LIBRARIES
NAMES
    ${LIBGIT2_SO}
    git2
    libgit2.a
HINTS
    ${CMAKE_CURRENT_SOURCE_DIR}/../install-root/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../libgit2/build
)

if(LIBGIT2_LIBRARIES MATCHES "\\.a$")
	# Static libgit2: ask pkg-config what it actually needs. Distribution builds
	# pull in things like libssh2, libpcre2, libhttp_parser and GSSAPI, none of
	# which we can sensibly guess.
	find_package(PkgConfig QUIET)
	if(PKG_CONFIG_FOUND)
		pkg_check_modules(_LIBGIT2_PC QUIET libgit2)
	endif()
	if(_LIBGIT2_PC_STATIC_LDFLAGS)
		SET(LIBGIT2_LIBRARIES ${LIBGIT2_LIBRARIES} ${_LIBGIT2_PC_STATIC_LDFLAGS})
	elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
		SET(LIBGIT2_LIBRARIES ${LIBGIT2_LIBRARIES} -lssl -lcrypto)
	endif()
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	SET(LIBGIT2_LIBRARIES ${LIBGIT2_LIBRARIES} -lssl -lcrypto)
endif()

INCLUDE( FindPackageHandleStandardArgs )
FIND_PACKAGE_HANDLE_STANDARD_ARGS( git2 DEFAULT_MSG LIBGIT2_INCLUDE_DIR LIBGIT2_LIBRARIES )
include_directories(${LIBGIT2_INCLUDE_DIR})
