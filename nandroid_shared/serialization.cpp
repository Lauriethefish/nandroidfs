#ifdef __ANDROID__
#include <arpa/inet.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#undef min
#else
// No htonll/ntohll function without winsock2.h so we need to implement it ourselves.
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
uint64_t htonll(uint64_t v) {
    // Swap around the first 4 and the last 4 bytes.
    // Call htonl on each to swap them from host to network (or network to host) byte order.
    return (static_cast<uint64_t>(htonl(v)) << 32) | static_cast<uint64_t>(htonl(v >> 32));
}
uint64_t ntohll(uint64_t v) {
    return htonll(v);
}
#else
uint64_t htonll(uint64_t v) {
    return v;
}
uint64_t ntohll(uint64_t v) {
    return v;
}
#endif

#endif

#include "serialization.hpp"
#include <stdexcept>
#include <cmath>
#include <string_view>
#include <iostream>

namespace nandroidfs 
{
    EOFException::EOFException() : std::runtime_error("EOF reached while reading from stream") {}

    DataReader::DataReader(Readable* stream, int buffer_size) 
    {
        this->stream = stream;
        this->buffer_size = buffer_size;
        this->buffer = new uint8_t[buffer_size];
    }

    DataReader::~DataReader()
    {
        delete[] this->buffer;
    }

    void DataReader::read_exact(uint8_t* into, int num_bytes) {
        while(num_bytes > 0) {
            // Work out how many bytes we can read with the current buffer
            int rem_bytes_in_buffer = read_into_buffer - position_in_buffer;
            int bytes_to_copy = std::min(num_bytes, rem_bytes_in_buffer);

            // Copy remaining data from the internal buffer into the destination buffer.
            memcpy(into, buffer + position_in_buffer, bytes_to_copy);
            num_bytes -= bytes_to_copy;
            into += bytes_to_copy; // Advance the pointer by the number of bytes copied.
            position_in_buffer += bytes_to_copy;

            // If the number of bytes left to read is more than the buffer size, we can skip the buffer altogether and read directly into the destination
            int bytes_read = 1;
            while (num_bytes >= buffer_size && bytes_read > 0) {
                bytes_read = stream->read(into, num_bytes);
                into += bytes_read;
                num_bytes -= bytes_read;
            }

            // Once the number of bytes left is smaller than the buffer size, we should buffer this data again.
            if (num_bytes > 0) {
                // Any remaining bytes should be buffered to reduce syscalls.
                bytes_read = stream->read(buffer, buffer_size);
                if (bytes_read == 0) {
                    throw EOFException();
                }

                else {
                    read_into_buffer = bytes_read;
                    position_in_buffer = 0;
                }
            }
        }
    }

    uint8_t DataReader::read_byte() {
        uint8_t result;
        read_exact(&result, 1);
        return result;
    }

    uint16_t DataReader::read_u16() {
        uint16_t result;
        read_exact(reinterpret_cast<uint8_t*>(&result), 2);
        return ntohs(result);
    }

    uint32_t DataReader::read_u32() {
        uint32_t result;
        read_exact(reinterpret_cast<uint8_t*>(&result), 4);
        return ntohl(result);
    }

    uint64_t DataReader::read_u64() {
        uint64_t result;
        read_exact(reinterpret_cast<uint8_t*>(&result), 8);
        return ntohll(result);
    }

    std::string DataReader::read_utf8_string() {
        uint16_t length = read_u16();

        char* buffer = new char[length];
        read_exact(reinterpret_cast<uint8_t*>(buffer), length);
        std::string result(buffer, length);
        delete[] buffer;

        return result;
    }

    DataWriter::DataWriter(Writable* stream, int buffer_size) {
        this->stream = stream;
        this->buffer_size = buffer_size;
        this->buffer = new uint8_t[buffer_size];
    }

    DataWriter::~DataWriter() {
        delete[] buffer;
    }

    void DataWriter::flush() {
        stream->write(buffer, buffer_bytes_filled);
        buffer_bytes_filled = 0;
    }

    void DataWriter::write_exact(const uint8_t* data_ptr, int data_len) {
        while(data_len > 0) {
            // Copy as much data as we can in this iteration
            int rem_buffer_len = buffer_size - buffer_bytes_filled;
            int len_to_write = std::min(data_len, rem_buffer_len);

            // If we're aiming to write a full buffer, there's no need to copy into the buffer from data_ptr before writing.
            // We can just do the write operation directly.
            if(len_to_write == buffer_size) {
                stream->write(data_ptr, data_len);
                return;
            }   else    {
                std::memcpy(buffer + buffer_bytes_filled, data_ptr, len_to_write);

                // Advance the pointer to the data after what we have just written
                // and ensure the length written is properly reflected.
                data_ptr += len_to_write;
                data_len -= len_to_write;
                buffer_bytes_filled += len_to_write;
            }

            if(data_len > 0) {
                // If still more data to write, the buffer did not have capacity to store all the data.
                flush(); // Flush the buffer to make space for more data.
            }
        }
    }

    void DataWriter::write_byte(uint8_t data) {
        write_exact(&data, 1);
    }

    void DataWriter::write_u16(uint16_t data) {
        uint16_t net_data = htons(data);
        write_exact(reinterpret_cast<uint8_t*>(&net_data), 2);
    }

    void DataWriter::write_u32(uint32_t data) {
        uint32_t net_data = htonl(data);
        write_exact(reinterpret_cast<uint8_t*>(&net_data), 4);
    }

    void DataWriter::write_u64(uint64_t data) {
        uint64_t net_data = htonll(data);
        write_exact(reinterpret_cast<uint8_t*>(&net_data), 8);
    }

    void DataWriter::write_utf8_string(std::string_view data) {
        if(data.length() > UINT16_MAX) {
            throw std::runtime_error("String too long to write to stream. Was path length properly limited?");
        }

        write_u16(static_cast<uint16_t>(data.length()));
        write_exact(reinterpret_cast<const uint8_t*>(data.data()), static_cast<int>(data.length()));
    }
}