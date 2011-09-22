if (JSON_INCLUDE_DIR AND JSON_LIBRARY)
  set(JSON_FIND_QUIETLY TRUE)
endif (JSON_INCLUDE_DIR AND JSON_LIBRARY)

find_path(JSON_INCLUDE_DIR json.h
  PATHS /usr/include
  PATH_SUFFIXES json
)

find_library(JSON_LIBRARY
  NAMES json
  PATHS /usr/lib /usr/local/lib
)

set(JSON_LIBRARIES ${JSON_LIBRARY} )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JSON
  DEFAULT_MSG
  JSON_INCLUDE_DIR
  JSON_LIBRARIES
)

mark_as_advanced(JSON_INCLUDE_DIR JSON_LIBRARY)
