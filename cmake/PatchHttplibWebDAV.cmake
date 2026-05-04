# PatchHttplibWebDAV.cmake
#
# Adds WebDAV HTTP methods (PROPFIND, MKCOL, MOVE, COPY, LOCK, UNLOCK) to
# cpp-httplib's parse_request_line() allow-list, which by default only lets
# the standard HTTP/1.1 methods through. Without this patch, our pre-routing
# handler never sees those methods — the request is rejected at parse time
# with InvalidHTTPMethod (error 24, status 400).
#
# Idempotent: re-running on an already-patched header is a no-op.
#
# Required input variable:
#   HTTPLIB_HEADER — absolute path to the cpp-httplib httplib.h to patch.

if(NOT DEFINED HTTPLIB_HEADER)
    message(FATAL_ERROR "HTTPLIB_HEADER not defined; pass via -DHTTPLIB_HEADER=...")
endif()
if(NOT EXISTS "${HTTPLIB_HEADER}")
    message(FATAL_ERROR "httplib.h not found at: ${HTTPLIB_HEADER}")
endif()

file(READ "${HTTPLIB_HEADER}" CONTENT)

# Sentinel string we inject — checked first to make this idempotent.
set(SENTINEL "// SDCPP_WEBDAV_PATCH_APPLIED")

string(FIND "${CONTENT}" "${SENTINEL}" ALREADY_PATCHED)
if(NOT ALREADY_PATCHED EQUAL -1)
    message(STATUS "PatchHttplibWebDAV: already applied to ${HTTPLIB_HEADER}")
    return()
endif()

# Locate the methods set initializer. v0.36.0 has:
#   thread_local const std::set<std::string> methods{
#       "GET",     "HEAD",    "POST",  "PUT",   "DELETE",
#       "CONNECT", "OPTIONS", "TRACE", "PATCH", "PRI"};
# We append our methods inside the brace-init list before the closing "};".
set(NEEDLE [=[      "CONNECT", "OPTIONS", "TRACE", "PATCH", "PRI"};]=])
set(REPLACEMENT [=[      "CONNECT", "OPTIONS", "TRACE", "PATCH", "PRI",
      // SDCPP_WEBDAV_PATCH_APPLIED — extends the allow-list so WebDAV
      // verbs reach the pre-routing handler in request_handlers.cpp.
      "PROPFIND", "MKCOL", "MOVE", "COPY", "LOCK", "UNLOCK"};]=])

string(FIND "${CONTENT}" "${NEEDLE}" NEEDLE_POS)
if(NEEDLE_POS EQUAL -1)
    message(FATAL_ERROR
        "PatchHttplibWebDAV: failed to locate the methods set in ${HTTPLIB_HEADER}. "
        "cpp-httplib was likely upgraded — adjust the patch.")
endif()

string(REPLACE "${NEEDLE}" "${REPLACEMENT}" PATCHED "${CONTENT}")
file(WRITE "${HTTPLIB_HEADER}" "${PATCHED}")
message(STATUS "PatchHttplibWebDAV: applied to ${HTTPLIB_HEADER}")
