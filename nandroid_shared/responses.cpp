#include "responses.hpp"


namespace nandroidfs
{
	void FileStat::write(DataWriter& writer) {
		writer.write_u16(mode);
		writer.write_u64(size);
		writer.write_u64(access_time);
		writer.write_u64(write_time);
	}

	FileStat::FileStat() {
		this->mode = 0;
		this->size = 0;
		this->access_time = 0;
		this->write_time = 0;
	}

	FileStat::FileStat(uint16_t mode, uint64_t size, uint64_t access_time, uint64_t write_time) {
		this->mode = mode;
		this->size = size;
		this->access_time = access_time;
		this->write_time = write_time;
	}

	FileStat::FileStat(DataReader& reader) {
		mode = reader.read_u16();
		size = reader.read_u64();
		access_time = reader.read_u64();
		write_time = reader.read_u64();
	}

	DiskStats::DiskStats() {
		this->free_bytes = 0;
		this->available_bytes = 0;
		this->total_bytes = 0;
	}

	DiskStats::DiskStats(uint64_t free_bytes, uint64_t available_bytes, uint64_t total_bytes) {
		this->free_bytes = free_bytes;
		this->available_bytes = available_bytes;
		this->total_bytes = total_bytes;
	}

	DiskStats::DiskStats(DataReader& reader) {
		free_bytes = reader.read_u64();
		available_bytes = reader.read_u64();
		total_bytes = reader.read_u64();
	}

	void DiskStats::write(DataWriter& writer) {
		writer.write_u64(free_bytes);
		writer.write_u64(available_bytes);
		writer.write_u64(total_bytes);
	}
}