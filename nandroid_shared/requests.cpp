#include "requests.hpp"
#include <iostream>

namespace nandroidfs {
    OpenHandleArgs::OpenHandleArgs(DataReader& reader) {
        path = reader.read_utf8_string();
        // MSB is whether handle needs read access
        // Next MSB is whether handle needs write access
        // Next MSB is whether to delete the file on close.
        // Lower bits are the OpenMode
        uint8_t mode_and_perms = reader.read_byte();

        read_access = mode_and_perms & 0b10000000;
        write_access = mode_and_perms & 0b01000000;
        mode = (OpenMode) (mode_and_perms & 0b00111111);
    }

    OpenHandleArgs::OpenHandleArgs(std::string path, OpenMode mode, bool read_access, bool write_access) {
        this->path = path;
        this->mode = mode;
        this->read_access = read_access;
        this->write_access = write_access;
    }

    void OpenHandleArgs::write(DataWriter& writer) {
        writer.write_utf8_string(path);
        uint8_t mode_and_perms = (uint8_t) mode;
        mode_and_perms |= read_access ? 0b10000000 : 0;
        mode_and_perms |= write_access ? 0b01000000 : 0;

        writer.write_byte(mode_and_perms);
    }

    ReadHandleArgs::ReadHandleArgs(DataReader& reader) {
        handle = reader.read_u32();
        data_len = reader.read_u32();
        offset = reader.read_u64();
    }

    ReadHandleArgs::ReadHandleArgs(FILE_HANDLE handle, uint32_t data_len, uint64_t offset) {
        this->handle = handle;
        this->data_len = data_len;
        this->offset = offset;
    }

    void ReadHandleArgs::write(DataWriter& writer) {
        writer.write_u32(handle);
        writer.write_u32(data_len);
        writer.write_u64(offset);
    }

    WriteHandleInitArgs::WriteHandleInitArgs(DataReader& reader) {
        handle = reader.read_u32();
        offset = reader.read_u64();
        data_len = reader.read_u32();
    }
    
    WriteHandleInitArgs::WriteHandleInitArgs(FILE_HANDLE handle, uint64_t offset, uint32_t data_len) {
        this->handle = handle;
        this->offset = offset;
        this->data_len = data_len;
    }

    void WriteHandleInitArgs::write(DataWriter& writer) {
        writer.write_u32(handle);
        writer.write_u64(offset);
        writer.write_u32(data_len);
    }

    MoveEntryArgs::MoveEntryArgs(DataReader& reader) {
        from_path = reader.read_utf8_string();
        to_path = reader.read_utf8_string();
        overwrite = static_cast<bool>(reader.read_byte());
    }

    MoveEntryArgs::MoveEntryArgs(std::string from_path, std::string to_path, bool overwrite) {
        this->from_path = from_path;
        this->to_path = to_path;
        this->overwrite = overwrite;
    }

    void MoveEntryArgs::write(DataWriter& writer) {
        writer.write_utf8_string(from_path);
        writer.write_utf8_string(to_path);
        writer.write_byte(overwrite);
    }

    TruncateHandleArgs::TruncateHandleArgs(DataReader& reader) {
        handle = reader.read_u32();
        new_length = reader.read_u64();
    }

    TruncateHandleArgs::TruncateHandleArgs(FILE_HANDLE handle, uint64_t new_length) {
        this->handle = handle;
        this->new_length = new_length;
    }

    void TruncateHandleArgs::write(DataWriter& writer) {
        writer.write_u32(handle);
        writer.write_u64(new_length);
    }

    SetFileTimeArgs::SetFileTimeArgs(DataReader& reader) {
        path = reader.read_utf8_string();
        access_time = reader.read_u64();
        write_time = reader.read_u64();
    }

    SetFileTimeArgs::SetFileTimeArgs(std::string path, int64_t access_time, int64_t write_time) {
        this->path = path;
        this->access_time = access_time;
        this->write_time = write_time;
    }

    void SetFileTimeArgs::write(DataWriter& writer) {
        writer.write_utf8_string(path);
        writer.write_u64(access_time);
        writer.write_u64(write_time);
    }
}