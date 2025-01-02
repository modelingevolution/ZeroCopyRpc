#pragma once
#include "TypeDefs.h"

class CyclicMemoryPool
{
private:
    byte* _buffer;
    size_t _offset;
    unsigned long _size;
    bool _external;
    std::atomic<bool> _inUse;

    size_t Remaining() const { return _size - _offset; }

    
public:

    struct Span {
        byte* Start;
        size_t Size;

        Span(byte* ptr, size_t size, CyclicMemoryPool* parent)
            : Start(ptr), Size(size), _parent(parent), _committed(0) {
        }

        // Disallow copying
        Span(const Span&) = delete;

        // Allow move semantics
        Span(Span&& other) noexcept
            : Start(other.Start), Size(other.Size), _parent(other._parent), _committed(other._committed) {
            other.Start = nullptr;
            other.Size = 0;
            other._parent = nullptr;
        }
        size_t StartOffset() { return Start - _parent->_buffer; }
        size_t EndOffset() { return Start - _parent->_buffer + _committed; }
        size_t CommitedSize() const { return _committed; }
        byte* End() const { return Start + _committed; }
        void Commit(size_t size) {
            if (size > (Size - _committed)) {
                throw std::runtime_error("Commit size exceeds reserved span.");
            }
            _committed += size;
            _parent->_offset += size;  // Move parent pointer forward
        }

        ~Span() {
            if (_parent) {
                _parent->_inUse.store(false);  // Release the lock
            }
        }

    private:
        CyclicMemoryPool* _parent;
        size_t _committed;
    };


   
    static size_t SizeOf(unsigned long size)
    {
        return sizeof(CyclicMemoryPool) + size;
    }
    ~CyclicMemoryPool()
    {
	    if(!_external)
	    {
            delete[] _buffer;
            _buffer = nullptr;
	    }
    }
    CyclicMemoryPool(byte* buffer, unsigned long size) : _size(size),
        _offset(0), _inUse(false), _external(true), _buffer(buffer + sizeof(CyclicMemoryPool)) {

    }
    CyclicMemoryPool(unsigned long size) : _size(size),
         _offset(0), _inUse(false), _buffer(nullptr), _external(false) {
        _buffer = new byte[_size];
    }
    byte* Get(size_t offset)
    {
        return _buffer + offset;
    }
    size_t Size() const { return _size; }
    byte* End() { return _buffer + _offset; }
    Span GetWriteSpan(size_t minSize) {
        if (minSize > _size) {
            throw std::runtime_error("Requested size exceeds buffer capacity.");
        }

        size_t freeSpace = Remaining();
        bool expected = false;

        // Try to acquire the lock
        if (!_inUse.compare_exchange_strong(expected, true)) {
            throw std::runtime_error("Buffer is already in use.");
        }

        // Check if there is enough space, or reset the pointer to reuse the buffer
        if (freeSpace < minSize) {
            _offset = 0;
            freeSpace = _size;
        }

        return Span(this->End(), freeSpace, this);
    }
};