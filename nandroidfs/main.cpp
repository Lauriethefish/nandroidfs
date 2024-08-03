#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include "dokan_no_winsock.h"
#include "Connection.hpp"
#include "Nandroid.hpp"
#include "Adb.hpp"
#include "DeviceTracker.hpp"
#include "Logger.hpp"
#include "TrayMenu.hpp"

using namespace nandroidfs;


std::atomic_bool shutdown_requested = false;
void scan_for_devices(ContextLogger& logger) {
	logger.info("nandroidfs is periodically checking for new devices");
	DeviceTracker device_tracker(logger);
	while (!shutdown_requested.load()) {
		device_tracker.update_connected_devices();
		// TODO, allow this to be customised?
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}

	logger.info("got Ctrl + C signal, shutting down active filesystems");
}


int CALLBACK WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR lpCmdLine,
	int cCmdShow)
{
	ContextLogger logger(std::make_shared<StdOutSink>(), "main", LogLevel::Trace);
	std::ios_base::sync_with_stdio(false); // Speed up logging by decoupling C and C++ IO.

	TrayMenuManager tray_menu(hInstance, [&]() {
		shutdown_requested.store(true);
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