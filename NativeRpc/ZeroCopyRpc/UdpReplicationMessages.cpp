#include "UdpReplicationMessages.h"

UdpReplicationMessageHeader::UdpReplicationMessageHeader(): UdpReplicationMessageHeader(0,0,0,0)
{  }

UdpReplicationMessageHeader::UdpReplicationMessageHeader(uint32_t size, uint8_t type, uint16_t sequence): Created(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
		.count())),
	Size(size),
	Sequence(sequence),
	Type(type)
{
}
