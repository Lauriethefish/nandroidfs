#pragma once

#include <string>
#include <optional>

namespace nandroidfs {
	// Gets the full path of a file within a directory
	std::string get_full_path(std::string dir_path, std::string file_name);

	// Gets the parent path (i.e. path to the parent directory) of the given directory/file path.
	// Nullopt if the provided path is the root (`/`) 
	// ...or is a relative path with no slashes within it. (i.e., it is a file in the CWD)
	std::optional<std::string> get_parent_path(std::string path);
}