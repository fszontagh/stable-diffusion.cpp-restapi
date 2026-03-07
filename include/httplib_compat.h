#pragma once

// Wrapper for cpp-httplib that suppresses internal deprecation warnings.
// httplib v0.36.0 has self-referencing deprecated API calls that trigger
// -Wdeprecated-declarations even though we don't use those APIs.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <httplib.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
