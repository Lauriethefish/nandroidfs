#include "conversion.hpp"
#undef max
#include <cmath>
#include <algorithm>
#include <codecvt>

namespace nandroidfs {
    NTSTATUS ntstatus_from_respstatus(ResponseStatus resp_status) {
        switch (resp_status) {
        case ResponseStatus::Success:
            return STATUS_SUCCESS;
        case ResponseStatus::FileNotFound:
            return STATUS_OBJECT_NAME_NOT_FOUND;
        case ResponseStatus::AccessDenied:
            return STATUS_ACCESS_DENIED;
        case ResponseStatus::FileExists:
            return STATUS_OBJECT_NAME_COLLISION;
        case ResponseStatus::NotADirectory:
            return STATUS_NOT_A_DIRECTORY;
        case ResponseStatus::DirectoryNotEmpty:
            return STATUS_DIRECTORY_NOT_EMPTY;
        default:
            return STATUS_UNSUCCESSFUL;

        }
    }

    DWORD file_attributes_from_st_mode(uint16_t st_mode) {
        uint16_t file_type = st_mode & 0xF000;
        switch (file_type) {
            // TODO: symlink support.
        case 0x4000:
            return FILE_ATTRIBUTE_DIRECTORY;
        case 0x8000:
        default:
            return FILE_ATTRIBUTE_NORMAL;
        }
    }

    // The number of seconds between 1 / 1 / 1970 and 1 / 1 / 1601.
    // This is the time difference between the unix epoch and the windows FILETIME epoch.
    // Crucially, this should NOT take into account the change from the Julian to Gregorian calender.
    // ...as windows does not take this into account.
    const uint64_t EPOCH_DIFFERENCE = 11644473600;

    FILETIME filetime_from_unix_time(uint64_t unix_time) {
        // Add the number of seconds between 1/1/1970 and 1/1/1601 so that the time is measured relative to the windows FILETIME epoch.
        uint64_t since_windows_epoch = unix_time + EPOCH_DIFFERENCE;

        // Windows filetime has resolution of 100ns, whereas unix_time is in seconds.
        // 1s = 1,000,000,000ns = 10,000,000 * 100ns
        uint64_t win_epoch_valid_res = 10000000 * since_windows_epoch;

        FILETIME file_time;
        file_time.dwLowDateTime = static_cast<DWORD>(win_epoch_valid_res);
        file_time.dwHighDateTime = static_cast<DWORD>(win_epoch_valid_res >> 32);
        return file_time;
    }

    uint64_t unix_time_from_filetime(FILETIME file_time) {
        uint64_t file_time_u64 = (static_cast<uint64_t>(file_time.dwHighDateTime) << 32) 
            | file_time.dwLowDateTime;
        // Convert from windows to unix resolution
        file_time_u64 /= 10000000;

        // Subtract the epoch diff. The result may be negative if the windows time is before 1970.
        int64_t signed_file_time = static_cast<int64_t>(file_time_u64) 
            - EPOCH_DIFFERENCE;
        // Ensure that the time is at least 0 before casting back to a uint64_t implicitly.
        return std::max(signed_file_time, 0LL);
    }

    std::string win32_path_to_unix(LPCWSTR win32_path) {
        // Work out how long the utf-8 string will be, in bytes.
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, win32_path, -1, NULL, 0, NULL, NULL);

        // Now we can actually convert the string to UTF-8
        std::string unix_path(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, win32_path, -1, &unix_path[0], size_needed, NULL, NULL);
        // Remove terminating null byte
        unix_path.resize(size_needed - 1);

        std::replace(unix_path.begin(), unix_path.end(), '\\', '/');
        // Normalise by removing the trailing `/` unless the path is the root.
        if (unix_path.length() > 1 && unix_path.ends_with('/')) {
            unix_path.pop_back();
        }

        return unix_path;
    }

    std::wstring wstring_from_string(std::string string) {
        // This API is deprecated but there is no STL equiavalent
        // Could use a windows API function instead.
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.from_bytes(string);
    }

    std::wstring unix_path_to_win32(std::string unix_path) {
        std::wstring win32_path = wstring_from_string(unix_path);
        std::replace(win32_path.begin(), win32_path.end(), '/', '\\');
        return win32_path;
    }
}