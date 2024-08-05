#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>

#include "dokan_no_winsock.h"
#include "Connection.hpp"
#include "Nandroid.hpp"
#include "Adb.hpp"
#include "DeviceTracker.hpp"
#include "Logger.hpp"
#include "TrayMenu.hpp"

using namespace nandroidfs;

// Maximum number of milliseconds between each call to `adb devices`.
const int DEVICE_CHECK_INTERVAL_MS = 1000;

// Thread synchronisation for notifying the main thread to shut down the app 
// from the system tray or Ctrl + C
std::mutex shutdown_mutex;
std::condition_variable shutdown_cond_var;
bool shutdown_requested = false;

void scan_for_devices(ContextLogger& logger) {
	logger.info("nandroidfs is periodically checking for new devices");
	DeviceTracker device_tracker(logger);

	std::unique_lock lock(shutdown_mutex);
	while (!shutdown_requested) {
		lock.unlock();
		device_tracker.update_connected_devices();
		lock.lock();

		// Wait for a maximum of DEVICE_CHECK_INTERVAL_MS. The actual wait time may be shorter due to spurious wakeups
		shutdown_cond_var.wait_for(lock, std::chrono::milliseconds(DEVICE_CHECK_INTERVAL_MS));
		// ...wait_for unlocks then relocks the unique_lock.
	}

	logger.info("got Ctrl + C signal, shutting down active filesystems");
}

// Checks if NandroidFS is already running.
// If it is, a message box is shown to the user informing them of this, and the function returns `false`.
// If it is not, this function returns `true`.
bool prompt_if_already_running() {
	CreateMutex(NULL, true, L"NandroidFS_Mutex");
	switch (GetLastError()) {
	case ERROR_SUCCESS:
		return true;
	case ERROR_ALREADY_EXISTS:
		MessageBox(NULL, L"NandroidFS is already running. Only one instance is allowed at any one time."
			"\nPlease exit the existing instance if you wish to restart the program.",
			L"NandroidFS already open",
			MB_ICONWARNING | MB_OK);
		return false;
	default:
		// Unknown error occured. Return false to prevent the program from launching (since we're not sure if it's running already)
		return false;
	}
}


int CALLBACK WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int cCmdShow)
{
	// Check that we don't have two instances running at once.
	if (!prompt_if_already_running()) {
		return 1;
	}

	ContextLogger logger(std::make_shared<StdOutSink>(), "main", LogLevel::Trace);
	std::ios_base::sync_with_stdio(false); // Speed up logging by decoupling C and C++ IO.

	TrayMenuManager tray_menu(hInstance, [&]() {
		std::unique_lock lock(shutdown_mutex);
		shutdown_requested = true;
		shutdown_cond_var.notify_one();
	});

	if (!tray_menu.initialise()) {
		logger.error("failed to create tray menu");
	}

	try
	{
		logger.info("nandroidfs is starting up");
		DokanInit();

		scan_for_devices(logger);

		logger.info("goodbye!");
		DokanShutdown();
		return 0;
	}
	catch(std::exception& ex)
	{
		logger.error("nandroidfs crashed due to an unhandled error: {}", ex.what());
		DokanShutdown();
		return 1;
	}
}