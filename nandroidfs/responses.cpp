#include "responses.hpp"


namespace nandroidfs
{
	void FileStat::write(DataWriter& writer) {
		writer.write_u32(mode);
	}

	FileStat::FileStat(DataReader& reader) {
		mode = reader.read_u32();
	}
}