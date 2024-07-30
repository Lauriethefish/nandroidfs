#pragma once

#include "responses.hpp"
#include <chrono>
#include <unordered_map>
#include <shared_mutex>
#include <string>
#include <iostream>

namespace nandroidfs {
	typedef std::chrono::milliseconds ms_duration;

	// Statistics about how well the cache is functioning to reduce requests to the daemon.
	struct CacheStatistics {
		size_t total_cache_hits;
		size_t total_data_fetched;
	};

	inline std::ostream& operator << (std::ostream& os, const CacheStatistics& stats) {
		return (os << "Total hits: " << stats.total_cache_hits << ", total requests: " << stats.total_data_fetched << std::endl
			<< "Hit rate: " << (100.0 * stats.total_cache_hits / stats.total_data_fetched) << std::endl);
	}

	// A cache for generic data used to reduce requests being send to the nandroid daemon.
	// This class is thread safe.
	template<typename T>
	class TimedCache {
	private:
		struct CachedData {
			T data;
			std::chrono::time_point<std::chrono::steady_clock> fetched_at;
		};

		ms_duration cache_scan_interval;
		ms_duration cached_item_valid_for;
		std::atomic_size_t total_cache_hits;
		std::atomic_size_t total_data_fetched;

		// The last time at which the cache was scanned to remove outdated entries
		// Outdated entries are removed if they are fetched, but it is also useful to manually remove them every few seconds
		// This avoids the cache growing in size forever.
		std::chrono::time_point<std::chrono::steady_clock> last_cache_scan;

		std::unordered_map<std::string, CachedData> cached_data;
		std::shared_mutex cache_mutex;

		// Removes any cache entries that are out of date.
		// I.e. frees memory.
		// Does not lock, caller must lock.
		void clean_cache() {
			auto now = std::chrono::steady_clock::now();

			int removed = 0;
			for (auto current = cached_data.begin(), last = cached_data.end(); current != last;) {
				CachedData& cached_datum = current->second;
				// If a cached datum is out of date, remove it.
				if ((now - cached_datum.fetched_at) > cached_item_valid_for) {
					current = cached_data.erase(current);
					removed++;
				}
				else
				{
					current++;
				}
			}
		}

	public:
		TimedCache(ms_duration cache_scan_interval,
			ms_duration cached_item_valid_for) {
			this->cache_scan_interval = cache_scan_interval;
			this->cached_item_valid_for = cached_item_valid_for;
			this->last_cache_scan = std::chrono::steady_clock::now();
		}

		// Gets the cached data for the given file, or nullopt if there is none.
		std::optional<T> get_cached(std::string& file_path) {
			std::shared_lock lock(cache_mutex);
			total_data_fetched++;

			if (cached_data.contains(file_path)) {
				auto now = std::chrono::steady_clock::now();

				CachedData& cached = cached_data[file_path];
				if ((now - cached.fetched_at) <= cached_item_valid_for) {
					total_cache_hits++;
					return cached.data;
				}
			}

			return std::nullopt;
		}

		// Adds a cached data for the file with the given path.
		void cache(std::string file_path, T datum) {
			std::unique_lock lock(cache_mutex);
			auto now = std::chrono::steady_clock::now();

			//std::cout << "Adding cached stat for " << file_path << std::endl;
			CachedData cached_datum;
			cached_datum.fetched_at = now;
			cached_datum.data = datum;
			cached_data[file_path] = cached_datum;

			// Check if it is time to clear the cache
			if ((now - last_cache_scan) > cache_scan_interval) {
				last_cache_scan = now;
				clean_cache(); // Safe since we hold a unique lock.
			}
		}

		// ...something something 2 hard problems...
		void invalidate(std::string& file_path) {
			// Initially get a shared lock since if there isn't a cached datum for this path,
			// then we will never need to write to the map
			{
				std::shared_lock lock(cache_mutex);

				if (!cached_data.contains(file_path)) {
					return;
				}
			}

			std::unique_lock unique_lock(cache_mutex);
			cached_data.erase(file_path);
		}

		CacheStatistics get_cache_statistics() {
			CacheStatistics ret;
			ret.total_cache_hits = total_cache_hits.load();
			ret.total_data_fetched = total_data_fetched.load();

			return ret;
		}
	};
}