#pragma once
#include "requests.hpp"
#include "serialization.hpp"

// Responses are written as a StatusCode enum (1 byte) 
// followed by the response data, or no data if the StatusCode is not that of a success.

namespace nandroidfs 
{
    // A code given that represents each response.
    // NB:
    // A ResponseStatus is also sent with each FileStat when listing the files in a directory.
    // If the status is NoMoreEntries, then the final file stat has just been sent.
    // If the status is Success, then a FileStat is included with the file name.
    enum class ResponseStatus {
        Success,
        GenericFailure,
        AccessDenied,
        NotADirectory,
        NotAFile,
        FileNotFound,
        FileExists, // Considered a success if opening a file with a mode that supports existing files.
        DirectoryNotEmpty,
        NoMoreEntries
    };

    // The information about a file returned by the daemon 
    struct FileStat 
    {
        // Unix file mode.
        uint16_t mode;
        // File size, in bytes.
        // TODO: Make this 64 bit.
        uint64_t size;

        // All times are measured in seconds since 01/01/1970.

        // Last time file was read from.
        uint64_t access_time;
        // Last time at which the file was written to
        uint64_t write_time;

        // Reads a FileStat from the given reader.
        FileStat();
        FileStat(uint16_t mode, uint64_t size, uint64_t access_time, uint64_t write_time);
        FileStat(DataReader& reader);
        void write(DataWriter& writer);
    };

    struct DiskStats {
        uint64_t free_bytes; // Free blocks including that only available to root.
        uint64_t available_bytes; // Available to unprivileged users
        uint64_t total_bytes; // Total space on the file system.

        DiskStats(uint64_t free_bytes, uint64_t available_bytes, uint64_t total_bytes);
        DiskStats();
        DiskStats(DataReader& reader);
        void write(DataWriter& writer);
    };


}