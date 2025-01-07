#pragma once

#include <boost/asio.hpp>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <cstring>
#include <cstdint>
#include <array>
#include <boost/asio.hpp>
#include "UdpReplicationMessages.h"
#include <boost/asio/buffer.hpp>
#include <algorithm>


using namespace boost;
using namespace boost::asio;
//constexpr size_t UDP_MTU = 9000; // Jumbo frame MTU size
constexpr size_t HEADER_SIZE = sizeof(UdpReplicationMessageHeader);

//template <class _Ty>
//constexpr const _Ty& min_i(const _Ty& _Left, const _Ty& _Right) noexcept(noexcept(_Right < _Left)) {
//    return _Right < _Left ? _Right : _Left;
//}

// An iterator that is used when a udp frame needs be fragmented. Given we have buffer that contain the message (message-type), we need to create an
// efficient iterator that would return all required frames. The UdpFrameIterator will work on typical MTU 1500 and jumbo MTU = 9KB.
template<size_t UDP_MTU>
class UdpFrameIterator {
public:
    const size_t PAYLOAD_SIZE = UDP_MTU - HEADER_SIZE;

    UdpFrameIterator(const uint8_t* buffer, size_t size, uint8_t type, uint64_t created)
        :
		_header(created,size,0,type),
		buffer_(buffer),
		offset_(0) {
    }

    // Dereference operator to get the current fragment
    std::array<const_buffer, 2> operator*() const {
        if (offset_ >= _header.Size)
            throw std::out_of_range("Iterator out of range");

        size_t chunkSize = std::min<size_t>(PAYLOAD_SIZE, _header.Size - offset_);

        // Return the header and payload as a pair of buffers
        return {boost::asio::const_buffer(&_header, HEADER_SIZE), boost::asio::const_buffer(buffer_ + offset_, chunkSize) };
    }

    // Pre-increment operator
    UdpFrameIterator& operator++() {
        if (offset_ >= _header.Size)
            throw std::out_of_range("Iterator cannot be incremented past the end");

        size_t chunkSize = std::min<size_t>(PAYLOAD_SIZE, _header.Size - offset_);
        offset_ += chunkSize;
        ++_header.Sequence;
        return *this;
    }
    bool CanRead() const {
        return offset_ < _header.Size;
    }
    

    // Equality comparison
    bool operator==(const UdpFrameIterator& other) const {
        return buffer_ == other.buffer_ && offset_ == other.offset_;
    }

    // Inequality comparison
    bool operator!=(const UdpFrameIterator& other) const {
        return !(*this == other);
    }

    // Begin iterator
    static UdpFrameIterator Begin(const uint8_t* buffer, size_t size, uint8_t type, uint64_t created) {
        return UdpFrameIterator(buffer, size, type, created);
    }

    // End iterator
    static UdpFrameIterator End(const uint8_t* buffer, size_t size, uint8_t type, uint64_t created) {
        UdpFrameIterator it(buffer, size, type, created);
        it.offset_ = size; // Mark as end
        return it;
    }

private:
    
    UdpReplicationMessageHeader _header;
    const uint8_t* buffer_;
    size_t offset_;
};