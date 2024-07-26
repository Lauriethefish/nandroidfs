#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <stdexcept>

namespace nandroidfs
{
    // Thrown if EOF is reached when reading in DataReader.
    // Not thrown by Readable.
    class EOFException : public std::runtime_error {
    public:
        EOFException();
    };

    class Readable 
    {
    public:
        // Reads bytes from the underlying stream into the given buffer, with length `length`.
        // Returns the number of bytes read, or 0 if EOF.
        virtual int read(uint8_t* buffer, int length) = 0;
    };

    class Writable {
    public:
        // Writes bytes from the given buffer to the underlying stream.
        // Will always write the complete length of the buffer provided.
        virtual void write(const uint8_t* buffer, int length) = 0;
    };

    // A buffered reader for primitive data types.
    class DataReader 
    {
    private:
        Readable* stream;

        uint8_t* buffer;
        int buffer_size; // Total length of buffer in memory.
        int read_into_buffer = 0; // The number of bytes that were last read into the buffer. 
        int position_in_buffer = 0; // The position of the next byte to read within the buffer.

    public:
        DataReader(Readable* stream, int buffer_size);
        ~DataReader();

        // Reads exactly the specified number of bytes into the pointer at into.
        void read_exact(uint8_t* into, int num_bytes);
        
        uint8_t read_byte();
        uint16_t read_u16();
        uint32_t read_u32();
        uint64_t read_u64();

        // Reads a string, prefixed with a 2 byte length.
        std::string read_utf8_string();
    };

    // A buffered writer for primitive data types.
    class DataWriter 
    {
    private:
        Writable* stream;

        uint8_t* buffer;
        int buffer_size;
        int buffer_bytes_filled = 0;

    public:
        void write_exact(const uint8_t* data_ptr, int data_len);

        DataWriter(Writable* stream, int buffer_size);
        ~DataWriter();

        // Writes any data pending to be written in the buffer.
        void flush();

        void write_byte(uint8_t data);
        void write_u16(uint16_t data);
        void write_u32(uint32_t data);
        void write_u64(uint64_t data);
        void write_utf8_string(std::string_view data);
    };
}