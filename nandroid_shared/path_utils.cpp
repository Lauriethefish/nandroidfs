
#include "path_utils.hpp"

#include <stdexcept>

namespace nandroidfs {
	std::string get_full_path(std::string dir_path, std::string file_name) {
		if (!dir_path.ends_with('/')) {
			dir_path.push_back('/');
		}
		dir_path.append(file_name);
		return dir_path;
	}

	std::optional<std::string> get_parent_path(std::string path) {
		// Remove the final `/` if this path is `/` terminated.
		if (path.ends_with('/')) {
			path.pop_back();
		}

		// Find the last slash now that the trailing slash has been removed.
		size_t last_separator_offset = path.find_last_of('/');
		if (last_separator_offset == std::string::npos) {
			return std::nullopt;
		}

		path.resize(last_separator_offset);
		if (path.length() == 0) { // No file path after the last slash, so no parent.
			return std::nullopt;
		}
		else
		{
			return path;
		}
	}
}