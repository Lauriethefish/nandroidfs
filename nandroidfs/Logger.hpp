#pragma once

#include <iostream>
#include <format>
#include <vector>
#include <optional>
#include <mutex>

// Basic logging classes, with some simple support for logging contect via a `context` string.
// Not a fully structured logger since this is overkill.
namespace nandroidfs {
	enum class LogLevel {
		Trace,
		Debug,
		Info,
		Warn,
		Error
	};

	class LogSink {
	public:
		// Sends a log message (as a single line).
		// This may be called at the same time from multiple threads.
		// The implementor may choose to use a lock_guard on the contents of this method
		// and block/wait for IO to complete or they could buffer log messages and do the IO on another thread.
		virtual void log(LogLevel level, std::string_view context, std::string message) = 0;
	};

	// Sends log messages to stdout.
	class StdOutSink : public LogSink {
	public:
		virtual void log(LogLevel level, std::string_view context, std::string message);
	private:
		// Gets the name of the specified log level.
		const std::string_view get_log_level_name(LogLevel level);

		std::mutex mutex;
	};

	class ContextLogger {
	public:
		inline ContextLogger(std::shared_ptr<LogSink> log_sink, std::string context, LogLevel min_level) : 
			min_level(min_level), log_sink(log_sink), context(context) { }


		// Creates a new ContextLoggger with the specified string appendeed to the previous context
		// If `new_min_level` is nullopt, the new logger will have the same minimum level as the current logger.
		// Otherwise, `new_min_level` sets the minimum level of the new logger.
		ContextLogger with_context(std::string_view new_context, std::optional<LogLevel> new_min_level = std::nullopt);

		// The same as the above method but this method will not add to the context, but simply replace all existing context with what's given.
		ContextLogger replace_context(std::string context, std::optional<LogLevel> new_min_level = std::nullopt);

		template <typename ...Args> void log(LogLevel level, std::format_string<Args...> fmt_str, Args&&... args) {
			if ((int)level >= (int)min_level) {
				std::string log_msg = std::format(fmt_str, std::forward<Args>(args)...);
				log_sink->log(level, context, log_msg);
			}
		}

		template <typename ...Args> void trace(std::format_string<Args...> fmt_str, Args&&... args) {
			log(LogLevel::Trace, fmt_str, std::forward<Args>(args)...);
		}

		template <typename ...Args> void debug(std::format_string<Args...> fmt_str, Args&&... args) {
			log(LogLevel::Debug, fmt_str, std::forward<Args>(args)...);
		}

		template <typename ...Args> void info(std::format_string<Args...> fmt_str, Args&&... args) {
			log(LogLevel::Info, fmt_str, std::forward<Args>(args)...);
		}

		template <typename ...Args> void warn(std::format_string<Args...> fmt_str, Args&&... args) {
			log(LogLevel::Warn, fmt_str, std::forward<Args>(args)...);
		}

		template <typename ...Args> void error(std::format_string<Args...> fmt_str, Args&&... args) {
			log(LogLevel::Error, fmt_str, std::forward<Args>(args)...);
		}

	private:
		LogLevel min_level;
		std::shared_ptr<LogSink> log_sink;
		// The current logger context.
		std::string context;
	};
}