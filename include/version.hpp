#pragma once

// SDCPP_VERSION is captured at CMake configure time from the VERSION file —
// safe because it's a manual bump on a tag, not a per-commit value.
//
// SDCPP_GIT_COMMIT used to follow the same pattern (compile def) but that
// meant `cmake --build` after new commits embedded a STALE hash, so the
// /health endpoint's git_commit field lied about which source tree produced
// the binary. The commit hash now comes from
// ${CMAKE_BINARY_DIR}/generated/version_runtime.cpp — regenerated at build
// time by cmake/UpdateGitVersion.cmake. The file is rewritten only when the
// hash actually changes, so recompiles only happen on real commits.

#ifndef SDCPP_VERSION
#define SDCPP_VERSION "unknown"
#endif

namespace sdcpp {

// Defined in the build-time-generated version_runtime.cpp. The "-dirty"
// suffix is appended (by UpdateGitVersion.cmake) when the working tree has
// uncommitted changes, so /health surfaces "this binary was built from an
// uncommitted snapshot" without the caller needing to grep git status.
extern const char* const SDCPP_GIT_COMMIT_RUNTIME;

inline const char* get_version() {
    return SDCPP_VERSION;
}

inline const char* get_git_commit() {
    return SDCPP_GIT_COMMIT_RUNTIME;
}

} // namespace sdcpp
