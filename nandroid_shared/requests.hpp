#pragma once

#include <string>
#include "serialization.hpp"

#define NANDROID_PORT 25818
#define NANDROID_READY "NANDROID_READY_FOR_CONNECTION"

// Requests are sent as a RequestType (1 byte), 
// followed by the request body.

namespace nandroidfs 
{
    typedef uint32_t FILE_HANDLE;

    enum class RequestType : uint8_t
    {
        // Followed by a singular string - the full file/directory path.
        StatFile,
        // Followed by a singular string - the full directory path.
        ListDirectory,
        // Followed by a singular string - the full directory path.
        CreateDirectory,
        // Followed by the file path.
        // Checks if it is possible to remove the file at the given path.
        // 
        // This is necessary as dokan has a separate callback for checking it can delete before it actually releases
        // the file for us to clean-up.
        CheckRemoveFile,
        // Followed by directory path.
        // Checks if it is possible to remove the directory at the given path.
        CheckRemoveDirectory,
        // Followed by file path.
        RemoveFile,
        // Followed by directory path.
        RemoveDirectory,
        // Moves the entry at the specified path to a different location
        MoveEntry,
        // Followed by OpenHandleArgs
        OpenHandle,
        // Followed by the FILE_HANDLE
        CloseHandle,
        // Followed by ReadHandleArgs
        // Response is the number of bytes read (uint32_t), followed by the data read.
        ReadHandle,
        // Followed by WriteHandleArgs
        WriteHandle,
        // Followed by TruncateHandleArgs
        TruncateHandle,
        // Followed by SetFileTimeArgs
        SetFileTime,
        // No additional arguments
        // Gives a DiskStats as its response.
        GetDiskStats
    };

    enum class OpenMode  : uint8_t
    {
        // Only allows opening an existing file
        OpenOnly,
        // Creates a file if it does not exist, otherwise opens an existing file with no changes.
        CreateIfNotExist,
        // Only allows opening an existing file, once it is opened, the file's length is set to 0.
        Truncate,
        // Creates a new file if one does not exist, otherwise it trunacates (overwrites) the existing file.
        CreateOrTruncate,
        // Creates a new file if one does not exist, if one does exist, it gives an error.
        CreateAlways
    };

    struct OpenHandleArgs
    {
        // Full file path.
        std::string path;
        OpenMode mode;
        bool read_access;
        bool write_access;

        OpenHandleArgs(DataReader& reader);
        OpenHandleArgs(std::string path, OpenMode mode, bool read_access, bool write_access);
        void write(DataWriter& writer);
    };

    struct ReadHandleArgs
    {
        FILE_HANDLE handle;
        // Length of data to read from the file.
        // The full length of data requested will always be read, except in the case of EOF.
        uint32_t data_len;
        // Offset in the file at which to read that data.
        uint64_t offset;

        ReadHandleArgs(DataReader& reader);
        ReadHandleArgs(FILE_HANDLE handle, uint32_t data_len, uint64_t offset);
        void write(DataWriter& writer);
    };

    // Followed by the actual data to write.
    struct WriteHandleInitArgs
    {
        FILE_HANDLE handle;
        // Offset *in the file* at which to write the data. 
        uint64_t offset;
        // Length of data, in bytes.
        // It is presumed that each write will not be larger than 4GB - this would
        // be a frankly ridiculously sized buffer.
        uint32_t data_len;

        WriteHandleInitArgs(DataReader& reader);
        WriteHandleInitArgs(FILE_HANDLE handle, uint64_t offset, uint32_t data_len);
        void write(DataWriter& writer);
    };

    struct MoveEntryArgs {
        std::string from_path; // Origin file
        std::string to_path; // Destination location
        bool overwrite; // Whether to allow overwriting the destination file.

        MoveEntryArgs(DataReader& reader);
        MoveEntryArgs(std::string from_path, std::string to_path, bool overwrite);
        void write(DataWriter& writer);
    };

    // Arguments for a request to increase or decrease the length of a file.
    struct TruncateHandleArgs {
        FILE_HANDLE handle;
        uint64_t new_length;

        TruncateHandleArgs(DataReader& reader);
        TruncateHandleArgs(FILE_HANDLE handle, uint64_t new_length);
        void write(DataWriter& writer);
    };

    // Arguments for a request to set when a file was last read from/written to.
    struct SetFileTimeArgs {
        std::string path;

        // A time of -1 indicates that the time should not be set.

        int64_t access_time;
        int64_t write_time;

        SetFileTimeArgs(DataReader& reader);
        SetFileTimeArgs(std::string path, int64_t access_time, int64_t write_time);
        void write(DataWriter& writer);
    };
}