#include "Connection.hpp"
#include "WinSockException.hpp"
#include "path_utils.hpp"

#include "conversion.hpp"

#include <iostream>

namespace nandroidfs {
	Connection::Connection(std::string address, uint16_t port) 
		: writer(this, BUFFER_SIZE), 
		reader(this, BUFFER_SIZE),
		stat_cache(STAT_SCAN_PERIOD, STAT_CACHE_PERIOD),
		dir_list_cache(STAT_SCAN_PERIOD, STAT_CACHE_PERIOD) {
		WSADATA wsa_data;
		throw_if_nonzero(WSAStartup(MAKEWORD(2, 2), &wsa_data));
		try {
			struct addrinfo* result,
				hints;

			ZeroMemory(&hints, sizeof(hints));
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			// Fetch all addresses matching the address/port specified.
			std::string port_string = std::to_string(port);
			throw_if_nonzero(getaddrinfo(address.c_str(), port_string.c_str(), &hints, &result));

			// Iterate through the provided linked list of addresses until reaching a nullptr.
			SOCKET connect_socket = INVALID_SOCKET;
			for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
				std::cout << "Trying connection " << ptr->ai_family << " " << ptr->ai_socktype << " " << ptr->ai_protocol << std::endl;
				connect_socket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
				if (connect_socket == INVALID_SOCKET) {
					freeaddrinfo(result);
					throw WinSockException(WSAGetLastError());
				}

				int result = connect(connect_socket, ptr->ai_addr, (int)ptr->ai_addrlen);
				if (result == -1) {
					closesocket(connect_socket);
					continue;
				}

				// Successfully connected; break
				break;
			}

			freeaddrinfo(result);
			// No possible addresses connected successfully
			if (connect_socket == INVALID_SOCKET) {
				throw std::runtime_error("Unable to connect to daemon; none of the supplied addresses led to a successful connection");
			}

			conn_sock = connect_socket;
			this->handshake();
#ifdef _DEBUG
			data_log_thread = std::thread(&Connection::data_log_entry_point, this);
#endif
		}
		catch (std::exception&) {
			// Ensure we release resources before propogating any exceptions.
			WSACleanup();
			throw;
		}
	}

	void Connection::handshake() {
		const uint32_t HANDSHAKE_DATA = 0xFAFE5ABE;
		writer.write_u32(HANDSHAKE_DATA);
		writer.flush();

		uint32_t received_data = reader.read_u32();
		if (received_data == HANDSHAKE_DATA) {
			std::cout << "Handshake succeeded" << std::endl;
		}
		else
		{
			throw std::runtime_error("Failed handshake! Did not receive same bytes that were sent");
		}
	}

	int Connection::read(uint8_t* buffer, int length) {
		int result = recv(conn_sock, reinterpret_cast<char*>(buffer), length, 0);
		if (result == -1) {
			throw WinSockException(WSAGetLastError());
		}
		else
		{
			// Number of bytes read
#ifdef _DEBUG
			data_read += result;
#endif
			return result;
		}
	}

	void Connection::write(const uint8_t* buffer, int length) {
		while (length > 0) {
			int result = send(conn_sock, reinterpret_cast<const char*>(buffer), length, 0);
			if (result == -1) {
				throw WinSockException(WSAGetLastError());
			}

			length -= result;
			buffer += result;
#ifdef _DEBUG
			data_written += result;
#endif
		}
	}

	ResponseStatus Connection::req_stat_file(LPCWSTR path, FileStat& out_file_stat) {
		std::string unix_path = win32_path_to_unix(path);

		// Attempt to use a cached version of the stat.
		auto cached_stat = stat_cache.get_cached(unix_path);
		if (cached_stat.has_value()) {
			out_file_stat = *cached_stat;
			return ResponseStatus::Success;
		}

		std::lock_guard guard(request_mutex);

		// Now that we've waited to lock the mutex, it's possible that somebody else statted and cached the stat
		// for this path in the meanwhile, so we will check for a stat again.
		cached_stat = stat_cache.get_cached(unix_path);
		if (cached_stat.has_value()) {
			out_file_stat = *cached_stat;
			return ResponseStatus::Success;
		}

		// Skip any requests for desktop.ini.
		// Windows requests this about 6000 times each time you click a directory in file explorer.
		// If somebody actually needs a file with this name, they can complain to me later.
		if (unix_path.ends_with("desktop.ini")) {
			return ResponseStatus::FileNotFound;
		}

		writer.write_byte((uint8_t) RequestType::StatFile);
		writer.write_utf8_string(unix_path);
		writer.flush();

		ResponseStatus status = (ResponseStatus) reader.read_byte();
		if (status != ResponseStatus::Success) {
			return status;
		}

		out_file_stat = FileStat(reader);
		// Cache the stat for future calls
		stat_cache.cache(unix_path, out_file_stat);

		return ResponseStatus::Success;
	}

	ResponseStatus Connection::req_list_file_stats(LPCWSTR path, std::function<void(FileStat stat, std::wstring file_name)> consume_stat) {
		std::string unix_dir_path = win32_path_to_unix(path);
		if (try_use_cached_dir_listing(unix_dir_path, consume_stat)) {
			return ResponseStatus::Success;
		}
		
		std::lock_guard guard(request_mutex);
		writer.write_byte((uint8_t) RequestType::ListDirectory);
		writer.write_utf8_string(win32_path_to_unix(path));
		writer.flush();

		ResponseStatus status = (ResponseStatus) reader.read_byte();
		if (status != ResponseStatus::Success) {
			return status;
		}

		std::vector<std::string> entries;
		ResponseStatus entry_status;
		while((entry_status = (ResponseStatus) reader.read_byte()) != ResponseStatus::NoMoreEntries)
		{
			if (entry_status == ResponseStatus::Success) {
				std::string file_name = reader.read_utf8_string();
				FileStat entry_stat(reader);
				std::string full_entry_path = get_full_path(unix_dir_path, file_name);

				stat_cache.cache(full_entry_path, entry_stat);
				entries.push_back(file_name);

				consume_stat(entry_stat, unix_path_to_win32(file_name));
			}
			// TODO: Right now we're skipping files with AccessDenied, maybe in the future we can show these files in some way?
			// We know the filename, but we have no clue if they're files or directories.
		}
		dir_list_cache.cache(unix_dir_path, entries);

		return ResponseStatus::Success;
	}

	bool Connection::try_use_cached_dir_listing(std::string& unix_dir_path, std::function<void(FileStat stat, std::wstring file_name)> consume_stat) {
		auto opt_cached_dir = dir_list_cache.get_cached(unix_dir_path);
		if (!opt_cached_dir.has_value()) {
			return false;
		}

		std::vector<std::string> cached_dir = *opt_cached_dir;
		for (int i = 0; i < cached_dir.size(); i++) {
			std::string full_path = get_full_path(unix_dir_path, cached_dir[i]);

			std::optional<FileStat> cached = stat_cache.get_cached(full_path);
			if (cached.has_value()) {
				consume_stat(*cached, unix_path_to_win32(cached_dir[i]));
			}
			else
			{
				// TODO: Handle this case. e.g. fetch the stat with a separate call.
				// Right now the file will just be skipped.
			}
		}

		return true;
	}

	ResponseStatus Connection::req_move_entry(LPCWSTR from_path, LPCWSTR to_path, bool replace_if_exists) {
		std::lock_guard guard(request_mutex);

		std::string unix_from_path = win32_path_to_unix(from_path);
		std::string unix_to_path = win32_path_to_unix(to_path);

		stat_cache.invalidate(unix_from_path);
		invalidate_parent_dir(unix_from_path);
		stat_cache.invalidate(unix_to_path);
		invalidate_parent_dir(unix_to_path);

		writer.write_byte((uint8_t)RequestType::MoveEntry);
		MoveEntryArgs args(unix_from_path, unix_to_path, replace_if_exists);
		args.write(writer);
		writer.flush();

		return (ResponseStatus)reader.read_byte();
	}

	ResponseStatus Connection::req_remove_file(LPCWSTR path) {
		std::lock_guard guard(request_mutex);

		std::string unix_path = win32_path_to_unix(path);
		stat_cache.invalidate(unix_path);
		invalidate_parent_dir(unix_path);

		writer.write_byte((uint8_t)RequestType::RemoveFile);
		writer.write_utf8_string(unix_path);
		writer.flush();

		return (ResponseStatus) reader.read_byte();
	}

	ResponseStatus Connection::req_can_remove_file(LPCWSTR path) {
		std::lock_guard guard(request_mutex);

		writer.write_byte((uint8_t)RequestType::CheckRemoveFile);
		writer.write_utf8_string(win32_path_to_unix(path));
		writer.flush();

		return (ResponseStatus)reader.read_byte();
	}

	ResponseStatus Connection::req_remove_directory(LPCWSTR path) {
		std::lock_guard guard(request_mutex);

		std::string unix_path = win32_path_to_unix(path);
		stat_cache.invalidate(unix_path);
		invalidate_parent_dir(unix_path);

		writer.write_byte((uint8_t)RequestType::RemoveDirectory);
		writer.write_utf8_string(unix_path);
		writer.flush();

		return (ResponseStatus)reader.read_byte();
	}

	ResponseStatus Connection::req_can_remove_directory(LPCWSTR path) {
		std::lock_guard guard(request_mutex);

		writer.write_byte((uint8_t)RequestType::CheckRemoveDirectory);
		writer.write_utf8_string(win32_path_to_unix(path));
		writer.flush();

		return (ResponseStatus)reader.read_byte();
	}

	ResponseStatus Connection::req_create_directory(LPCWSTR path) {
		std::lock_guard guard(request_mutex);

		std::string unix_path = win32_path_to_unix(path);
		stat_cache.invalidate(unix_path);
		invalidate_parent_dir(unix_path);

		writer.write_byte((uint8_t)RequestType::CreateDirectory);
		writer.write_utf8_string(unix_path);
		writer.flush();

		return (ResponseStatus)reader.read_byte();
	}

	ResponseStatus Connection::req_open_file(LPCWSTR path,
		OpenMode mode,
		bool read_access,
		bool write_access,
		FILE_HANDLE& out_file_handle) {
		std::lock_guard guard(request_mutex);

		std::string unix_path = win32_path_to_unix(path);
		// Fail anything to do with desktop.ini, just to stop windows spamming requests for this constantly.
		if (unix_path.ends_with("desktop.ini")) {
			return ResponseStatus::GenericFailure;
		}
		
		writer.write_byte((uint8_t)RequestType::OpenHandle);
		OpenHandleArgs args(unix_path, mode, read_access, write_access);
		args.write(writer);
		writer.flush();

		ResponseStatus status = (ResponseStatus) reader.read_byte();
		if (status == ResponseStatus::Success) {
			out_file_handle = reader.read_u32();
		}

		switch (mode) {
			case OpenMode::CreateAlways:
			case OpenMode::CreateIfNotExist:
			case OpenMode::CreateOrTruncate:
				invalidate_parent_dir(unix_path);
		}

		return status;
	}

	ResponseStatus Connection::req_close_file(FILE_HANDLE file_handle) {
		std::lock_guard guard(request_mutex);

		writer.write_byte((uint8_t)RequestType::CloseHandle);
		writer.write_u32(file_handle);
		writer.flush();
		return (ResponseStatus)reader.read_byte();
	}

	ResponseStatus Connection::req_write_to_file(FILE_HANDLE file_handle,
		uint64_t file_offset,
		const uint8_t* data,
		uint32_t data_len) {
		std::lock_guard guard(request_mutex);

		// Write the request header and data to be written.
		writer.write_byte((uint8_t)RequestType::WriteHandle);
		WriteHandleInitArgs args(file_handle, file_offset, data_len);
		args.write(writer);
		writer.write_exact(data, data_len);

		writer.flush();

		return (ResponseStatus)reader.read_byte();
	}

	ResponseStatus Connection::req_read_from_file(FILE_HANDLE file_handle,
		uint64_t file_offset,
		uint8_t* buffer,
		uint32_t buffer_len,
		int& bytes_read) {
		std::lock_guard guard(request_mutex);

		// Write the request header and data to be written.
		writer.write_byte((uint8_t)RequestType::ReadHandle);
		ReadHandleArgs args(file_handle, buffer_len, file_offset);
		args.write(writer);
		writer.flush();

		ResponseStatus status = (ResponseStatus)reader.read_byte();
		if (status == ResponseStatus::Success) {
			bytes_read = reader.read_u32();
			reader.read_exact(buffer, bytes_read);
		}

		return status;
	}
	
	ResponseStatus Connection::req_set_file_len(FILE_HANDLE file_handle, uint64_t file_len) {
		std::lock_guard guard(request_mutex);
		
		writer.write_byte((uint8_t)RequestType::TruncateHandle);
		TruncateHandleArgs args(file_handle, file_len);

		args.write(writer);
		writer.flush();

		return (ResponseStatus)reader.read_byte();
	}

	ResponseStatus Connection::req_set_file_time(LPCWSTR path, uint64_t access_time, uint64_t write_time) {
		std::lock_guard guard(request_mutex);

		std::string unix_path = win32_path_to_unix(path);
		stat_cache.invalidate(unix_path);

		writer.write_byte((uint8_t)RequestType::SetFileTime);

		SetFileTimeArgs args(unix_path, access_time, write_time);
		args.write(writer);
		writer.flush();

		return (ResponseStatus)reader.read_byte();
	}

	ResponseStatus Connection::req_get_disk_stats(DiskStats& out_disk_stats) {
		std::lock_guard guard(request_mutex);

		writer.write_byte((uint8_t)RequestType::GetDiskStats);
		writer.flush();

		ResponseStatus status = (ResponseStatus)reader.read_byte();
		if (status == ResponseStatus::Success) {
			DiskStats stats(reader);
			out_disk_stats = stats;
		}

		return status;
	}

	void Connection::invalidate_parent_dir(std::string& path) {
		std::optional<std::string> parent = get_parent_path(path);
		if (parent.has_value()) {
			dir_list_cache.invalidate(*parent);
		}
	}

#ifdef _DEBUG
	void Connection::data_log_entry_point() {
		while (true) {
			{
				std::lock_guard guard(request_mutex);

				if (kill_data_log) {
					return; // Stop thread if requested.
				}

				int written_kb = data_written >> 10;
				int read_kb = data_read >> 10;

				std::cout << "Wrote " << written_kb << " read " << read_kb << " KiB" << std::endl;
				data_written = 0;
				data_read = 0;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
	}
#endif

	Connection::~Connection() {
		// Tell the data log thread to stop.
#ifdef _DEBUG
		request_mutex.lock();
		kill_data_log = true;
		request_mutex.unlock();

		data_log_thread.join();

		std::cout << "Stat cache::" << stat_cache.get_cache_statistics() << std::endl;
		std::cout << "Dir listing cache::" << dir_list_cache.get_cache_statistics() << std::endl;
#endif

		closesocket(conn_sock);
		WSACleanup();
	}
}