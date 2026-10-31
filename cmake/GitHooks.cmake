# Points git at the repository's hooks in .githooks (pre-commit, commit-msg,
# pre-push), so a fresh clone gets them with its first configure.
# Run by CMakeLists.txt at configure time; also usable on its own:
#   cmake -DSOURCE_DIR=<checkout> -P cmake/GitHooks.cmake
# Does nothing outside a git checkout (a source archive) or without git, and
# never replaces a core.hooksPath that someone set to something else.
cmake_minimum_required(VERSION 3.20)

find_package(Git QUIET)
if(NOT GIT_FOUND OR NOT EXISTS "${SOURCE_DIR}/.git")
  return()
endif()

execute_process(COMMAND "${GIT_EXECUTABLE}" config --local --get core.hooksPath
                WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE current_hooks_path
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(current_hooks_path STREQUAL ".githooks")
  return()
elseif(current_hooks_path STREQUAL "")
  execute_process(COMMAND "${GIT_EXECUTABLE}" config --local core.hooksPath .githooks
                  WORKING_DIRECTORY "${SOURCE_DIR}" RESULT_VARIABLE git_result)
  if(NOT git_result EQUAL 0)
    message(FATAL_ERROR "Could not set core.hooksPath to .githooks in ${SOURCE_DIR}")
  endif()
  message(STATUS "Git hooks: core.hooksPath set to .githooks")
else()
  message(WARNING "core.hooksPath is '${current_hooks_path}', not .githooks: the repository's "
                  "pre-push check does not run. Fix: git config core.hooksPath .githooks")
endif()
