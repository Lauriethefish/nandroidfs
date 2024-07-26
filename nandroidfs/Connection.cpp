#include "Connection.hpp"
#include "WinSockException.hpp"

#include "conversion.hpp"

#include <iostream>

namespace nandroidfs {
	Connection::Connection(std::string address, uint16_t port) 
		: writer(DataWriter(this, BUFFER_SIZE)), reader(DataReader(this, BUFFER_SIZE)) {
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
		std::lock_guard guard(request_mutex);

		std::string unix_path = win32_path_to_unix(path);
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
		return ResponseStatus::Success;
	}

	ResponseStatus Connection::req_list_file_stats(LPCWSTR path, std::function<void(FileStat stat, std::wstring file_name)> consume_stat) {
		std::lock_guard guard(request_mutex);

		writer.write_byte((uint8_t) RequestType::ListDirectory);
		writer.write_utf8_string(win32_path_to_unix(path));
		writer.flush();

		ResponseStatus status = (ResponseStatus) reader.read_byte();
		if (status != ResponseStatus::Success) {
			return status;
		}

		ResponseStatus entry_status;
		while((entry_status = (ResponseStatus) reader.read_byte()) != ResponseStatus::NoMoreEntries)
		{
			if (entry_status == ResponseStatus::Success) {
				std::string file_name = reader.read_utf8_string();
				consume_stat(FileStat(reader), unix_path_to_win32(file_name));
			}
			// TODO: Right now we're skipping files with AccessDenied, maybe in the future we can show these files in some way?
			// We know the filename, but we have no clue if they're files or directories.
		}

		return ResponseStatus::Success;
	}

	ResponseStatus Connection::req_move_entry(LPCWSTR from_path, LPCWSTR to_path, bool replace_if_exists) {
		std::lock_guard guard(request_mutex);

		writer.write_byte((uint8_t)RequestType::MoveEntry);
		MoveEntryArgs args(win32_path_to_unix(from_path), win32_path_to_unix(to_path), replace_if_exists);
		args.write(writer);
		writer.flush();

		return (ResponseStatus)reader.read_byte();
	}

	ResponseStatus Connection::req_remove_file(LPCWSTR path) {
		std::lock_guard guard(request_mutex);

		writer.write_byte((uint8_t)RequestType::RemoveFile);
		writer.write_utf8_string(win32_path_to_unix(path));
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

		writer.write_byte((uint8_t)RequestType::RemoveDirectory);
		writer.write_utf8_string(win32_path_to_unix(path));
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

		writer.write_byte((uint8_t)RequestType::CreateDirectory);
		writer.write_utf8_string(win32_path_to_unix(path));
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

		writer.write_byte((uint8_t)RequestType::SetFileTime);

		SetFileTimeArgs args(win32_path_to_unix(path), access_time, write_time);
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
#endif

		closesocket(conn_sock);
		WSACleanup();
	}
}