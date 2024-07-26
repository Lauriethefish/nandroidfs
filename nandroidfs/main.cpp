#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include "dokan_no_winsock.h"
#include "Connection.hpp"
#include "Nandroid.hpp"
#include "Adb.hpp"
#include "DeviceTracker.hpp"

using namespace nandroidfs;

bool shutdown_requested = false;
BOOL WINAPI console_handler(DWORD signal) {
	if (signal == CTRL_C_EVENT) {
		shutdown_requested = true;
	}
	
	return true;
}

void scan_for_devices() {
	if (!SetConsoleCtrlHandler(console_handler, true)) {
		std::cerr << "Warning: could not set Ctrl + C handler. Cannot gracefully exit" << std::endl;
	}

	DeviceTracker device_tracker;

	while (!shutdown_requested) {
		device_tracker.update_connected_devices();
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}

	std::cout << "Shutting down nandroidfs" << std::endl;
}

int main()
{
	try
	{
		std::cout << "Starting up NandroidFS" << std::endl;
		DokanInit();

		std::cout << "Checking for devices periodically" << std::endl;
		scan_for_devices();
	}
	catch(std::exception& ex)
	{
		std::cerr << "NandroidFS crashed!" << std::endl << ex.what() << std::endl;
	}

	std::cout << "Goodbye!" << std::endl;
	DokanShutdown();
}