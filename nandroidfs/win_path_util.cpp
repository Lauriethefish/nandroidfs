#include "win_path_util.hpp"
#include <Shlwapi.h>

// The (MSVC) linker provides this symbol in order to locate the HMODULE 
// for the currently executing module.
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)

namespace nandroidfs::win_path_util {
	LPWSTR get_exe_path() {
		WCHAR* exe_path_buffer = new WCHAR[MAX_PATH];
		if (GetModuleFileName(HINST_THISCOMPONENT, exe_path_buffer, MAX_PATH)) {
			return exe_path_buffer;
		}
		else
		{
			delete[] exe_path_buffer;
			return nullptr;
		}
	}

	LPWSTR get_abs_path_from_rel_to_exe(LPCWSTR relative_path) {
		LPWSTR exe_path = get_exe_path();
		if (!exe_path) {
			return nullptr;
		}

		LPWSTR abs_path = new WCHAR[MAX_PATH];
		PathRemoveFileSpec(exe_path);
		PathCombine(abs_path, exe_path, relative_path);
		delete[] exe_path;
		return abs_path;
	}
}