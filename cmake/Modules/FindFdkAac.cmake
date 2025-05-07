# Try to find FDKAAC library and include path.
# Once done this will define
#
# FDKAAC_INCLUDE_DIRS - where to find faad.h, etc.
# FDKAAC_LIBRARIES - List of libraries when using libfaad.
# FDKAAC_FOUND - True if libfaad found.

find_path(FDKAAC_INCLUDE_DIR fdk-aac/aacdecoder_lib.h DOC "The directory where fdk-aac/aacdecoder_lib.h resides")
find_library(FDKAAC_LIBRARY NAMES fdk-aac DOC "The libfdk-aac library")

if(FDKAAC_INCLUDE_DIR AND FDKAAC_LIBRARY)
  set(FDKAAC_FOUND 1)
  set(FDKAAC_LIBRARIES ${FDKAAC_LIBRARY})
  set(FDKAAC_INCLUDE_DIRS ${FDKAAC_INCLUDE_DIR})
else(FDKAAC_INCLUDE_DIR AND FDKAAC_LIBRARY)
  set(FDKAAC_FOUND 0)
  set(FDKAAC_LIBRARIES)
  set(FDKAAC_INCLUDE_DIRS)
endif(FDKAAC_INCLUDE_DIR AND FDKAAC_LIBRARY)

mark_as_advanced(FDKAAC_INCLUDE_DIR)
mark_as_advanced(FDKAAC_LIBRARY)
mark_as_advanced(FDKAAC_FOUND)

if(NOT FDKAAC_FOUND)
  set(FDKAAC_DIR_MESSAGE "libfaad was not found. Make sure FDKAAC_LIBRARY and FDKAAC_INCLUDE_DIR are set.")
  if(NOT FDKAAC_FIND_QUIETLY)
    message(STATUS "${FDKAAC_DIR_MESSAGE}")
  else(NOT FDKAAC_FIND_QUIETLY)
    if(FDKAAC_FIND_REQUIRED)
      message(FATAL_ERROR "${FDKAAC_DIR_MESSAGE}")
    endif(FDKAAC_FIND_REQUIRED)
  endif(NOT FDKAAC_FIND_QUIETLY)
endif(NOT FDKAAC_FOUND)
