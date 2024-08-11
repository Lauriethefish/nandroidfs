#pragma once

#include "dokan_no_winsock.h"

// Utilities for working with paths on windows.
namespace nandroidfs::win_path_util {
	// Gets the path to the currently executing nandroidfs process.
	// Returns nullptr if the operation fails.
	LPWSTR get_exe_path();

	// Gets the path that is the concatenation of the parent directory of the NandroidFS executable and `relative_path`.
	// Returns nullptr if the operation fails.
	LPWSTR get_abs_path_from_rel_to_exe(LPCWSTR relative_path);
}