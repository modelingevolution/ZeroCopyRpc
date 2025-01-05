#include "TestFrame.h"

TestFrame::TestFrame(size_t count): Header( new TestFrameHeader(count)), Data(new byte[count]), _external(false)
{
	Initialize(count);
}
// Will compute stuff
TestFrame::TestFrame(byte* buffer, size_t count) : _external(true)
{
	Header = new (buffer)TestFrameHeader(count);
	Data = buffer + sizeof(TestFrameHeader);
	Initialize(Header->Size);
}

// created out of external memory.
TestFrame::TestFrame(TestFrameHeader* header, byte* data): Header(header), Data(data), _external(true)
{
}

boost::uuids::uuid TestFrame::ComputeHash() const
{
	if (Data == nullptr) return uuid();
	return ComputeHash(Data, Header->Size);
}

std::chrono::milliseconds TestFrame::Age() const
{
	auto now = std::chrono::steady_clock::now();
	return std::chrono::duration_cast<std::chrono::milliseconds>(now - Header->Created);
}

size_t TestFrame::SizeOf(size_t count)
{
	return sizeof(TestFrameHeader) + count;
}



TestFrame::~TestFrame()
{
	if (!_external) {
		delete[] Data;
		Data = nullptr;
		delete Header;
		Header = nullptr;
	}
}

void TestFrame::Initialize(size_t count)
{
	static std::mt19937_64 rng(std::random_device{}());
	std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);

	// Fill Data with random 64-bit chunks
	uint64_t* dataAsUlong = reinterpret_cast<uint64_t*>(Data);
	size_t numChunks = count / sizeof(uint64_t);

	for (size_t i = 0; i < numChunks; ++i) {
		dataAsUlong[i] = dist(rng);
	}

	Header->Hash = ComputeHash(Data, count);
}

boost::uuids::uuid TestFrame::ComputeHash(const unsigned char* data, size_t size)
{
	// Create an MD5 object
	boost::uuids::detail::md5 hash;
	// Process the data
	hash.process_bytes(data, size);
	// Retrieve the hash result
	boost::uuids::detail::md5::digest_type digest;
	hash.get_digest(digest);
	// Convert the digest to a UUID
	uuid result;
	memcpy(&result, &digest, sizeof(digest));
	return result;
}

std::ostream& operator<<(std::ostream& os, const TestFrame& frame)
{
	std::ostringstream oss;
	oss << boost::uuids::to_string(frame.Header->Hash) << " [" << frame.Header->Size << "B]";
	os << oss.str();
	return os;
}
