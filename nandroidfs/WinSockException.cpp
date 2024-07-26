#include "WinSockException.hpp"
#include <format>

namespace nandroidfs {
	WinSockException::WinSockException(int err_code) : std::exception() {
		this->err_code = err_code;
		this->msg = std::format("Failed to operate on socket: {}", err_code);
	}

	const char* WinSockException::what() const throw() {
		return msg.c_str();
	}

	int WinSockException::get_err_code() {
		return this->err_code;
	}

	void throw_if_nonzero(int return_value)
	{
		if (return_value != 0) {
			throw WinSockException(return_value);
		}
	}
}