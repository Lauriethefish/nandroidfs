#include "Adb.hpp"
#include <format>
#include <stdexcept>
#include <tchar.h>

namespace nandroidfs {
	void create_stdouterr_pipes(HANDLE* child_stdout_write, HANDLE* child_stdout_read) {
		// Allow pipe handles to be inherited.
		SECURITY_ATTRIBUTES sec_attrs;
		sec_attrs.nLength = sizeof(SECURITY_ATTRIBUTES);
		sec_attrs.bInheritHandle = true;
		sec_attrs.lpSecurityDescriptor = NULL;

		// Prepare the pipe for reading the stdout of the process.
		if (!CreatePipe(child_stdout_read, child_stdout_write, &sec_attrs, 0)) {
			throw std::runtime_error("Failed to CreatePipe for reading stdout");
		}

		if (!SetHandleInformation(*child_stdout_read, HANDLE_FLAG_INHERIT, 0)) {
			throw std::runtime_error("Failed to SetHandleInformation on pipe");
		}
	}

	PROCESS_INFORMATION create_process(HANDLE child_stdout_write, LPCWSTR command_line) {
		// Set options needed to start the process.
		PROCESS_INFORMATION process_info;
		STARTUPINFO start_info;
		ZeroMemory(&process_info, sizeof(PROCESS_INFORMATION));
		ZeroMemory(&start_info, sizeof(STARTUPINFO));
		start_info.cb = sizeof(STARTUPINFO);
		start_info.hStdError = child_stdout_write;
		start_info.hStdOutput = child_stdout_write;
		// ...TODO: Allow supplying a source for stdin?

		// Create a clone of the arguments that can be written to, to avoid an access violation.
		int args_len = lstrlenW(command_line);
		LPWSTR nonconst_args = new WCHAR[args_len + 1];
		lstrcpyW(nonconst_args, command_line);


		start_info.dwFlags |= STARTF_USESTDHANDLES;
		bool success = CreateProcess(nullptr,
			nonconst_args,
			nullptr, // No extra security attributes
			nullptr,
			true, // Inherit handles.
			0,
			nullptr, // Same environment as this process
			nullptr, // Same CWD as this process.
			&start_info,
			&process_info); // output PROCESS_INFORMATION
		delete[] nonconst_args;

		if (!success) {
			throw std::runtime_error("Failure in CreateProcess");
		}

		return process_info;
	}

	void invoke_process_capture_output(LPCWSTR command_line, int& out_exit_code,
		OutputCapture consume_output) {
		// Create a pipe for the process stdout and stderr.
		HANDLE child_stdout_read, child_stdout_write;
		create_stdouterr_pipes(&child_stdout_write, &child_stdout_read);

		// Now ready to actually create the process.
		PROCESS_INFORMATION process_info = create_process(child_stdout_write, command_line);
		CloseHandle(child_stdout_write); // Close the write handle to the child's stdout FROM THIS PROCESS
		// This means that the ReadFile call will EOF when the child process exits.

		// Read data from the process until EOF.
		CHAR* buffer = new CHAR[STDOUT_READ_BUF_SIZE];
		DWORD bytes_read;

		while (true) {
			bool read_success = ReadFile(child_stdout_read, buffer, STDOUT_READ_BUF_SIZE, &bytes_read, nullptr);
			if (!read_success || bytes_read == 0) {
				break; // Reached EOF.
			}

			consume_output(reinterpret_cast<uint8_t*>(buffer), bytes_read);
		}
		delete[] buffer;

		// Wait for process to exit and release resources.
		WaitForSingleObject(process_info.hProcess, INFINITE);
		CloseHandle(child_stdout_read);
		CloseHandle(process_info.hThread);

		DWORD exit_code;
		if (!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
			throw std::runtime_error("Failed to get exit code");
		}
		out_exit_code = exit_code;

		CloseHandle(process_info.hProcess);
	}

	std::string invoke_process_capture_output_string(LPCWSTR command_line, int& out_exit_code) {
		std::string output;

		invoke_process_capture_output(command_line, out_exit_code, [&output](uint8_t* buffer, int length) {
			output.append(reinterpret_cast<const char*>(buffer), length);
		});

		return output;
	}


	std::string invoke_throw_on_nonzero(LPCWSTR command_line) {
		int exit_code;
		std::string output = invoke_process_capture_output_string(command_line, exit_code);
		if (exit_code == 0) {
			return output;
		}
		else
		{
			throw std::runtime_error(std::format("Got nonzero exit code from ADB: {}, stdout/stderr: {}", exit_code, output));
		}
	}

	const LPCWSTR ADB_PATH = L"adb"; // TODO: Allow user of driver to specify adb path.
	std::string invoke_adb(std::wstring_view args) {
		std::wstring format_result = std::format(L"{} {}", ADB_PATH, args);

		return invoke_throw_on_nonzero(format_result.c_str());
	}

	std::string invoke_adb_with_serial(std::wstring_view serial, std::wstring_view args) {
		std::wstring format_result = std::format(L"{} -s {} {}", ADB_PATH, serial, args);

		return invoke_throw_on_nonzero(format_result.c_str());
	}

	int invoke_adb_capture_output(std::wstring_view serial, std::wstring_view args, OutputCapture consume_output) 
	{
		std::wstring format_result = std::format(L"{} -s {} {}", ADB_PATH, serial, args);
		
		int exit_code;
		invoke_process_capture_output(format_result.c_str(), exit_code, consume_output);
		return exit_code;
	}
}