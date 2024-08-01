#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include "dokan_no_winsock.h"
#include "Connection.hpp"
#include "Nandroid.hpp"
#include "Adb.hpp"
#include "DeviceTracker.hpp"
#include "Logger.hpp"

using namespace nandroidfs;


bool shutdown_requested = false;
BOOL WINAPI console_handler(DWORD signal) {
	if (signal == CTRL_C_EVENT) {
		shutdown_requested = true;
	}
	
	return true;
}

void scan_for_devices(ContextLogger& logger) {

	if (!SetConsoleCtrlHandler(console_handler, true)) {
		logger.warn("could not set Ctrl + C handler, so cannot gracefully exit\n"
			 "This may lead to connection failures on subsequent attempts");
	}

	logger.info("nandroidfs is periodically checking for new devices");
	DeviceTracker device_tracker(logger);
	while (!shutdown_requested) {
		device_tracker.update_connected_devices();
		// TODO, allow this to be customised?
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}

	logger.info("got Ctrl + C signal, shutting down active filesystems");
}

int main()
{
	ContextLogger logger(std::make_shared<StdOutSink>(), "main", LogLevel::Trace);

	try
	{
		logger.info("nandroidfs is starting up");
		DokanInit();

		scan_for_devices(logger);
	}
	catch(std::exception& ex)
	{
		logger.error("nandroidfs crashed due to an unhandled error: {}", ex.what());
	}

	logger.info("goodbye!");
	DokanShutdown();
}