#include "Logger.hpp"

namespace nandroidfs {
	void StdOutSink::log(LogLevel level, std::string_view context, std::string message) {
		std::lock_guard guard(mutex);

		// TODO: Buffer log messages and actually log them on a separate thread that does IO
		// This should reduce logging overhead.
		std::cout << "[" << get_log_level_name(level) << "(" << context << ")] " << message << std::endl;
	}

	const std::string_view StdOutSink::get_log_level_name(LogLevel level) {
		switch (level) {
		case LogLevel::Trace:
			return "TCE";
		case LogLevel::Debug:
			return "DBG";
		case LogLevel::Info:
			return "INF";
		case LogLevel::Warn:
			return "WAR";
		case LogLevel::Error:
			return "ERR";
		default:
			return "UNK"; // Unknown log level.
		}
	}

	ContextLogger ContextLogger::with_context(std::string_view new_context, std::optional<LogLevel> new_min_level) {
		std::string new_ctx_string = context;
		new_ctx_string.append(", ");
		new_ctx_string.append(new_context);

		return ContextLogger(log_sink, new_ctx_string, new_min_level.value_or(min_level));
	}

	ContextLogger ContextLogger::replace_context(std::string context, std::optional<LogLevel> new_min_level) {
		return ContextLogger(log_sink, context, new_min_level.value_or(min_level));
	}
}