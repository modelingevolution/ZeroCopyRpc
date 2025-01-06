
#pragma once
#include <cstdint>

#include "Export.h"
#include <chrono> // For std::chrono

struct EXPORT UdpReplicationMessageHeader {
    uint64_t Created;
    uint32_t Size;
    uint16_t Sequence;
    uint8_t Type;

    UdpReplicationMessageHeader();

    UdpReplicationMessageHeader(uint64_t created, uint32_t size, uint16_t sequence, uint8_t type)
	    : Created(created),
	      Size(size),
	      Sequence(sequence),
	      Type(type)
    {
    }
    UdpReplicationMessageHeader(uint32_t size, uint8_t type, uint16_t sequence=0);
};