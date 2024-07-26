#include "operations.hpp"
#include "Nandroid.hpp"
#include "FileContext.hpp"
#include "conversion.hpp"
#include <iostream>

#define NAN_CTX reinterpret_cast<::nandroidfs::Nandroid*>(file_info->DokanOptions->GlobalContext)
// Utility macro to get the connection from the dokan context.
#define NAN_CONN NAN_CTX->get_conn()
#define NAN_FILE_CTX reinterpret_cast<::nandroidfs::FileContext*>(file_info->Context)
#define NAN_HANDLER_START try {

#define NAN_HANDLER_END } catch(::nandroidfs::EOFException&) { \
    } catch(::std::exception& ex) { \
    ::std::cerr << "Exception in dokan callback: " << ex.what() << " unmounting!" << ::std::endl; \
    } catch(...) { \
    ::std::cerr << "Unkown exception in dokan callback - unmounting" << ::std::endl; \
    } \
    NAN_CTX->unmount(); \
    return STATUS_UNSUCCESSFUL; \

#define NAN_HANDLER_END_VOID return; } catch(::nandroidfs::EOFException&) { \
    } catch(::std::exception& ex) { \
    ::std::cerr << "Exception in dokan callback: " << ex.what() << " unmounting!" << ::std::endl; \
    } catch(...) { \
    ::std::cerr << "Unkown exception in dokan callback - unmounting" << ::std::endl; \
    } \
    NAN_CTX->unmount(); \
    return

namespace nandroidfs {
    static NTSTATUS handle_create_directory(LPCWSTR path, Connection& conn, DWORD creation_disposition, FileStat stat,
        bool entry_exists,
        bool entry_is_directory) {
        // These are the only creation dispositions that will create a new directory.
        if (creation_disposition == OPEN_ALWAYS ||
            creation_disposition == CREATE_NEW ||
            creation_disposition == CREATE_ALWAYS) {
            return ntstatus_from_respstatus(conn.req_create_directory(path));
        }
        else if(entry_exists && entry_is_directory)
        {
            return STATUS_SUCCESS;
        }
        else if (!entry_exists) {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }
        else if (!entry_is_directory) {
            return STATUS_NOT_A_DIRECTORY;
        }
        else
        {
            return STATUS_UNSUCCESSFUL;
        }
    }

    static NTSTATUS handle_create_file(LPCWSTR path,
        Connection& conn,
        DWORD creation_disposition,
        bool file_exists,
        FileContext* ctx) {
        OpenMode mode;
        switch (creation_disposition) {
            case CREATE_ALWAYS:
                mode = OpenMode::CreateOrTruncate;
                break;
            case CREATE_NEW:
                mode = OpenMode::CreateAlways;
                break;
            case OPEN_ALWAYS:
                mode = OpenMode::CreateIfNotExist;
                // If no read access or write access has been specified and the file already exists:
                // This call will leave the existing file umodified, so no changes have been made whatsoever to the filesystem.
                // There is therefore no need to actually call `open` from Android, as we do not need to make any changes.
                if (!ctx->read_access && !ctx->write_access && file_exists) {
                    return STATUS_SUCCESS;
                }

                break;
            case OPEN_EXISTING:
                // As before, this call will not require a file descriptor to be opened if not asking for read or write access.
                if ((!ctx->read_access && !ctx->write_access && file_exists)) {
                    return STATUS_SUCCESS;
                }

                // Early check - if the file does not exist, no need to send a request to the daemon to try and open the file.
                if (!file_exists) {
                    return STATUS_OBJECT_NAME_NOT_FOUND;
                }

                mode = OpenMode::OpenOnly;
                break;
            case TRUNCATE_EXISTING:
                mode = OpenMode::Truncate;
                break;
            default:
                std::cerr << "Unknown creation disposition" << std::endl;
                return STATUS_UNSUCCESSFUL;
        }

        // In POSIX, we cannot open a file without at least read or write access
        // In windows, having neither access is in fact legal, so we will set at read_access to true if neither access is specified.
        // This allows a file to still be created.
        if (!ctx->read_access && !ctx->write_access) {
            ctx->read_access = true;
        }
        
        //std::wcout << "Opening file through ADB: " << path << " mode : " << creation_disposition <<
        //    " read_access: " << ctx->read_access << " write_access: " << ctx->write_access << std::endl;
        ResponseStatus status = conn.req_open_file(path, mode, ctx->read_access, ctx->write_access, ctx->handle);
        if (status == ResponseStatus::Success) {
            // Check if the file existed already and give the correct status if so.
            if (file_exists && (creation_disposition == OPEN_ALWAYS || creation_disposition == CREATE_ALWAYS)) {
                return STATUS_OBJECT_NAME_COLLISION;
            }
            else
            {
                return STATUS_SUCCESS;
            }
        }
        else
        {
            return ntstatus_from_respstatus(status);
        }
    }

    static NTSTATUS DOKAN_CALLBACK create_file(LPCWSTR file_name,
        PDOKAN_IO_SECURITY_CONTEXT security_context,
        ACCESS_MASK desired_access,
        ULONG file_attributes,
        ULONG /*shareaccess*/,
        ULONG create_disposition,
        ULONG create_options,
        PDOKAN_FILE_INFO file_info)
    {
        NAN_HANDLER_START;
        ACCESS_MASK generic_desiredaccess;
        DWORD creation_disposition;
        DWORD file_attributes_and_flags;

        DokanMapKernelToUserCreateFileFlags(desired_access, file_attributes, create_options, create_disposition,
            &generic_desiredaccess, &file_attributes_and_flags, &creation_disposition);

        // Initialise the context for this file handle.
        FileContext* context = new FileContext();
        // Later, if handle_create_file decides that a file handle needs 
        // to be opened from the android device, this will be set.
        context->handle = -1;

        file_info->DeleteOnClose = (file_attributes_and_flags & FILE_FLAG_DELETE_ON_CLOSE) ? true : false;

        context->read_access = generic_desiredaccess & GENERIC_READ;
        context->write_access = generic_desiredaccess & GENERIC_WRITE;

        file_info->Context = reinterpret_cast<ULONG64>(context);

        // First of all, stat the file to get its initial status
        Connection& conn = NAN_CONN;
        FileStat stat;
        ResponseStatus status = conn.req_stat_file(file_name, stat);
        bool entry_exists; // Whether or not an entry with the specified path already exists.
        if (status == ResponseStatus::Success) {
            entry_exists = true;
        }
        else if(status == ResponseStatus::FileNotFound)
        {
            entry_exists = false;
        }
        else
        {
            // Error occurred while statting the file, return this to the caller.
            return ntstatus_from_respstatus(status);
        }

        bool entry_is_directory = file_attributes_from_st_mode(stat.mode) & FILE_ATTRIBUTE_DIRECTORY;

        // Check if existing entry is a directory, and inform dokan as such.
        if (entry_exists && entry_is_directory) {
            // If we're being asked to create a file handle only, terminate here.
            if (create_options & FILE_NON_DIRECTORY_FILE) {
                return STATUS_FILE_IS_A_DIRECTORY;
            }
            file_info->IsDirectory = true;
        }

        if (file_info->IsDirectory) {
            return handle_create_directory(file_name, conn, creation_disposition, stat, entry_exists, entry_is_directory);
        }
        else
        {
            return handle_create_file(file_name, conn, creation_disposition, entry_exists, context);
        }

        NAN_HANDLER_END;
    }

    static void DOKAN_CALLBACK clean_up(LPCWSTR file_name, PDOKAN_FILE_INFO file_info)
    {
        NAN_HANDLER_START;

        Connection& conn = NAN_CONN;
        if (file_info->DeleteOnClose) {
            if (file_info->IsDirectory) {
                conn.req_remove_directory(file_name);
            }
            else
            {
                conn.req_remove_file(file_name);
            }
        }

        NAN_HANDLER_END_VOID;
    }

    static void DOKAN_CALLBACK close_file(LPCWSTR file_name, PDOKAN_FILE_INFO file_info) {
        NAN_HANDLER_START;

        FileContext* ctx = NAN_FILE_CTX;
        Connection& conn = NAN_CONN;
        if (ctx->handle != -1) {
            //std::cout << "Closing ADB file descriptor " << ctx->handle << std::endl;
            conn.req_close_file(ctx->handle);
        }

        delete ctx;
        NAN_HANDLER_END_VOID;
    }

    static NTSTATUS DOKAN_CALLBACK read_file(LPCWSTR file_name,
        LPVOID buffer,
        DWORD buffer_len,
        // Output number of bytes actually read. NOTE: Memory mapped applications will fail to read properly if you ever give them fewer bytes than they request.
        LPDWORD read_len,
        LONGLONG offset,
        PDOKAN_FILE_INFO file_info)
    {
        NAN_HANDLER_START;

        FileContext* context = NAN_FILE_CTX;
        Connection& conn = NAN_CONN;
        if (!context->read_access || context->handle == -1) {
            return STATUS_ACCESS_DENIED;
        }
        else
        {
            int bytes_read;
            ResponseStatus status = conn.req_read_from_file(context->handle,
                offset,
                reinterpret_cast<uint8_t*>(buffer),
                buffer_len,
                bytes_read);

            if (status == ResponseStatus::Success) {
                *read_len = bytes_read;
                return STATUS_SUCCESS;
            }
            else
            {
                return ntstatus_from_respstatus(status);
            }
        }

        NAN_HANDLER_END;
    }

    static NTSTATUS DOKAN_CALLBACK write_file(LPCWSTR file_name,
        LPCVOID buffer,
        DWORD number_of_bytes_to_write,
        LPDWORD number_of_bytes_written,
        LONGLONG offset,
        PDOKAN_FILE_INFO file_info) {
        NAN_HANDLER_START;

        FileContext* context = NAN_FILE_CTX;
        Connection& conn = NAN_CONN;
        if (!context->write_access || context->handle == -1) {
            return STATUS_ACCESS_DENIED;
        }
        else
        {
            ResponseStatus status = conn.req_write_to_file(context->handle,
                offset,
                reinterpret_cast<const uint8_t*>(buffer),
                number_of_bytes_to_write);

            if (status == ResponseStatus::Success) {
                *number_of_bytes_written = number_of_bytes_to_write;
                return STATUS_SUCCESS;
            }
            else
            {
                return ntstatus_from_respstatus(status);
            }
        }

        NAN_HANDLER_END;
    }

    // We do not maintain a buffer for each file, so this callback does nothing
    static NTSTATUS DOKAN_CALLBACK flush_file_buffers(LPCWSTR file_name,
        PDOKAN_FILE_INFO file_info) {
        return STATUS_SUCCESS;
    }

    static NTSTATUS DOKAN_CALLBACK get_file_information(LPCWSTR filename,
        LPBY_HANDLE_FILE_INFORMATION buffer,
        PDOKAN_FILE_INFO file_info) {
        NAN_HANDLER_START;

        Connection& conn = NAN_CONN;
        FileStat stat;
        //std::wcout << L"Statting file " << filename << " thread ID: " << GetCurrentThreadId() << std::endl;
        ResponseStatus status = conn.req_stat_file(filename, stat);
        if (status != ResponseStatus::Success) {
            return ntstatus_from_respstatus(status);
        }

        buffer->dwFileAttributes = file_attributes_from_st_mode(stat.mode);
        buffer->nFileSizeLow = static_cast<uint32_t>(stat.size);
        buffer->nFileSizeHigh = static_cast<uint32_t>(stat.size >> 32);
        buffer->ftLastAccessTime = filetime_from_unix_time(stat.access_time);
        buffer->ftLastWriteTime = filetime_from_unix_time(stat.write_time);
        // No way to get creation time as the android filesystem does not store birth time.

        if (buffer->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            file_info->IsDirectory = true;
        }

        return STATUS_SUCCESS;
        NAN_HANDLER_END;
    }

    static NTSTATUS DOKAN_CALLBACK find_files(LPCWSTR filename,
        PFillFindData fill_finddata,
        PDOKAN_FILE_INFO file_info) {
        NAN_HANDLER_START;

        Connection& conn = NAN_CONN;
        // List all the file stats and convert them into the form dokan needs them in.
        //std::wcout << L"Listing file stats in " << filename << " thread id: " << GetCurrentThreadId() << std::endl;
        ResponseStatus status = conn.req_list_file_stats(filename, [fill_finddata, file_info](FileStat stat, std::wstring file_name) {
            WIN32_FIND_DATAW find_data;
            ZeroMemory(&find_data, sizeof(WIN32_FIND_DATAW));
            find_data.dwFileAttributes = file_attributes_from_st_mode(stat.mode);
            find_data.nFileSizeLow = static_cast<uint32_t>(stat.size);
            find_data.nFileSizeHigh = static_cast<uint32_t>(stat.size >> 32);
            find_data.ftLastAccessTime = filetime_from_unix_time(stat.access_time);
            find_data.ftLastWriteTime = filetime_from_unix_time(stat.write_time);
            // No way to get creation time as the android filesystem does not store birth time.

            wcscpy_s(find_data.cFileName, file_name.c_str());
            fill_finddata(&find_data, file_info);
        });

        return ntstatus_from_respstatus(status);
        NAN_HANDLER_END;
    }

    static NTSTATUS DOKAN_CALLBACK set_file_attributes(
        LPCWSTR file_name, DWORD file_attributes, PDOKAN_FILE_INFO file_info)
    {
        NAN_HANDLER_START;

        // This doesn't do very much as there aren't any windows file attributes that I can see have sensible *nix equivalents.
        // Obviously FILE_ATTRIBUTE_DIRECTORY has a *nix equivalent but this actually can't be set by set_file_attributes.
        // This method is implemented to return a success as long as the provided file attributes match the existing file, otherwise it fails.
        Connection& conn = NAN_CONN;

        // First of all get the file statistics.
        FileStat stat;
        ResponseStatus stat_status = conn.req_stat_file(file_name, stat);

        if (stat_status != ResponseStatus::Success) {
            return ntstatus_from_respstatus(stat_status);
        }
        bool entry_is_dir = file_attributes_from_st_mode(stat.mode) & FILE_ATTRIBUTE_DIRECTORY;

        if (file_attributes == 0 || file_attributes == FILE_ATTRIBUTE_NORMAL) { // Typical attributes for a file
            return entry_is_dir ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
        }
        else if (file_attributes == FILE_ATTRIBUTE_DIRECTORY) { // Typical attributes for a directory
            return entry_is_dir ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        }
        else
        {
            // Unsupported file attributes supplied - cannot set them.
            return STATUS_UNSUCCESSFUL;
        }

        return STATUS_NOT_IMPLEMENTED;
        NAN_HANDLER_END;
    }

    int64_t signed_time_from_ptr(CONST FILETIME* file_time) {
        if (file_time) {
            if (file_time->dwHighDateTime == 0 && file_time->dwLowDateTime == 0) {
                return -1; // Setting to 0, which also indicates not to change the time.
            }
            else
            {
                return unix_time_from_filetime(*file_time); // Clamps to Jan 1st 1970.
            }
        }
        else
        {
            return -1; // Not setting the time.
        }
    }

    static NTSTATUS DOKAN_CALLBACK set_file_time(LPCWSTR file_name, CONST FILETIME* creation_time,
        CONST FILETIME* last_access_time, CONST FILETIME* last_write_time,
        PDOKAN_FILE_INFO file_info)
    {
        NAN_HANDLER_START;

        Connection& conn = NAN_CONN;
        // Check each pointer for null before dereferencing it to get the time.

        // Convert the file times into their unix equivalents and then send a request to the daemon to set the file time.
        ResponseStatus status = conn.req_set_file_time(file_name, 
            signed_time_from_ptr(last_access_time),
            signed_time_from_ptr(last_write_time));
        // (Creation time is not supported, as the Android filesystem doesn't support it.)

        return ntstatus_from_respstatus(status);
        NAN_HANDLER_END;
    }

    static NTSTATUS DOKAN_CALLBACK delete_file(LPCWSTR file_name, PDOKAN_FILE_INFO file_info)
    {
        NAN_HANDLER_START;

        Connection& conn = NAN_CONN;
        return ntstatus_from_respstatus(conn.req_can_remove_file(file_name));

        NAN_HANDLER_END;
    }

    static NTSTATUS DOKAN_CALLBACK delete_directory(LPCWSTR file_name, PDOKAN_FILE_INFO file_info)
    {
        NAN_HANDLER_START;

        Connection& conn = NAN_CONN;
        return ntstatus_from_respstatus(conn.req_can_remove_directory(file_name));
        NAN_HANDLER_END;
    }

    static NTSTATUS DOKAN_CALLBACK move_file(LPCWSTR file_name, LPCWSTR new_file_name,
        BOOL replace_if_existing,
        PDOKAN_FILE_INFO file_info)
    {
        NAN_HANDLER_START;

        Connection& conn = NAN_CONN;
        return ntstatus_from_respstatus(conn.req_move_entry(file_name, new_file_name, replace_if_existing));

        NAN_HANDLER_END;
    }

    static NTSTATUS DOKAN_CALLBACK set_end_of_file(
        LPCWSTR file_name, LONGLONG byte_offset, PDOKAN_FILE_INFO file_info)
    {
        NAN_HANDLER_START;
        Connection& conn = NAN_CONN;
        FileContext* ctx = NAN_FILE_CTX;

        if (ctx->write_access) {
            ResponseStatus status = conn.req_set_file_len(ctx->handle, byte_offset);
            return ntstatus_from_respstatus(status);
        }
        else
        {
            return STATUS_ACCESS_DENIED;
        }
        NAN_HANDLER_END;
    }

    static NTSTATUS DOKAN_CALLBACK set_allocation_size(
        LPCWSTR file_name, LONGLONG byte_offset, PDOKAN_FILE_INFO file_info)
    {
        return set_end_of_file(file_name, byte_offset, file_info);
    }

    static NTSTATUS DOKAN_CALLBACK lock_file(LPCWSTR file_name,
        LONGLONG byte_offset,
        LONGLONG length,
        PDOKAN_FILE_INFO file_info)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    static NTSTATUS DOKAN_CALLBACK unlock_file(LPCWSTR file_name,
        LONGLONG byte_offset,
        LONGLONG length,
        PDOKAN_FILE_INFO file_info)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    static NTSTATUS DOKAN_CALLBACK get_disk_free_space(PULONGLONG free_bytes_available,
        PULONGLONG total_number_of_bytes,
        PULONGLONG total_number_of_free_bytes,
        PDOKAN_FILE_INFO file_info)
    {
        NAN_HANDLER_START;

        Connection& conn = NAN_CONN;

        DiskStats stats;
        ResponseStatus status = conn.req_get_disk_stats(stats);
        if (status == ResponseStatus::Success) {
            *total_number_of_bytes = stats.total_bytes;
            *total_number_of_free_bytes = stats.free_bytes;
            *free_bytes_available = stats.available_bytes;
            return STATUS_SUCCESS;
        }
        else
        {
            return ntstatus_from_respstatus(status);
        }

        NAN_HANDLER_END;
    }

    const DWORD nandroid_volume_serial = 0x20233838;
    static NTSTATUS DOKAN_CALLBACK get_volume_information(
        LPWSTR volume_name_buffer,
        DWORD volume_name_size,
        LPDWORD volume_serialnumber,
        LPDWORD maximum_component_length,
        LPDWORD fs_flags,
        LPWSTR fs_name_buffer,
        DWORD fs_name_size,
        PDOKAN_FILE_INFO file_info) {
        NAN_HANDLER_START;

        //std::cout << "get_volume_information" << std::endl;
        wcscpy_s(volume_name_buffer, volume_name_size, NAN_CTX->get_device_serial_wide().c_str());
        *volume_serialnumber = nandroid_volume_serial;
        *maximum_component_length = 255;
        *fs_flags = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES | FILE_UNICODE_ON_DISK;

        wcscpy_s(fs_name_buffer, fs_name_size, L"NTFS");
        return STATUS_SUCCESS;
        NAN_HANDLER_END;
    }

    static NTSTATUS DOKAN_CALLBACK mounted(LPCWSTR mount_point, PDOKAN_FILE_INFO file_info)
    {
        return STATUS_SUCCESS;
    }

    static NTSTATUS DOKAN_CALLBACK unmounted(PDOKAN_FILE_INFO file_info) {
        return STATUS_SUCCESS;
    }

    static NTSTATUS DOKAN_CALLBACK get_file_security(
        LPCWSTR file_name,
        PSECURITY_INFORMATION security_information,
        PSECURITY_DESCRIPTOR security_descriptor,
        ULONG buffer_length,
        PULONG length_needed,
        PDOKAN_FILE_INFO file_info)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    static NTSTATUS DOKAN_CALLBACK set_file_security(
        LPCWSTR file_name,
        PSECURITY_INFORMATION security_information,
        PSECURITY_DESCRIPTOR security_descriptor,
        ULONG buffer_length,
        PDOKAN_FILE_INFO file_info)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    static NTSTATUS DOKAN_CALLBACK find_streams(LPCWSTR filename,
        PFillFindStreamData fill_findstreamdata,
        PVOID findstreamcontext,
        PDOKAN_FILE_INFO file_info)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    DOKAN_OPERATIONS nandroid_operations = { create_file,
                                         clean_up,
                                         close_file,
                                         read_file,
                                         write_file,
                                         flush_file_buffers,
                                         get_file_information,
                                         find_files,
                                         nullptr,  // FindFilesWithPattern
                                         set_file_attributes,
                                         set_file_time,
                                         delete_file,
                                         delete_directory,
                                         move_file,
                                         set_end_of_file,
                                         set_allocation_size,
                                         lock_file,
                                         unlock_file,
                                         get_disk_free_space,
                                         get_volume_information,
                                         mounted,
                                         unmounted,
                                         get_file_security,
                                         set_file_security,
                                         find_streams };
}