#pragma once

#include "responses.hpp"
#include <chrono>
#include <unordered_map>
#include <shared_mutex>
#include <string>

namespace nandroidfs {
	typedef std::chrono::milliseconds ms_duration;

	// Statistics about how well the cache is functioning to reduce stat requests to the daemon.
	struct CacheStatistics {
		size_t total_cache_hits;
		size_t total_stats_fetched;
	};

	// A cache for file stats used to reduce requests being send to the nandroid daemon.
	// This class is thread safe.
	class StatCache {
	public:
		StatCache(ms_duration cache_scan_interval,
			ms_duration cached_stat_valid_for);

		// Gets the cached file stat for the given file, or nullopt if there is none.
		std::optional<FileStat> get_cached_stat(std::string& file_path);

		// Adds a cached stat for the file with the given path.
		void cache_stat(std::string file_path, FileStat stat);

		// ...something something 2 hard problems...
		void invalidate_stat(std::string& file_path);

		CacheStatistics get_cache_statistics();

	private:
		ms_duration cache_scan_interval;
		ms_duration cached_stat_valid_for;
		std::atomic_size_t total_cache_hits;
		std::atomic_size_t total_stats_fetched;

		// Removes any cache entries that are out of date.
		// I.e. frees memory.
		// Does not lock, caller must lock.
		void clean_cache();

		// The last time at which the cache was scanned to remove outdated entries
		// Outdated entries are removed if they are fetched, but it is also useful to manually remove them every few seconds
		// This avoids the cache growing in size forever.
		std::chrono::time_point<std::chrono::steady_clock> last_cache_scan;

		struct CachedStat {
			FileStat stat;
			std::chrono::time_point<std::chrono::steady_clock> fetched_at;
		};

		std::unordered_map<std::string, CachedStat> cached_stats;
		std::shared_mutex cache_mutex;
	};
}