#include "StatCache.hpp"
#include <iostream>

namespace nandroidfs {
	StatCache::StatCache(ms_duration cache_scan_interval,
		ms_duration cached_stat_valid_for) {
		this->cache_scan_interval = cache_scan_interval;
		this->cached_stat_valid_for = cached_stat_valid_for;
		this->last_cache_scan = std::chrono::steady_clock::now();
	}

	CacheStatistics StatCache::get_cache_statistics() {
		CacheStatistics ret;
		ret.total_cache_hits = total_cache_hits.load();
		ret.total_stats_fetched = total_stats_fetched.load();

		return ret;
	}

	std::optional<FileStat> StatCache::get_cached_stat(std::string& file_path) {
		std::shared_lock lock(cache_mutex);
		total_stats_fetched++;

		if (cached_stats.contains(file_path)) {
			auto now = std::chrono::steady_clock::now();

			CachedStat& cached_stat = cached_stats[file_path];
			if ((now - cached_stat.fetched_at) <= cached_stat_valid_for) {
				total_cache_hits++;
				//std::cout << "Using cached stat for " << file_path << std::endl;
				return cached_stat.stat;
			}
			else
			{
				//std::cout << "Stat out of date for " << file_path << std::endl;
				cached_stats.erase(file_path);
			}
		}

		return std::nullopt;
	}

	void StatCache::cache_stat(std::string file_path, FileStat stat) {
		std::unique_lock lock(cache_mutex);
		auto now = std::chrono::steady_clock::now();

		//std::cout << "Adding cached stat for " << file_path << std::endl;
		CachedStat cached_stat;
		cached_stat.fetched_at = now;
		cached_stat.stat = stat;
		cached_stats[file_path] = cached_stat;

		// Check if it is time to clear the cache
		if ((now - last_cache_scan) > cache_scan_interval) {
			last_cache_scan = now;
			clean_cache(); // Safe since we hold a unique lock.
		}
	}

	void StatCache::clean_cache() {
		std::cout << "Cleaning stat cache" << std::endl;
		auto now = std::chrono::steady_clock::now();

		int removed = 0;
		for (auto current = cached_stats.begin(), last = cached_stats.end(); current != last;) {
			CachedStat& cached_stat = current->second;
			// If a CachedStat is out of date, remove it.
			if ((now - cached_stat.fetched_at) > cached_stat_valid_for) {
				current = cached_stats.erase(current);
				removed++;
			}
			else
			{
				current++;
			}
		}

		std::cout << "Complete - " << removed << " stats removed" << std::endl;
	}

	void StatCache::invalidate_stat(std::string& file_path) {
		// Initially get a shared lock since if there isn't a cached stat for this path,
		// then we will never need to write to the map
		{
			std::shared_lock lock(cache_mutex);

			if (!cached_stats.contains(file_path)) {
				return;
			}
		}

		std::unique_lock unique_lock(cache_mutex);
		cached_stats.erase(file_path);
	}
}