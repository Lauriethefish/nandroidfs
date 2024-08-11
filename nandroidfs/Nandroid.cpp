#include "Nandroid.hpp"
#include "operations.hpp"
#include "Adb.hpp"
#include "conversion.hpp"
#include "DeviceTracker.hpp"
#include "win_path_util.hpp"
#include <format>
#include <stdexcept>
#include <iostream>

LPCWSTR AGENT_REL_PATH = L"nandroid-daemon";
LPCWSTR AGENT_DEST_PATH = L"/data/local/tmp/nandroid-daemon";

namespace nandroidfs 
{
	Nandroid::Nandroid(DeviceTracker& parent, std::string device_serial, uint16_t port_num, ContextLogger& parent_logger) :
		parent(parent),
		logger(parent_logger.replace_context(device_serial)),
		agent_logger(logger.with_context("agent_std(out|err)")),
		operations_logger(logger.with_context("operations"))
	{
		this->device_serial = device_serial;
		wide_device_serial = wstring_from_string(device_serial);
		this->port_num = port_num;
	}

	Nandroid::~Nandroid() {
		// Stop dokan calls first so that we can safely delete the connection.
		logger.info("unmounting drive");
		if (instance) {
			DokanCloseHandle(instance);
		}

		if (connection) {
			logger.debug("closing socket connection");
			// Delete the connection immediately
			// This will cause the agent to exit as it loses connection ...
			delete connection;
		}

		{
			logger.trace("dropping ready lock");
			std::unique_lock lock(mtx_agent_ready);
			bool notified = cv_agent_dead.wait_for(lock, std::chrono::milliseconds(2000), [this] { return this->agent_dead_notified; });
			if (!notified) {
				logger.warn("killing daemon as it did not exit within the timeout when the connection was killed.");
				try {
					invoke_adb_with_serial(wide_device_serial, std::format(L"shell pkill nandroid-daemon"));
				}
				catch (const std::exception& ex)
				{
					logger.error("failed to kill daemon, giving up: {}", ex.what());
				}
			}
		}

		logger.trace("Waiting for agent invoke thread to stop");
		this->agent_invoke_thread.join();
	}

	void Nandroid::unmount() {
		this->parent.unmount_device(this);
	}

	std::string Nandroid::get_device_serial() {
		return device_serial;
	}

	std::wstring Nandroid::get_device_serial_wide() {
		return wide_device_serial;
	}

	void Nandroid::begin() {
		logger.debug("preparing for agent execution");
		logger.trace("pushing agent to quest");

		LPWSTR agent_path = win_path_util::get_abs_path_from_rel_to_exe(AGENT_REL_PATH);
		if (!agent_path) {
			throw std::runtime_error("Failed to get agent path");
		}

		invoke_adb_with_serial(wide_device_serial, std::format(L"push \"{}\" {}", agent_path, AGENT_DEST_PATH));
		logger.trace("chmodding agent");
		invoke_adb_with_serial(wide_device_serial, std::format(L"shell chmod +x {}", AGENT_DEST_PATH));
		logger.debug("forwarding device port {} to local port {}", AGENT_PORT, port_num);
		invoke_adb_with_serial(wide_device_serial, std::format(L"forward tcp:{} tcp:{}", port_num, AGENT_PORT));

		// Get the daemon running ready to initialise the connection
		logger.debug("executing agent, and waiting for it to be ready for requests");
		this->agent_invoke_thread = std::thread(&Nandroid::invoke_daemon, this);

		// Wait for the "ready" message before starting the connection.
		std::unique_lock lock(mtx_agent_ready);
		cv_agent_ready.wait(lock, [this] { return agent_ready_notified;  });
		agent_ready_notified = false;

		if (!agent_ready) {
			throw std::runtime_error("Agent failed to start up");
		}

		// Initialise the TCP connection with the agent, which will carry out a brief handshake to ensure the connection is working.
		this->connection = new Connection(std::string("localhost"), port_num, logger);
		
		// Now the connection is established, we can make an attempt to mount the drive.
		mount_filesystem();
	}

	void Nandroid::invoke_daemon() {
		std::string line_buffer;

		try
		{
			int exit_code = invoke_adb_capture_output(wide_device_serial,
				std::format(L"shell .{}", AGENT_DEST_PATH),
				std::bind(&Nandroid::handle_daemon_output, this, std::placeholders::_1, std::placeholders::_2));
			logger.debug("agent exited with code: {}", exit_code);
		}
		catch (const std::exception& ex)
		{
			logger.error("error occured while invoking the agent, did ADB.exe suddenly get removed?\n"
			"{}", ex.what());
		}

		// Notify (the destructor) that the agent is no longer running (or failed to run.)
		std::unique_lock lock(mtx_agent_ready);
		agent_ready = false;
		agent_ready_notified = true;
		agent_dead_notified = true;
		cv_agent_ready.notify_all();
		cv_agent_dead.notify_all();
	}

	void Nandroid::mount_filesystem()
	{
		logger.info("mounting filesystem");
		PDOKAN_OPTIONS dokan_options = new DOKAN_OPTIONS();
		// Ensure the options aren't full of uninitialised garbage
		ZeroMemory(dokan_options, sizeof(DOKAN_OPTIONS));
		dokan_options->Version = DOKAN_VERSION;
		dokan_options->Options = DOKAN_OPTION_CASE_SENSITIVE;

		// Begin with `D:\` when searching for a mount point.
		// Will then try the following drives in turn until we find one that mounts successfully.
		std::wstring mount_point = L"D:\\";
		dokan_options->MountPoint = mount_point.c_str();
		dokan_options->SingleThread = false;

		// Enable debug printing to stderr.
		// dokan_options->Options |= DOKAN_OPTION_STDERR | DOKAN_OPTION_DEBUG;
		// TODO: Set timeout potentially, e.g. if existing is too short

		dokan_options->GlobalContext = reinterpret_cast<ULONG64>(this);
		NTSTATUS status = DOKAN_MOUNT_ERROR;
		while (status == DOKAN_MOUNT_ERROR) {
			status = DokanCreateFileSystem(dokan_options, &nandroid_operations, &instance);
			switch (status)
			{
				case DOKAN_SUCCESS:
					logger.info("successfully mounted device to {}", static_cast<char>(mount_point[0]));
					break;
				case DOKAN_ERROR:
					throw std::runtime_error("Unspecified dokan error");
				case DOKAN_DRIVE_LETTER_ERROR:
					throw std::runtime_error("Invalid drive letter");
				case DOKAN_DRIVER_INSTALL_ERROR:
					throw std::runtime_error("Failed to install dokan driver");
				case DOKAN_START_ERROR:
					throw std::runtime_error("Internal driver error");
				case DOKAN_MOUNT_ERROR:
					// Move to the next letter... if there is a next letter.
					if (mount_point[0] == 'Z') {
						throw std::runtime_error("No valid mount points found! All drive letters must be occupied");
					}
					mount_point[0] += 1;
					logger.debug("trying next drive letter: {}", static_cast<char>(mount_point[0]));
					break;
				case DOKAN_MOUNT_POINT_ERROR:
					throw std::runtime_error("Mount point was invalid");
				case DOKAN_VERSION_ERROR:
					throw std::runtime_error("Incompatible with installed version of dokan");
				default:
					logger.error("unknown error occured, code: {}", status);
					throw std::runtime_error("Unknown error occured");
			}
		}
	}

	void Nandroid::handle_daemon_output(uint8_t* buffer, int length) {
		agent_output_buffer.append(reinterpret_cast<char*>(buffer), length);
		size_t last_newline_idx = -1;
		size_t next_newline_idx;
		// Locate any new-lines within the buffer.
		while ((next_newline_idx = agent_output_buffer.find('\n', last_newline_idx + 1)) != std::string::npos) {
			std::string line = agent_output_buffer.substr(last_newline_idx + 1, next_newline_idx);
			if (line.starts_with(AGENT_READY_MSG)) {
				std::unique_lock lock(mtx_agent_ready);
				agent_ready = true;
				agent_ready_notified = true;
				// Notify the thread calling `begin` that the drive is ready to mount.
				cv_agent_ready.notify_one();
			}

			// Remove extra line endings since the logger adds its own line ending.
			if (line.ends_with('\n')) {
				line.pop_back();
			}
			if (line.ends_with('\r')) {
				line.pop_back();
			}

			agent_logger.debug("{}", line);
			last_newline_idx = next_newline_idx;
		}

		// Trim the buffer to the remaining data after the last newline.
		agent_output_buffer = agent_output_buffer.substr(last_newline_idx + 1);
	}

	Connection& Nandroid::get_conn() {
		if (this->connection) {
			return *this->connection;
		}
		else
		{
			throw std::runtime_error("Attempting to get connection when not yet connected/mounted");
		}
	}

	ContextLogger& Nandroid::get_operations_logger() {
		return operations_logger;
	}
}