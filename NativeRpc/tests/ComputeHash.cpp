#include "ComputeHash.h"

uuid ComputeHash(const unsigned char* data, size_t size)
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
	if (sizeof(digest) != sizeof(uuid))
		throw ZeroCopyRpcException("Fuck");
	return result;
}
