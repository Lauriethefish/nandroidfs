#pragma once

#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "Nandroid.hpp"
#include "dokan_no_winsock.h"

namespace nandroidfs {
	// Keeps track of the connected ADB devices and attempts to mount a filesystem for each.
	// Manages the Nandroid* instance for each ADB device.
	class DeviceTracker {
	public:
		~DeviceTracker();

		// Attempts to create a nandroid instance and mount a filesystem for the device with the provided serial number.
		// ... adding it to the internal collection of devices.
		// Throws an exception if this fails, or if a filesystem is already mounted for the provided device.
		Nandroid* connect_to_device(std::string serial);
		// Removes the filesystem for a device and releases all resources.
		void unmount_device(Nandroid* handle);

		// Returns a set of the serial numbers of all authorized ADB devices.
		std::unordered_set<std::string> list_authorized_adb_devices();

		// Mounts ADB devices that are not currently mounted as filesystems and dismounts any filesystems that no longer correspond to a connected ADB device.
		void update_connected_devices();

	private:
		std::mutex device_man_mtx;
		uint16_t current_port = 25989;

		// Does not lock.
		Nandroid* connect_to_device_internal(std::string serial);

		// Key is device serial number.
		std::unordered_map<std::string, Nandroid*> instances;
		// Keep track of serial numbers of devices for which the connection failed.
		// This avoids spamming the logs continually trying to reconnect to a device.
		std::unordered_set<std::string> failed_to_connect;
	};
};