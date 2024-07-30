#include "ClientHandler.hpp"
#include "UnixException.hpp"
#include "requests.hpp"
#include "responses.hpp"
#include "path_utils.hpp"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdexcept>
#include <functional>
#include <iostream>
#include <string>
#include <stdio.h>

#define DEFAULT_DIRECTORY_MODE 16888
#define DEFAULT_FILE_MODE 33200
// The path of a file within the filesystem to use when checking the available space on disk.
#define FREE_SPACE_FS_PATH "/sdcard/"

namespace nandroidfs {
    ClientHandler::ClientHandler(int socket) : reader(DataReader(this, BUFFER_SIZE)), writer(DataWriter(this, BUFFER_SIZE)) {
        this->socket = socket;

        // Basic handshake, echo some bytes back to the client to ensure we have 2-way communication
        uint32_t handshake_bytes = reader.read_u32();
        writer.write_u32(handshake_bytes);
        writer.flush();
        std::cout << "Handshake complete" << std::endl;
    }

    ClientHandler::~ClientHandler() {
        close(socket);
    }

    int ClientHandler::read(uint8_t* buffer, int length) {
        return throw_unless(recv(socket, buffer, length, 0));
    }

    void ClientHandler::write(const uint8_t* buffer, int length) {
        // Continue writing until all of the bytes provided have definitely been written.
        while(length > 0) {
            int bytes_sent = throw_unless(send(socket, buffer, length, 0));

            length -= bytes_sent;
            buffer += bytes_sent;
        }
    }

    ResponseStatus get_status_from_errno() {
        int err_num = errno;
        //std::cout << "errno: " << err_num << strerror(err_num) << std::endl;
        switch(err_num) {
            case EACCES:
                return ResponseStatus::AccessDenied;
            case ENOENT:
                return ResponseStatus::FileNotFound;
            case EEXIST:
                return ResponseStatus::FileExists;
            case ENOTDIR:
                return ResponseStatus::NotADirectory;
            case EISDIR:
                return ResponseStatus::NotAFile;
            case ENOTEMPTY:
                return ResponseStatus::DirectoryNotEmpty;
            default:
                return ResponseStatus::GenericFailure;
        }
    }

    ResponseStatus stat_file(const char* path, FileStat* out_stat) {
        struct stat posix_stat;
        if(stat(path, &posix_stat) == -1) {
            return get_status_from_errno();
        }   else    {
            FileStat result(posix_stat.st_mode,
                posix_stat.st_size,
                posix_stat.st_atime,
                posix_stat.st_mtime);
            *out_stat = result;

            return ResponseStatus::Success;
        }
    }

    void ClientHandler::ensure_rw_buffer_size(size_t size) {
        if(size > rw_buffer.size()) {
            rw_buffer.resize(size);
        }
    }

    void ClientHandler::handle_list_dir_stats() {
        std::string directory_path = reader.read_utf8_string();
        //std::cout << "list_dir_stats: " << directory_path << std::endl;

        DIR* dir = opendir(directory_path.c_str());
        if(!dir) {
            writer.write_byte((uint8_t) get_status_from_errno());
            return;
        }

        // First indicate the request succeeded.
        writer.write_byte((uint8_t) ResponseStatus::Success);

        // Then write all the directory stats until a nullptr reached.
        dirent* current_entry = readdir(dir);
        while(current_entry) {
            std::string full_entry_path = get_full_path(directory_path, current_entry->d_name);

            FileStat stat;
            ResponseStatus status = stat_file(full_entry_path.c_str(), &stat);

            
            if(status == ResponseStatus::Success) {
                writer.write_byte((uint8_t) ResponseStatus::Success);
                writer.write_utf8_string(std::string_view(current_entry->d_name));
                stat.write(writer);
            }   else    {
                writer.write_byte((uint8_t) status);
            }

            //std::cout << "File stat for `" << full_entry_path << "`: " << stat.size << std::endl;
            current_entry = readdir(dir);
        }

        // Indicate that this is the end of the entries list.
        writer.write_byte((uint8_t) ResponseStatus::NoMoreEntries);

        closedir(dir);
        //std::cout << "Successfully listed dir" << std::endl;
    }

    void ClientHandler::handle_move_entry() {
        MoveEntryArgs args(reader);

        // Check if the destination file exists
        struct stat existing_stat;
        if(stat(args.to_path.c_str(), &existing_stat) == -1) {
            if(errno != ENOENT) {
                // Error occured that was not "file not found", return this.
                writer.write_byte((uint8_t) get_status_from_errno());
                return;
            }
        }   else if(!args.overwrite) { // File DID exist and overwriting not allowed.
            writer.write_byte((uint8_t) ResponseStatus::FileExists);
            return;
        }

        if(rename(args.from_path.c_str(), args.to_path.c_str()) == -1) {
            writer.write_byte((uint8_t) get_status_from_errno());
        }   else    {
            writer.write_byte((uint8_t) ResponseStatus::Success);
        }
    }

    void ClientHandler::handle_remove_file() {
        std::string file_path = reader.read_utf8_string();
        if(unlink(file_path.c_str()) == -1) {
            writer.write_byte((uint8_t) get_status_from_errno());
        }   else    {
            writer.write_byte((uint8_t) ResponseStatus::Success);
        }
    }

    void ClientHandler::handle_remove_directory() {
        std::string file_path = reader.read_utf8_string();
        if(rmdir(file_path.c_str()) == -1) {
            writer.write_byte((uint8_t) get_status_from_errno());
        }   else    {
            writer.write_byte((uint8_t) ResponseStatus::Success);
        }
    }

    void ClientHandler::handle_open_handle() {
        OpenHandleArgs args(reader);
        // Cannot open a handle with no read or write access, since this is useless.
        int creation_flags;
        if(!args.read_access && !args.write_access) {
            writer.write_byte((uint8_t) ResponseStatus::GenericFailure);
            return;
        }   else if(args.read_access && !args.write_access) {
            creation_flags = O_RDONLY;
        }   else if(args.write_access && !args.read_access) {
            creation_flags = O_WRONLY;
        }   else    {
            creation_flags = O_RDWR; // Both read and write access.
        }

        //std::cout << "open_handle: " << args.path << std::endl;
        switch(args.mode) {
            case OpenMode::CreateIfNotExist:
                //std::cout << "create if not exist" << std::endl;
                creation_flags |= O_CREAT;
                break;
            case OpenMode::Truncate:
                //std::cout << "trunacate" << std::endl;
                creation_flags |= O_TRUNC;
                break;
            case OpenMode::CreateOrTruncate:
                //std::cout << "create or trunacate" << std::endl;
                creation_flags |= O_CREAT | O_TRUNC;
                break;
            case OpenMode::CreateAlways:
                //std::cout << "create new" << std::endl;
                creation_flags |= O_CREAT | O_EXCL;
                break;
            case OpenMode::OpenOnly:
                //std::cout << "just opening" << std::endl;
                break;
                // OpenOnly is the default option for opening a file - no need for additional flags.
        }

        int fd = open(args.path.c_str(), creation_flags, DEFAULT_FILE_MODE);
        if(fd == -1) {
            writer.write_byte((uint8_t) get_status_from_errno());
        }   else    {
            writer.write_byte((uint8_t) ResponseStatus::Success);
            writer.write_u32(fd);
        }
    }

    void ClientHandler::handle_create_directory() {
        std::string dir_path = reader.read_utf8_string();
        if(mkdir(dir_path.c_str(), DEFAULT_DIRECTORY_MODE) == -1) {
            writer.write_byte((uint8_t) get_status_from_errno());
        }   else    {
            writer.write_byte((uint8_t) ResponseStatus::Success);
        }
    }

    void ClientHandler::handle_read_file() {
        ReadHandleArgs args(reader);

        if(::lseek(args.handle, args.offset, SEEK_SET) == -1) {
            writer.write_byte((uint8_t) get_status_from_errno());
            return;
        }

        // Ensure that the read buffer has enough space for the data length we're reading.
        ensure_rw_buffer_size(args.data_len);

        // Continue to read until we have read the length requested, or it's EOF.
        // It may seem like this is unnecessary: can't we just return fewer bytes than requested like most OS file reading functions?
        // However: When using memory mapped files, windows requires that the full buffer length provided is read from the file unless EOF has been reached
        // So to keep windows happy, we must keep reading until EOF or requested length.
        int total_read = 0;
        while(total_read < args.data_len) {
            // Read a maximum of the number of bytes remaining in the buffer.
            ssize_t read_result = ::read(args.handle, &rw_buffer[total_read], (args.data_len - total_read));
            if(read_result == -1) {
                writer.write_byte((uint8_t) get_status_from_errno());
                return;
            }

            total_read += read_result;
            if(read_result == 0) { // EOF condition
                break;
            }
        }
        //std::cout << "Total read: " << total_read << " buff size: " << rw_buffer.size() << " requested: " << args.data_len << std::endl;

        writer.write_byte((uint8_t) ResponseStatus::Success);
        writer.write_u32(total_read);
        writer.write_exact(&rw_buffer[0], total_read);
    }

    void ClientHandler::handle_write_file() {
        WriteHandleInitArgs args(reader);

        if(::lseek(args.handle, args.offset, SEEK_SET) == -1) {
            writer.write_byte((uint8_t) get_status_from_errno());
            return;
        }

        ensure_rw_buffer_size(args.data_len);
        reader.read_exact(&rw_buffer[0], args.data_len);

        // Keep writing until the entire buffer has been written.
        int total_written = 0;
        while(total_written < args.data_len) {
            ssize_t write_result = ::write(args.handle, &rw_buffer[total_written], (args.data_len - total_written));
            if(write_result == -1) {
                writer.write_byte((uint8_t) get_status_from_errno());
                return;
            }

            total_written += write_result;
        }

        writer.write_byte((uint8_t) ResponseStatus::Success);
    }

    void ClientHandler::handle_truncate_file() {
        TruncateHandleArgs args(reader);
        if(ftruncate(args.handle, args.new_length) == -1) {
            writer.write_byte((uint8_t) get_status_from_errno());
        }   else    {
            writer.write_byte((uint8_t) ResponseStatus::Success);
        }
    }

    // Gets a timespec from the provided timestamp (unix file timestamp in seconds, from Jan 1st 1970)
    // If the timestamp is -1, this will create a timespec that leaves the time unchanged.
    timespec get_timspec_from_timestamp(int64_t timestamp) {
        timespec spec;
        if(timestamp == -1) {
            spec.tv_nsec = UTIME_OMIT; // Leave time unchanged, tv_sec is ignored.
        }   else    {
            // TODO: It is possible to set file times with nanosecond precision
            // However, there seems to be no way to GET file times with this level of precision
            // So for now, we are only using a precision of 1 second.
            spec.tv_sec = timestamp;
            spec.tv_nsec = 0;
        }

        return spec;
    }

    void ClientHandler::handle_set_file_time() {
        SetFileTimeArgs args(reader);
        timespec timespecs[2];
        timespecs[0] = get_timspec_from_timestamp(args.access_time);
        timespecs[1] = get_timspec_from_timestamp(args.write_time);

        if(utimensat(/* ignored with absolute path */ 0, args.path.c_str(), timespecs, 0)) {
            writer.write_byte((uint8_t) get_status_from_errno());
        }   else    {
            writer.write_byte((uint8_t) ResponseStatus::Success);
        }
    }

    void ClientHandler::handle_get_disk_stats() {
        struct statvfs vfs_info;
        if(statvfs(FREE_SPACE_FS_PATH, &vfs_info) == -1) {
            writer.write_byte((uint8_t) get_status_from_errno());
        }   else    {
            writer.write_byte((uint8_t) ResponseStatus::Success);
            uint64_t block_size = vfs_info.f_bsize;
            DiskStats stats(block_size * vfs_info.f_bfree,
                block_size * vfs_info.f_favail,
                block_size * vfs_info.f_blocks);
            stats.write(writer);
        }
    }

    // Finds the parent directory of the given entry path.
    // Returns ResponseStatus::Success if the parent directory has read, write and execute permissions allowed for the current process.
    // Gives ResponseStatus::AccessDenied if any of these permissions are missing.
    ResponseStatus can_remove_directory_entry(std::string& path) {
        std::optional<std::string> parent_dir_path = get_parent_path(path);
        if(!parent_dir_path.has_value()) { // Trying to delete the root
            return ResponseStatus::AccessDenied; // Obviously a bad idea, deny access.
        }

        // Use the `access` syscall to see if we can actually read/write/ex the file with this mode.
        if(faccessat(0, parent_dir_path->c_str(), R_OK | W_OK | X_OK, AT_EACCESS)) {
            return ResponseStatus::Success;
        }   else    {
            std::cout << "Failed to remove dir entry" << std::endl;
            return get_status_from_errno();
        }
    }

    void ClientHandler::handle_check_remove_file() {
        // Whether or not we can remove a file only relies on having the right permissions on the parent directory
        // ... in order to `unlink` it.
        std::string file_path = reader.read_utf8_string();
        writer.write_byte((uint8_t) can_remove_directory_entry(file_path));
    }

    void ClientHandler::handle_check_remove_directory() {
        // To remove a directory, there is a secondary requirement: it needs to be empty.
        std::string dir_path = reader.read_utf8_string();
        ResponseStatus can_rem_entry = can_remove_directory_entry(dir_path);
        if(can_rem_entry != ResponseStatus::Success) {
            writer.write_byte((uint8_t) can_rem_entry);
            return;
        }
        
        DIR* dir = opendir(dir_path.c_str());
        if(!dir) {
            writer.write_byte((uint8_t) get_status_from_errno());
            return;
        }

        // Check if the directory is empty.
        dirent* entry = readdir(dir);
        bool has_entry = 0;
        while(entry) {
            // Need to allow `.` and `..` in an empty directory.
            // If the entry is not `.` or `..`, skip it.
            // Otherwise, there is an entry in the directory, so we can't delete it.
            if((strcmp(entry->d_name, ".") != 0) && (strcmp(entry->d_name, "..") != 0)) {
                has_entry = true;
                break;
            }
            entry = readdir(dir);
        }
        closedir(dir);

        if(has_entry)  {
            writer.write_byte((uint8_t) ResponseStatus::DirectoryNotEmpty);
        }   else    {
            writer.write_byte((uint8_t) ResponseStatus::Success);
        }
    }

    void ClientHandler::handle_messages() {
        RequestType req_type;
        while(true) {
            try
            {
                req_type = (RequestType) reader.read_byte();
            }
            catch(const EOFException&)
            {
                std::cout << "Disconnected from client" << std::endl;
                return;
            }

            switch(req_type) {
                case RequestType::StatFile:
                {
                    std::string file_path = reader.read_utf8_string();
                    //std::cout << "stat_file: " << file_path << std::endl;
                    FileStat stat;
                    ResponseStatus status = stat_file(file_path.c_str(), &stat);

                    writer.write_byte((uint8_t) status);
                    if(status == ResponseStatus::Success) {
                        stat.write(writer); 
                    }

                    break;
                }
                case RequestType::ListDirectory:
                    handle_list_dir_stats();
                    break;
                case RequestType::MoveEntry:
                    handle_move_entry();
                    break;
                case RequestType::RemoveFile:
                    handle_remove_file();
                    break;
                case RequestType::RemoveDirectory:
                    handle_remove_directory();
                    break;
                case RequestType::CreateDirectory:
                    handle_create_directory();
                    break;
                case RequestType::OpenHandle:
                    handle_open_handle();
                    break;
                case RequestType::ReadHandle:
                    handle_read_file();
                    break;
                case RequestType::WriteHandle:
                    handle_write_file();
                    break;
                case RequestType::CloseHandle: {
                    int handle = reader.read_u32();
                    //std::cout << "closing: " << handle << std::endl;
                    close(handle);
                    writer.write_byte((uint8_t) ResponseStatus::Success);
                    break;
                }
                case RequestType::TruncateHandle:
                    handle_truncate_file();
                    break;
                case RequestType::SetFileTime:
                    handle_set_file_time();
                    break;
                case RequestType::GetDiskStats:
                    handle_get_disk_stats();
                    break;
                case RequestType::CheckRemoveFile:
                    handle_check_remove_file();
                    break;
                case RequestType::CheckRemoveDirectory:
                    handle_check_remove_directory();
                    break;
                default:
                    std::cerr << "Unknown request type " << std::to_string((uint8_t) req_type) << std::endl;
                    throw std::runtime_error("Unknown request type received!");
                    break;
            }

            writer.flush();
        }
    }
}