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

# Ask pkg-config first. It knows where libgit2 is and, for a static one, what
# else has to go on the link line. It is only a source of hints here: the
# explicit searches below still win, so a libgit2 built by scripts/build.sh into
# ../install-root is still preferred over a system one.
find_package( PkgConfig QUIET )
if ( PKG_CONFIG_FOUND )
	pkg_check_modules( PC_LIBGIT2 QUIET libgit2 )
endif ()

FIND_PATH( LIBGIT2_INCLUDE_DIR
NAMES git2.h
HINTS
    ${CMAKE_CURRENT_SOURCE_DIR}/../install-root/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libgit2/include
    ${PC_LIBGIT2_INCLUDE_DIRS}
)

IF ( LIBGIT2_DYNAMIC )
    SET( LIBGIT2_SO libgit2.so )
ENDIF()

# "git2" resolves to the shared library where there is one, so it goes ahead of
# the explicit archive name. The other order picked libgit2.a on any distro that
# ships both, and a static libgit2 pulls in krb5, pcre and friends that nothing
# put on the link line - so a plain "cmake .." failed to link with an
# undefined reference to gss_indicate_mechs. Only scripts/build.sh passed
# -DLIBGIT2_DYNAMIC=ON, so only scripts/build.sh worked.
FIND_LIBRARY( LIBGIT2_LIBRARIES
NAMES
    ${LIBGIT2_SO}
    git2
    libgit2.a
HINTS
    ${CMAKE_CURRENT_SOURCE_DIR}/../install-root/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../libgit2/build
    ${PC_LIBGIT2_LIBRARY_DIRS}
)

# If we did end up with a static archive - because that is all that is
# installed - it carries no record of its own dependencies, so take them from
# pkg-config rather than leaving the link to fail.
IF ( LIBGIT2_LIBRARIES MATCHES "\\.a$" AND PC_LIBGIT2_FOUND )
	SET( LIBGIT2_LIBRARIES ${LIBGIT2_LIBRARIES} ${PC_LIBGIT2_STATIC_LDFLAGS_OTHER} ${PC_LIBGIT2_STATIC_LIBRARIES} )
ENDIF ()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	SET(LIBGIT2_LIBRARIES ${LIBGIT2_LIBRARIES})
else()
	SET(LIBGIT2_LIBRARIES ${LIBGIT2_LIBRARIES} -lssl -lcrypto)
endif()

INCLUDE( FindPackageHandleStandardArgs )
FIND_PACKAGE_HANDLE_STANDARD_ARGS( git2 DEFAULT_MSG LIBGIT2_INCLUDE_DIR LIBGIT2_LIBRARIES )
include_directories(${LIBGIT2_INCLUDE_DIR})
