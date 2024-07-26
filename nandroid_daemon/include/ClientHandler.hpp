#pragma once

#include "serialization.hpp"
#include <vector>

#define BUFFER_SIZE 4096

namespace nandroidfs {

    class ClientHandler : Readable, Writable {
    private:
        int socket;
        DataReader reader;
        DataWriter writer;
        // All data read from a file is temporarily stored in this buffer.
        // We need to read into a buffer first so that we know the length of the data read, which needs to be supplied before the response.
        // Data read from the socket to be written to a file is also saved here.
        // This could be skipped to avoid a buffering step - TODO
        std::vector<uint8_t> rw_buffer;

        // Ensures that the read/write buffer length is at least the specified length (in bytes)
        void ensure_rw_buffer_size(size_t size);

        virtual int read(uint8_t* buffer, int length);
        virtual void write(const uint8_t* buffer, int length);

        void handle_open_handle();
        void handle_create_directory();
        void handle_list_dir_stats();
        void handle_move_entry();
        void handle_check_remove_file();
        void handle_check_remove_directory();
        void handle_remove_file();
        void handle_remove_directory();
        void handle_read_file();
        void handle_write_file();
        void handle_truncate_file();
        void handle_set_file_time();
        void handle_get_disk_stats();
    public:
        // Carries out a handshake to ensure the connection is working.
        ClientHandler(int socket);
        ~ClientHandler();

        // Continually handles messages and sends responses through the socket until EOF on read is reached.
        void handle_messages();
    };
}