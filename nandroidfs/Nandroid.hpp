#pragma once

#include "dokan_no_winsock.h"
#include "Connection.hpp"
#include "Logger.hpp"

#include <string>
#include <optional>
#include <functional>
#include <thread>
#include <condition_variable>

namespace nandroidfs 
{
	class DeviceTracker;

	class Nandroid 
	{
	public:
		// Creates a new Nandroid filesystem, intended for use with the ADB device with the given serial number.
		// The provided port is used to open a TCP connection between the ADB device and this application via `adb forward`.
		Nandroid(DeviceTracker& parent, std::string device_serial, uint16_t port_num, ContextLogger& parent_logger);
		Nandroid() = delete;
		~Nandroid(); // Destructor unmounts the filesystem if mounted.

		// Initialises the filesystem.
		// This will push the nandroid agent to the target device and invoke it on another thread.
		// It will connect to the agent and then finally attempt to mount the filesystem.
		void begin();

		Connection& get_conn();
		ContextLogger& get_operations_logger();

		std::string get_device_serial();
		std::wstring get_device_serial_wide();

		// Unmounts the device.
		void unmount();

	private:
		void invoke_daemon();
		void handle_daemon_output(uint8_t* buffer, int length);

		void mount_filesystem();

		ContextLogger logger;
		ContextLogger agent_logger;
		ContextLogger operations_logger;

		std::string agent_output_buffer;

		// Nullopt if the connection has yet to be established.
		Connection* connection = nullptr;
		std::thread agent_invoke_thread;

		// Communication between the agent thread and startup thread when waiting for the agent to finish starting
		bool agent_ready = false;
		std::mutex mtx_agent_ready;

		bool agent_ready_notified = false; // Whether or not the condition variable has been notified.
		std::condition_variable cv_agent_ready;

		bool agent_dead_notified = false;
		std::condition_variable cv_agent_dead;

		DOKAN_HANDLE instance = nullptr;
		DeviceTracker& parent;

		std::string device_serial;
		std::wstring wide_device_serial;
		uint16_t port_num;
	};
}