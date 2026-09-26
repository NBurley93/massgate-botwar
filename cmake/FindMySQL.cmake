# FindMySQL
# ---------
#
# Find MySQL library
#
# Imported Targets
# ^^^^^^^^^^^^^^^^
#
# This module defines the following :prop_tgt:`IMPORTED` targets:
#
# ``MySQL::MySQL``
#   The MySQL imported library.
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module will set the following variables in your project:
#
# ``MYSQL_FOUND``
#   System has the MySQL library.
# ``MYSQL_INCLUDE_DIR``
#   The MySQL include directory.
# ``MYSQL_LIBRARY``
#   The MySQL library.
# ``MYSQL_LIBRARIES.``
#   MySQL libraries to be linked.
# ``MYSQL_VERSION``
#   The MySQL version.

#
# MariaDB Connector/C is also accepted (see scripts/bootstrap-deps.ps1).
# Set MYSQL_ROOT to the install prefix of either client library.

find_path(MYSQL_INCLUDE_DIR
	NAMES
		mysql.h
	HINTS
		"${MYSQL_ROOT}"
		"$ENV{MYSQL_HOME}"
	PATHS
		"$ENV{SYSTEMDRIVE}/MySQL/*"
		"$ENV{PROGRAMFILES} (x86)/MySQL/*"
		"$ENV{PROGRAMFILES}/MySQL/*"
	PATH_SUFFIXES
		include
		include/mariadb
)

if(DEFINED MYSQL_LIBRARIES AND NOT DEFINED MYSQL_LIBRARY)
	set(MYSQL_LIBRARY ${MYSQL_LIBRARIES})
else()
	find_library(MYSQL_LIBRARY
		NAMES
			libmysql
			libmariadb
		HINTS
			"${MYSQL_ROOT}"
			"$ENV{MYSQL_HOME}"
		PATHS
			"$ENV{SYSTEMDRIVE}/MySQL/*"
			"$ENV{PROGRAMFILES} (x86)/MySQL/*"
			"$ENV{PROGRAMFILES}/MySQL/*"
		PATH_SUFFIXES
			lib
			lib/mariadb
	)
endif()

# The runtime DLL that has to sit next to the executables.
get_filename_component(_mysql_library_dir "${MYSQL_LIBRARY}" DIRECTORY)
find_file(MYSQL_DLL
	NAMES
		libmysql.dll
		libmariadb.dll
	HINTS
		"${_mysql_library_dir}"
		"${_mysql_library_dir}/../bin"
		"${_mysql_library_dir}/../../bin"
	NO_DEFAULT_PATH
)
unset(_mysql_library_dir)

if(MYSQL_INCLUDE_DIR AND EXISTS "${MYSQL_INCLUDE_DIR}/mysql_version.h")
	file(STRINGS "${MYSQL_INCLUDE_DIR}/mysql_version.h" _mysql_version_str
		REGEX "^#define[\t ]+MYSQL_SERVER_VERSION[\t ]+\".*\"")
	string(REGEX REPLACE "^#define[\t ]+MYSQL_SERVER_VERSION[\t ]+\"([^\"]*)\".*" "\\1"
		MYSQL_VERSION "${_mysql_version_str}")
	unset(_mysql_version_str)
elseif(MYSQL_INCLUDE_DIR AND EXISTS "${MYSQL_INCLUDE_DIR}/mariadb_version.h")
	file(STRINGS "${MYSQL_INCLUDE_DIR}/mariadb_version.h" _mysql_version_str
		REGEX "^#define[\t ]+MARIADB_PACKAGE_VERSION[\t ]+\".*\"")
	string(REGEX REPLACE "^#define[\t ]+MARIADB_PACKAGE_VERSION[\t ]+\"([^\"]*)\".*" "\\1"
		MYSQL_VERSION "${_mysql_version_str}")
	unset(_mysql_version_str)
endif()

set(MYSQL_LIBRARIES ${MYSQL_LIBRARY})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MySQL
	REQUIRED_VARS
		MYSQL_LIBRARY
		MYSQL_INCLUDE_DIR
	VERSION_VAR
		MYSQL_VERSION
)

mark_as_advanced(MYSQL_INCLUDE_DIR MYSQL_LIBRARY MYSQL_DLL)

if (MYSQL_FOUND AND NOT TARGET MySQL::MySQL)
	add_library(MySQL::MySQL UNKNOWN IMPORTED)
	set_target_properties(MySQL::MySQL
		PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES "${MYSQL_INCLUDE_DIR}"
			IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
			IMPORTED_LOCATION "${MYSQL_LIBRARY}"
	)
endif()
