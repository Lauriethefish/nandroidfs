#pragma once

#include "dokan_no_winsock.h"

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace nandroidfs {
	// Manages the nandroid tray menu and handles enabling/disabling the debug console
	// when the user requests.
	// Takes in a callback to trigger the main thread to quit nandroid when desired.
	// This callback will also be triggered if the user presses Ctrl + C in the console (if it is enabled.)
	// This class is a singleton, creating more than one instance will result in an error.
	class TrayMenuManager {
	public:
		// Creates a new instance of the class but does NOT yet create the tray menu.
		TrayMenuManager(HINSTANCE hInstance, std::function<void()> quit_callback);
		// Destroys the TrayMenuManager and releases all associated resources.
		~TrayMenuManager();

		// Attempts to create the tray menu and begins listening for events for the created window
		// This all happens on another thread.
		// This must only be called once on a given instance.
		// Returns false if initialising the tray menu failed, true if it succeeded.
		bool initialise();
	private:
		// Attempts to set whether or not the console is enabled.
		// Returns `true` if this operation succeeded.
		// Should only be called by event loop thread.
		bool set_console_enabled(bool enabled);

		// Deletes the menu window and releases all resources
		// Must be called on the window thread.
		void release_resources();


		bool register_window_class();
		bool load_window_icon();
		bool create_rightclick_menu();
		bool update_rightclick_menu(); // Sets whether the "Enable Console" is checked or unchecked.

		void event_loop();
		// Called by windows whenever a message needs to be processed on the window.
		static LRESULT CALLBACK tray_window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
		// Handles the console Ctrl + C event.
		static BOOL WINAPI console_handler(DWORD signal);

		void window_thread_entry_point();
		// Intialises the tray menu, on the current thread.
		// Returns whether it succeeded
		bool initialise_internal();

		// The `this` pointer for the current instance.
		// Necessary as the console Ctrl + C handler callback needs the current instance
		// in order to invoke the quit callback.
		static inline std::atomic<TrayMenuManager*> inst = nullptr;
		// Whether `initialise` has yet been called.
		bool initialised = false;
		// Whether the initialisation process has succeeded.
		bool init_succeeded = false;
		// Whether the initialisation process has completed (succeeded or failed)
		bool init_process_finished = false;
		std::condition_variable init_cond_var;
		std::mutex init_mutex;

		// Whether the console has ever been allocated, regardless of if it is currently open/closed.
		bool alloced_console = false;

		bool console_enabled = false;
		std::function<void()> quit_callback;

		static inline const LPCTSTR window_class_name = L"NandroidTrayWnd";
		bool window_class_registered = false;

		HINSTANCE hInstance;
		HWND tray_icon_window = NULL;
		int tray_icon_id = 0;
		HMENU right_click_menu = 0;
		HICON exe_icon = NULL;
		std::thread event_loop_thread;
	};
}