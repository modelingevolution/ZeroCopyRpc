#pragma once

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/algorithm/hex.hpp>
#include "ComputeHash.h"

template<unsigned int TSize>
struct BigFrame
{
	boost::uuids::uuid Hash;
	unsigned char Data[TSize];
	BigFrame()
	{
		static std::mt19937_64 rng(std::random_device{}());
		std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);

		// Fill Data with random 64-bit chunks
		uint64_t* dataAsUlong = reinterpret_cast<uint64_t*>(Data);
		size_t numChunks = TSize / sizeof(uint64_t);

		for (size_t i = 0; i < numChunks; ++i) {
			dataAsUlong[i] = dist(rng);
		}

		Hash = ComputeHash(Data, TSize);
	}

};