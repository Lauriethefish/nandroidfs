#pragma once

#include <string_view>
#include <mutex>
#include <functional>

#include "dokan_no_winsock.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

#include "serialization.hpp"
#include "responses.hpp"
#include "TimedCache.hpp"
#include "Logger.hpp"

#define BUFFER_SIZE 4096

namespace nandroidfs {
	const ms_duration STAT_CACHE_PERIOD = std::chrono::milliseconds(200);
	const ms_duration STAT_SCAN_PERIOD = std::chrono::milliseconds(5000);

	class Connection : Readable, Writable {
	public:
		// Creates a new instance of the Connection class
		// This will establish a TCP connection to the server with the given address and port.
		// It will also carry out a handshake to ensure the connection is working
		Connection(std::string address, uint16_t port, ContextLogger& parent_logger);
		~Connection();

		// Requests to stat a singular file.
		ResponseStatus req_stat_file(LPCWSTR path, FileStat& out_file_stat);
		// Requests to list the stats for all files in a directory.
		ResponseStatus req_list_file_stats(LPCWSTR path,
			std::function<void(FileStat stat, std::wstring file_name)> consume_stat);
		// Requests to move a file or directory
		ResponseStatus req_move_entry(LPCWSTR from_path, LPCWSTR to_path, bool replace_if_exists);
		// Requests to remove a file
		ResponseStatus req_remove_file(LPCWSTR path);
		// Checks if it is possible to remove the file at the given path
		ResponseStatus req_can_remove_file(LPCWSTR path);
		// Requests to remove a directory
		ResponseStatus req_remove_directory(LPCWSTR path);
		// Checks if it is possible to remove the directory at the given path.
		ResponseStatus req_can_remove_directory(LPCWSTR path);
		// Requests to create a directory.
		ResponseStatus req_create_directory(LPCWSTR path);
		// Requests to open a file.
		// If successful, the file descriptor is written to out_file_handle.
		ResponseStatus req_open_file(LPCWSTR path,
			OpenMode mode,
			bool read_access,
			bool write_access,
			FILE_HANDLE& out_file_handle);
		// Closes the provided file handle.
		ResponseStatus req_close_file(FILE_HANDLE handle);
		// Requests to write to a file.
		// Will always write the full length of data requested.
		ResponseStatus req_write_to_file(FILE_HANDLE file_handle, uint64_t file_offset, const uint8_t* data, uint32_t data_len);
		// Requests to read from a file.
		// Will always read the full length of data requested, 
		// unless the status is not `ResponseStatus::Success` or EOF is reached.
		// `bytes_read` will be overwritten with the number of bytes successfully read.
		ResponseStatus req_read_from_file(FILE_HANDLE file_handle, uint64_t file_offset, uint8_t* buffer, uint32_t buffer_len, int& bytes_read);
		// Requests to set the length of a file.
		// This will extend the file with null bytes if the given length is more than the current file length.
		// The file handle must be writable.
		ResponseStatus req_set_file_len(FILE_HANDLE file_handle, uint64_t file_len);
		// Requests to set the access and modification times of a file.
		ResponseStatus req_set_file_time(LPCWSTR path, uint64_t access_time, uint64_t write_time);
		// Requests to get the number of free/available/total bytes on the filesystem.
		// If successful, this is saved to out_disk_stats
		ResponseStatus req_get_disk_stats(DiskStats& out_disk_stats);
	private:
		ContextLogger logger;
		SOCKET conn_sock;
		DataWriter writer;
		DataReader reader;
		std::mutex request_mutex;
		std::thread data_log_thread;

		TimedCache<FileStat> stat_cache;
		// Cache of the entry names of the entries in directories.
		// Does NOT include the full entry path to save memory. Does NOT include the stat as that is kept separately in the stat cache.
		TimedCache<std::vector<std::string>> dir_list_cache;

#ifdef _DEBUG
		int data_written = 0;
		int data_read = 0;
		// Entry point for a thread that logs the quantity of data being written by this connection each second.
		void data_log_entry_point();
		bool kill_data_log = false;
#endif
		bool try_use_cached_dir_listing(std::string& unix_dir_path, std::function<void(FileStat stat, std::wstring file_name)> consume_stat);
		// Invalidates the cached directory listing for the parent of `path`.
		// This does nothing if `path` has no parent.
		void invalidate_parent_dir(std::string& path);

		void handshake();

		virtual int read(uint8_t* buffer, int length);
		virtual void write(const uint8_t* buffer, int length);
	};
}