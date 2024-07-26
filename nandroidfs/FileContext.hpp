#pragma once

#include "requests.hpp"

namespace nandroidfs {
	// The context about each open file handle
	// This is stored by dokan and provided automatically to each function in `operations.cpp`.
	struct FileContext {
		// The file descriptor of the open file on the Android device.
		// If this is -1, then the file was not actually opened by the createfile call.
		// This happens if only access to attributes is required.
		FILE_HANDLE handle;
		// Whether the file was opened with read access.
		bool read_access;
		// Whether the file was opened with write access.
		bool write_access;
	};
}