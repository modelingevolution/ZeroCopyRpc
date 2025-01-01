#include "ZeroCopyRpcException.h"

ZeroCopyRpcException::ZeroCopyRpcException(const char* message): msg_(message)
{
	
}

const char* ZeroCopyRpcException::what() const noexcept
{
	return msg_.c_str();
}
