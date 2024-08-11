#include "Adb.hpp"
#include "win_path_util.hpp"
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

	// Allocates a new string and copies `orig` into it.
	LPWSTR non_constify_string(LPCWSTR orig) {
		LPWSTR nonconst = new WCHAR[lstrlenW(orig) + 1]; // + 1 for the terminating NULL
		lstrcpyW(nonconst, orig);
		return nonconst;
	}

	PROCESS_INFORMATION create_process_pipe_output(HANDLE child_stdout_write, LPCWSTR command_line, DWORD flags) {
		// Set options needed to start the process.
		PROCESS_INFORMATION process_info;
		STARTUPINFO start_info;
		ZeroMemory(&process_info, sizeof(PROCESS_INFORMATION));
		ZeroMemory(&start_info, sizeof(STARTUPINFO));
		start_info.cb = sizeof(STARTUPINFO);
		start_info.hStdError = child_stdout_write;
		start_info.hStdOutput = child_stdout_write;
		start_info.dwFlags = STARTF_USESHOWWINDOW;
		start_info.wShowWindow = SW_HIDE;
		// ...TODO: Allow supplying a source for stdin?

		// Create a clone of the arguments that can be written to, to avoid an access violation.
		LPWSTR nonconst_args = non_constify_string(command_line);
		start_info.dwFlags |= STARTF_USESTDHANDLES;
		bool success = CreateProcess(nullptr,
			nonconst_args,
			nullptr, // No extra security attributes
			nullptr,
			true, // Inherit handles.
			flags,
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
		OutputCapture consume_output, DWORD flags) {
		// Create a pipe for the process stdout and stderr.
		HANDLE child_stdout_read, child_stdout_write;
		create_stdouterr_pipes(&child_stdout_write, &child_stdout_read);

		// Now ready to actually create the process.
		PROCESS_INFORMATION process_info = create_process_pipe_output(child_stdout_write, command_line, flags);
		CloseHandle(child_stdout_write); // Close the write handle to the child's stdout FROM THIS PROCESS
		// This means that the ReadFile call will EOF when the child process exits.

		// Read data from the process until EOF.
		const int STDOUT_READ_BUF_SIZE = 4096;
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

	std::atomic<LPCWSTR> adb_invoke_path = nullptr;

	// Checks if the ADB installation at the given path exists.
	// `invoke_path` must be quoted if it contains spaces.
	bool adb_install_exists(LPCWSTR invoke_path) {
		PROCESS_INFORMATION process_info;
		STARTUPINFO start_info;
		ZeroMemory(&process_info, sizeof(PROCESS_INFORMATION));
		ZeroMemory(&start_info, sizeof(STARTUPINFO));
		start_info.cb = sizeof(STARTUPINFO);
		start_info.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
		start_info.hStdError = nullptr;
		start_info.hStdOutput = nullptr;
		start_info.hStdInput = nullptr;
		// ...use nullptr for each handle to ensure that the stdout/err doesn't go anywhere
		// NB: Cannot find any documentation to support this but seems to work.
		start_info.wShowWindow = SW_HIDE;

		LPWSTR nonconst_invoke_path = non_constify_string(invoke_path);
		bool success = CreateProcess(nullptr, nonconst_invoke_path, nullptr, nullptr, true, 0, nullptr, nullptr, &start_info, &process_info);
		CloseHandle(process_info.hThread);
		CloseHandle(process_info.hProcess);

		// TODO: Verify got correct error
		return success;
	}

	LPCWSTR find_adb_path() {
		LPCWSTR current = adb_invoke_path.load();
		if (current) {
			return current;
		}
		
		// Check if ADB exists on PATH, and use this version if so
		if (adb_install_exists(L"adb")) {
			adb_invoke_path.store(L"adb");
			return L"adb";
		}

		// Otherwise, combine the executable path with the expected 
		// location of `platform-tools` as extracted by the installer.
		LPWSTR included_adb_path = win_path_util::get_abs_path_from_rel_to_exe(L"platform-tools\\adb.exe");
		if (included_adb_path) {
			int included_path_len = lstrlenW(included_adb_path);
			// Wrap the path in quotes as "Program Files" has a space.
			LPWSTR quoted_adb_path = new WCHAR[included_path_len + 3];
			quoted_adb_path[0] = L'"';
			quoted_adb_path[included_path_len + 2] = 0;
			quoted_adb_path[included_path_len + 1] = L'"';
			memcpy(quoted_adb_path + 1, included_adb_path, included_path_len * 2);

			delete[] included_adb_path;

			if (adb_install_exists(quoted_adb_path)) {
				adb_invoke_path.store(quoted_adb_path);
				return quoted_adb_path;
			}

			delete[] quoted_adb_path;
		}

		throw std::runtime_error("No ADB installation found!");
	}

	std::string invoke_adb(std::wstring_view args) {
		std::wstring format_result = std::format(L"{} {}", find_adb_path(), args);

		return invoke_throw_on_nonzero(format_result.c_str());
	}

	std::string invoke_adb_with_serial(std::wstring_view serial, std::wstring_view args) {
		std::wstring format_result = std::format(L"{} -s {} {}", find_adb_path(), serial, args);

		return invoke_throw_on_nonzero(format_result.c_str());
	}

	int invoke_adb_capture_output(std::wstring_view serial, std::wstring_view args, OutputCapture consume_output) 
	{
		std::wstring format_result = std::format(L"{} -s {} {}", find_adb_path(), serial, args);
		
		int exit_code;
		invoke_process_capture_output(format_result.c_str(), exit_code, consume_output, /* prevent Ctrl+C interrupts: */ CREATE_NEW_PROCESS_GROUP);
		return exit_code;
	}
}