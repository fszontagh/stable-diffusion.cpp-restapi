#pragma once

// Version information is injected via compile definitions from CMakeLists.txt
// which reads from the VERSION file (single source of truth)

#ifndef SDCPP_VERSION
#define SDCPP_VERSION "unknown"
#endif

#ifndef SDCPP_GIT_COMMIT
#define SDCPP_GIT_COMMIT "unknown"
#endif

namespace sdcpp {

inline const char* get_version() {
    return SDCPP_VERSION;
}

inline const char* get_git_commit() {
    return SDCPP_GIT_COMMIT;
}

} // namespace sdcpp
