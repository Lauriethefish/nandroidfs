#include "DeviceTracker.hpp"
#include "Adb.hpp"

#include <iostream>

namespace nandroidfs {
	std::unordered_set<std::string> DeviceTracker::list_authorized_adb_devices() {
		std::string output = invoke_adb(L"devices");
		std::unordered_set<std::string> devices_serial;

		// Get the serial number of each connected device
		size_t prev_newline_pos = -1;
		size_t next_newline_pos;
		while ((next_newline_pos = output.find('\n', prev_newline_pos + 1)) != std::string::npos) {
			std::string line = output.substr(prev_newline_pos + 1, next_newline_pos - prev_newline_pos - 1);
			prev_newline_pos = next_newline_pos;
			if (line.ends_with('\r')) {
				line.pop_back();
			}

			if (line == "List of devices attached" || line.empty()) {
				continue;
			}

			// Parse the line to get the device information
			size_t tab_idx = line.find('\t');
			if (tab_idx == std::string::npos) {
				// Invalid device line: no space
				continue;
			}

			std::string serial_no = line.substr(0, tab_idx);
			std::string status = line.substr(tab_idx + 1);
			if (status == "device") {
				devices_serial.insert(serial_no);
			}
		}

		return devices_serial;
	}

	DeviceTracker::~DeviceTracker() {
		std::lock_guard lock(device_man_mtx);

		std::cout << "Unmounting all devices" << std::endl;
		for (auto& pair : instances) {
			std::cout << "Unmouting " << pair.first << std::endl;
			delete pair.second;
		}
	}

	// Attempts to create a nandroid instance and mount a filesystem for the device
	// with the provided serial number.
	// Throws an exception if this fails.
	Nandroid* DeviceTracker::connect_to_device(std::string serial) {
		std::lock_guard lock(device_man_mtx);
		
		return connect_to_device_internal(serial);
	}

	Nandroid* DeviceTracker::connect_to_device_internal(std::string serial) {
		Nandroid* instance = new Nandroid(*this, serial, current_port);
		current_port++;
		try
		{
			instance->begin();
			instances[serial] = instance;
		}
		catch (const std::exception&)
		{
			delete instance;
			throw;
		}

		return instance;
	}

	void DeviceTracker::unmount_device(Nandroid* handle) {
		std::lock_guard lock(device_man_mtx);

		std::cout << "Device " << handle->get_device_serial() << " unmounting!" << std::endl;
		instances.erase(handle->get_device_serial());
		delete handle;
	}

	void DeviceTracker::update_connected_devices() {
		std::lock_guard lock(device_man_mtx);
		std::unordered_set<std::string> devices = list_authorized_adb_devices();

		for (std::string device_serial : devices) {
			// Find devices that are not yet mounted and haven't failed to connect previously.
			if (!instances.contains(device_serial) && !failed_to_connect.contains(device_serial)) {
				std::cout << "Found new authorized device: " << device_serial << " (initializing connection)" << std::endl;
				try
				{
					Nandroid* connection = connect_to_device_internal(device_serial);
				}
				catch (const std::exception& ex)
				{
					std::cerr << "Failed to connect: " << ex.what() << std::endl;
					std::cerr << "Will not attempt to connect to this device again" << std::endl;
					failed_to_connect.insert(device_serial);
				}
			}
		}

		// Unmount any devices that no longer exist
		// NB this also happens if any request to the daemon fails due to an EOF on read
		// So this step isn't super important - but may help the device get removed more quickly in some cases...
		// ...where no requests are made for a long time.
		std::vector<std::string> to_remove;
		for (auto& instance : instances) {
			if (!devices.contains(instance.first)) {
				std::cout << "Device " << instance.first << " disconnected :(" << std::endl;
				to_remove.push_back(instance.first);
			}
		}
		for (std::string serial : to_remove) {
			delete instances[serial];
			instances.erase(serial);
		}
	}
}