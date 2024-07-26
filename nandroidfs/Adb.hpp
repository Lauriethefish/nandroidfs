#pragma once

#include <string>
#include <functional>
#include "dokan_no_winsock.h"

#define STDOUT_READ_BUF_SIZE 4096

namespace nandroidfs {
	typedef std::function<void(uint8_t* buf, int length)> OutputCapture;

	// Invokes a process with the provided name and arguments, and captures its stdout and stderr.
	// Throws std::runtime_error if invoking the process failed.
	void invoke_process_capture_output(LPCWSTR command_line, int& out_exit_code,
		// Takes in the stdout and stderr from the process.
		// The output is in a buffer at the provided pointer and has the specified `length` in bytes.
		OutputCapture consume_output);

	// Invokes a process with the provided name and arguments, and captures its stdout and stderr.
	// These are saved in the provided string
	std::string invoke_process_capture_output_string(LPCWSTR command_line, int& out_exit_code);

	// Invokes ADB with the specified arguments.
	// Returns the stdout and stderr as a string if a zero exit code is returned.
	// Throws std::runtime_error if invoking ADB fails or if there is a nonzero exit code.
	std::string invoke_adb(std::wstring_view args);

	// Invokes ADB with the specified arguments and device serial number.
	std::string invoke_adb_with_serial(std::wstring_view serial, std::wstring_view args);


	// Invokes ADB with the specified serial number and arguments
	// Forwards stdout and stderr to the provided function.
	// Returns the exit code (will not throw on nonzero exit code)
	int invoke_adb_capture_output(std::wstring_view serial, std::wstring_view args, OutputCapture consume_output);
}