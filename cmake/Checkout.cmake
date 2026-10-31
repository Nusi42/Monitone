# Checkout setup, run by CMakeLists.txt at configure time; also usable on its own:
#   cmake -DSOURCE_DIR=<checkout> -P cmake/Checkout.cmake
# 1. The master checkout (local git config monitone.role=master) builds no
#    development builds: tools\build-release.ps1 builds releases from an
#    export of it in a folder outside (docs/BUILDING.md "Master and work folders").
# 2. Points git at the repository's hooks in .githooks (pre-commit, commit-msg,
#    pre-push), so a fresh clone gets them with its first configure. Never
#    replaces a core.hooksPath that someone set to something else.
# Does nothing outside a git checkout (a source archive) or without git.
cmake_minimum_required(VERSION 3.20)

find_package(Git QUIET)
if(NOT GIT_FOUND OR NOT EXISTS "${SOURCE_DIR}/.git")
  return()
endif()

execute_process(COMMAND "${GIT_EXECUTABLE}" config --local --get monitone.role
                WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE checkout_role
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(checkout_role STREQUAL "master")
  message(FATAL_ERROR "${SOURCE_DIR} is the master checkout: it builds only releases, with "
                      "tools/build-release.ps1 (outside the checkout). Build in a work folder.")
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
                  "hooks do not run. Fix: git config core.hooksPath .githooks")
endif()
