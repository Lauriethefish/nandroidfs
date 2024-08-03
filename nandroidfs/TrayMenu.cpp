#include "TrayMenu.hpp"
#include "dokan_no_winsock.h"
#include <iostream>


namespace nandroidfs {
	// Message sent when the user right clicks to open a menu on the tray icon.
	const UINT TRAY_ICON_CLICK_MSG = (WM_APP + 1);
	const UINT QUIT_ITEM_ID = 1;
	const UINT CONSOLE_ITEM_ID = 2;

	TrayMenuManager::TrayMenuManager(HINSTANCE hInstance, std::function<void()> quit_callback) :
		quit_callback(quit_callback),
		hInstance(hInstance) {
		TrayMenuManager* current_inst = nullptr;
		TrayMenuManager::inst.compare_exchange_strong(current_inst, this);
		if (current_inst) {
			throw std::runtime_error("Attemted to create multiple instances of TrayMenuManager");
		}
	}

	TrayMenuManager::~TrayMenuManager() {
		// Close the window to release resources and ensure the thread terminates
		if (tray_icon_window) {
			PostMessage(tray_icon_window, WM_CLOSE, 0, 0);
		}

		event_loop_thread.join();
		TrayMenuManager::inst.store(nullptr); // Allow another instance to be creatred.
	}

	void TrayMenuManager::release_resources() {
		if (tray_icon_id) {
			NOTIFYICONDATA nicon_data;
			nicon_data.cbSize = sizeof(NOTIFYICONDATA);
			nicon_data.hWnd = tray_icon_window;
			nicon_data.uID = tray_icon_id;
			Shell_NotifyIcon(NIM_DELETE, &nicon_data);
		}

		if (tray_icon_window) {
			DestroyWindow(tray_icon_window);
		}
		tray_icon_window = 0;

		if (exe_icon) {
			DestroyIcon(exe_icon);
		}

		if (window_class_registered) {
			UnregisterClass(window_class_name, hInstance);
		}
	}

	// The (MSVC) linker provides this symbol in order to locate the HMODULE 
	// for the currently executing module.
	EXTERN_C IMAGE_DOS_HEADER __ImageBase;
	#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)

	bool TrayMenuManager::initialise() {
		if (initialised) {
			throw std::runtime_error("already initialised");
		}
		initialised = true;

		// Start the window thread and wait until initialisation succeeds or fails.
		this->event_loop_thread = std::thread(&TrayMenuManager::window_thread_entry_point, this);
		std::unique_lock lock(init_mutex);
		init_cond_var.wait(lock, [this] { return this->init_process_finished; });

		return init_succeeded;
	}

	void TrayMenuManager::window_thread_entry_point() {
		bool success = initialise_internal();
		if (!success) {
			release_resources(); // Release all resources allocated thus far
		}

#ifdef _DEBUG
		set_console_enabled(true); // Enable console by default on debug builds.
#endif

		// Inform the main thread that initialisation has finished.
		std::unique_lock lock(init_mutex);
		this->init_process_finished = true;
		this->init_succeeded = success;
		this->init_cond_var.notify_one();
		lock.unlock();

		event_loop();
	}

	// TODO: Actually log errors if any of this fails, instead of only returning false.
	bool TrayMenuManager::initialise_internal() {
		// Create a class that handles events on the window.
		if (!register_window_class()) {
			return false;
		}
		window_class_registered = true;

		// Create a message-only window that handles the click events on the tray menu.
		tray_icon_window = CreateWindowEx(0, window_class_name, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
		if (tray_icon_window == 0) {
			return false;
		}

		if (!load_window_icon()) {
			return false;
		}

		// Create the basic menu that appears when the user right clicks.
		if (!create_rightclick_menu()) {
			return false;
		}
		update_rightclick_menu();
		
		// Create a tray icon
		const int default_tray_icon_id = 1;
		NOTIFYICONDATA icon_data;
		ZeroMemory(&icon_data, sizeof(NOTIFYICONDATA));
		icon_data.cbSize = sizeof(NOTIFYICONDATA);
		icon_data.hWnd = tray_icon_window;
		icon_data.uID = default_tray_icon_id;
		icon_data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		lstrcpy(icon_data.szTip, L"NandroidFS is running");
		icon_data.uCallbackMessage = TRAY_ICON_CLICK_MSG;
		icon_data.hIcon = exe_icon;
		icon_data.uVersion = NOTIFYICON_VERSION_4;

		if (!Shell_NotifyIcon(NIM_ADD, &icon_data)) {
			return false;
		}
		tray_icon_id = default_tray_icon_id;

		if (!Shell_NotifyIcon(NIM_SETVERSION, &icon_data)) {
			return false;
		}

		return true;
	}

	bool TrayMenuManager::register_window_class() {
		WNDCLASS window_class;
		ZeroMemory(&window_class, sizeof(WNDCLASS));
		window_class.lpfnWndProc = &tray_window_proc;
		window_class.hInstance = hInstance;
		window_class.lpszClassName = window_class_name;
		return RegisterClass(&window_class);
	}

	bool TrayMenuManager::load_window_icon() {
		// Find the path of the current executable
		const int EXE_BUF_SIZE = 512;
		char* exe_path_buffer = new char[EXE_BUF_SIZE];
		if (GetModuleFileNameA(HINST_THISCOMPONENT, exe_path_buffer, EXE_BUF_SIZE) == 0) {
			delete[] exe_path_buffer;
			return false;
		}

		// Grab the (nandroidfs) icon from the current executable.
		exe_icon = ExtractIconA(hInstance, exe_path_buffer, 0);
		delete[] exe_path_buffer;

		return exe_icon != NULL;
	}

	bool TrayMenuManager::create_rightclick_menu() {
		right_click_menu = CreatePopupMenu();
		if (!right_click_menu) {
			return false;
		}

		if (!InsertMenu(right_click_menu, 0, MF_STRING, QUIT_ITEM_ID, L"Quit NandroidFS")) {
			return false;
		}
		if (!InsertMenu(right_click_menu, 0, MF_STRING, CONSOLE_ITEM_ID, L"Show Console")) {
			return false;
		}
		return true;
	}

	bool TrayMenuManager::update_rightclick_menu() {
		if (!right_click_menu) {
			return false;
		}

		MENUITEMINFO console_item_info;
		ZeroMemory(&console_item_info, sizeof(MENUITEMINFO));
		// Enable/disable the checkmark depending on if the console is enabled or disabled.
		console_item_info.cbSize = sizeof(MENUITEMINFO);
		console_item_info.fState = console_enabled ? MFS_CHECKED : MFS_UNCHECKED;
		console_item_info.fMask = MIIM_STATE;
		bool success = SetMenuItemInfo(right_click_menu, CONSOLE_ITEM_ID, false, &console_item_info);
		return success;
	}

	bool TrayMenuManager::set_console_enabled(bool new_enabled) {
		if (new_enabled == console_enabled) {
			return true;
		}

		if(new_enabled) {
			// Allocate and set up the console if it is the first time we are enabling the console.
			if (!alloced_console) {
				if (!AllocConsole()) {
					return false;
				}

				// Add context to the console window.
				SetConsoleCtrlHandler(&TrayMenuManager::console_handler, true);

				// Doesn't really matter if this succeeds, so ignore the error.
				SetConsoleTitle(L"NandroidFS Console");
				
				// Point stdout and stderr so that they write to our new console.
				FILE* new_stdout;
				FILE* new_stderr;
				freopen_s(&new_stdout, "CONOUT$", "w", stdout);
				freopen_s(&new_stderr, "CONOUT$", "w", stderr);
				std::cout.clear();
				std::cerr.clear();

				alloced_console = true;
			}
			else {
				ShowWindow(GetConsoleWindow(), SW_SHOW);
			}
		}
		else if(alloced_console)
		{
			// Hide the current console window if allocated
			ShowWindow(GetConsoleWindow(), SW_HIDE);
		}

		console_enabled = new_enabled;
		update_rightclick_menu(); // Check/uncheck the "enable console" button.
		return true;
	}

	// Standard event loop to respond to clicking and mouse movements on our window.
	void TrayMenuManager::event_loop() {
		MSG msg;
		while (tray_icon_window != NULL && GetMessage(&msg, tray_icon_window, 0, 0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}


	LRESULT CALLBACK TrayMenuManager::tray_window_proc(HWND window_handle, UINT msg, WPARAM w_param, LPARAM l_param) {
		// Get the context from the window.
		// A separate thread is created for each TrayMenuManager instance, so this thread_local
		// variable is guaranteed to point to the correct instance.
		auto* inst = TrayMenuManager::inst.load();
		switch (msg) {
		case WM_CLOSE:
			inst->release_resources();
			return 0;
		case TRAY_ICON_CLICK_MSG:
			switch (LOWORD(l_param)) {
			case WM_CONTEXTMENU: // Right clicking to open a menu
				if (inst->right_click_menu) {
					// Find where the mouse is and "deploy" the pop-up menu here.
					POINT cursor_pos;
					GetCursorPos(&cursor_pos);
					SetForegroundWindow(window_handle);
					TrackPopupMenu(inst->right_click_menu,
						TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN,
						cursor_pos.x,
						cursor_pos.y,
						0,
						window_handle,
						NULL);
				}

				break;
			}

			return 0;
		case WM_COMMAND:
			if (HIWORD(w_param) == 0) {
				switch (LOWORD(w_param)) { // l_param contains the menu item ID.
				case QUIT_ITEM_ID:
					inst->quit_callback();
					break;
				case CONSOLE_ITEM_ID:
					// Toggle whether the console is enabled.
					inst->set_console_enabled(!inst->console_enabled);
					break;
				}

				return 0;
			}
		}

		return DefWindowProc(window_handle, msg, w_param, l_param);
	}

	BOOL WINAPI TrayMenuManager::console_handler(DWORD signal) {
		if (signal == CTRL_C_EVENT) {
			// Trigger the quit callback if console Ctrl + C is pressed.
			TrayMenuManager::inst.load()->quit_callback();
		}

		return true;
	}
}