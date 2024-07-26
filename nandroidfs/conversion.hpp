#pragma once

#include "dokan_no_winsock.h"
#include "responses.hpp"
#include <string>

// Various utility functions for converting from *nix formats to the windows equivalents
// 
// This includes dates/times, file attributes, and conversions from ResponseStatus to NTSTATUS
// for returning from a windows API call.

namespace nandroidfs {
    // Converts the ResponseStatus enum returned by a request to the daemon
    // into an NTSTATUS that can be returned from a dokan callback.
    NTSTATUS ntstatus_from_respstatus(ResponseStatus resp_status);

    // Converts the unix `st_mode` field on the `stat` struct into the windows file attributes.
    DWORD file_attributes_from_st_mode(uint16_t st_mode);

    // Converts a unix timestamp, measured in seconds since 1/1/1970 into the windows FILETIME format.
    FILETIME filetime_from_unix_time(uint64_t unix_time);

    // Converts a windows FILETIME into a unix timestamp.
    // This will result in a loss of resolution: the unix timestamp has resolution 1 second
    // whereas windows FILETIME has resolution 100ns.
    uint64_t unix_time_from_filetime(FILETIME file_time);

    // Converts a win32 file path to a unix file path.
    // i.e. converts UTF-16 to UTF-8 and `\` to `/`
    std::string win32_path_to_unix(LPCWSTR win32_path);
    // Converts a string to a wstring
    std::wstring wstring_from_string(std::string string);
    // Converts a unix file path to the version that can be used with win32 APIs
    std::wstring unix_path_to_win32(std::string unix_path);
}