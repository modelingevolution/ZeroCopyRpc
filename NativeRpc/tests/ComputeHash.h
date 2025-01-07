#pragma once
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/algorithm/hex.hpp>
#include "SharedMemoryClient.h"

using namespace boost::uuids;

uuid ComputeHash(const unsigned char* data, size_t size);
