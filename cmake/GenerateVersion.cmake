# Writes version.h from version.txt (maintained by tools/publish-release.ps1) and git state.
#
#   cmake -DSOURCE_DIR=<repo> -DOUTPUT=<file> [-DRELEASE_VERSION=X.Y.Z] -P GenerateVersion.cmake
#
# Release builds (HEAD is tag v<version>, clean tree) get the plain version,
# everything else gets SemVer build metadata, e.g. 0.3.0+g1a2b3c4.dirty, and
# the VS_FF_PRERELEASE flag in the version resource. RELEASE_VERSION builds the
# release candidate for a version that is tagged only after testing
# (tools/build-release.ps1).

cmake_minimum_required(VERSION 3.20)

set(dirty_result 0)
if(RELEASE_VERSION)
  set(MONITONE_VERSION "${RELEASE_VERSION}")
else()
  file(STRINGS "${SOURCE_DIR}/version.txt" MONITONE_VERSION LIMIT_COUNT 1)
  string(STRIP "${MONITONE_VERSION}" MONITONE_VERSION)
endif()
if(NOT MONITONE_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  message(FATAL_ERROR "version.txt must contain MAJOR.MINOR.PATCH, found '${MONITONE_VERSION}'")
endif()
set(MONITONE_VERSION_MAJOR ${CMAKE_MATCH_1})
set(MONITONE_VERSION_MINOR ${CMAKE_MATCH_2})
set(MONITONE_VERSION_PATCH ${CMAKE_MATCH_3})

set(MONITONE_GIT_REVISION "")
set(is_release FALSE)
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=8 HEAD
                  WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE MONITONE_GIT_REVISION
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE git_result)
  if(git_result EQUAL 0)
    execute_process(COMMAND "${GIT_EXECUTABLE}" diff --quiet HEAD --
                    WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE dirty_result)
    execute_process(COMMAND "${GIT_EXECUTABLE}" tag --points-at HEAD
                    WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE head_tags ERROR_QUIET)
    string(REPLACE "\n" ";" head_tags "${head_tags}")
    if(dirty_result EQUAL 0 AND "v${MONITONE_VERSION}" IN_LIST head_tags)
      set(is_release TRUE)
    endif()
  else()
    set(MONITONE_GIT_REVISION "")
  endif()
endif()

if(RELEASE_VERSION)
  set(is_release TRUE)
endif()

if(is_release OR MONITONE_GIT_REVISION STREQUAL "")
  set(MONITONE_VERSION_FULL "${MONITONE_VERSION}")
else()
  set(MONITONE_VERSION_FULL "${MONITONE_VERSION}+g${MONITONE_GIT_REVISION}")
  if(NOT dirty_result EQUAL 0)
    string(APPEND MONITONE_VERSION_FULL ".dirty")
  endif()
endif()
if(is_release)
  set(MONITONE_VERSION_FLAGS 0)
else()
  set(MONITONE_VERSION_FLAGS VS_FF_PRERELEASE)
endif()

# configure_file only touches OUTPUT when the content changes, so unchanged
# builds do not recompile.
configure_file("${CMAKE_CURRENT_LIST_DIR}/version.h.in" "${OUTPUT}" @ONLY)
