#pragma once
#include <random>
#include <boost/uuid/uuid.hpp>

#include "SharedMemoryClient.h"
#include "Export.h"

struct EXPORT TestFrameHeader
{
	boost::uuids::uuid Hash;
	std::chrono::steady_clock::time_point Created; // Point in time when the frame is created
	size_t Size;
	TestFrameHeader(size_t size) : Created(std::chrono::steady_clock::now()), Size(size){}
};
struct EXPORT TestFrame
{
	TestFrameHeader* Header;
	byte* Data;
	
	
	TestFrame(size_t count);
	TestFrame(byte *buffer, size_t count);
	TestFrame(TestFrameHeader *header, byte* data);
	void Initialize();
	boost::uuids::uuid ComputeHash() const;
	friend EXPORT std::ostream& operator<<(std::ostream& os, const TestFrame& frame);
	std::chrono::milliseconds Age() const;
	static size_t SizeOf(size_t count);
	TestFrame(const TestFrame& other) = delete;

	~TestFrame();

private:
	void Initialize(size_t count);
	bool _external;
	static boost::uuids::uuid ComputeHash(const unsigned char* data, size_t size);
};

