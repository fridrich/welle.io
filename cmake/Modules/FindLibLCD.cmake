if(NOT LIBLCD_FOUND)

  include(FindPkgConfig)
  pkg_check_modules(LIBLCD liblcd-0.0)

  if(LIBLCD_INCLUDE_DIRS AND LIBLCD_LIBRARIES)
	set(LIBLCD_FOUND TRUE CACHE INTERNAL "liblcd found")
	message(STATUS "Found liblcd: ${LIBLCD_INCLUDE_DIRS}, ${LIBLCD_LIBRARIES}")
  else()
	set(LIBLCD_FOUND FALSE CACHE INTERNAL "liblcd found")
	message(STATUS "liblcd not found.")
  endif()

  mark_as_advanced(LIBLCD_INCLUDE_DIRS LIBLCD_LIBRARIES)

endif()
