#pragma once
#include "TypeDefs.h"

class CyclicMemoryPool
{
private:
    struct State;

    State* _state;
    byte* _buffer;
    bool _external;
    size_t *_offset;
    unsigned long* _size;
    std::atomic<bool>* _inUse;

    size_t Remaining() const { return *_size - *_offset; }

    struct State
    {
        size_t _offset;
        unsigned long _size;
        std::atomic<bool> _inUse;
        State(size_t size): _offset(0), _size(size), _inUse(false) {  }
    };
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
            (*_parent->_offset) += size;  // Move parent pointer forward
        }

        ~Span() {
            if (_parent) {
                _parent->_inUse->store(false);  // Release the lock
            }
        }

    private:
        CyclicMemoryPool* _parent;
        size_t _committed;
    };


   
    static size_t SizeOf(unsigned long size)
    {
        return sizeof(CyclicMemoryPool) + sizeof(State) + size;
    }
    ~CyclicMemoryPool()
    {
	    if(!_external)
	    {
            delete[] _buffer;
            delete _state;
            _state = nullptr;
            _buffer = nullptr;
	    }
    }
    /// <summary>
    /// Returns true, if the collection had been locked.
    /// </summary>
    /// <returns></returns>
    bool Unlock()
    {
        bool desired = false;
        bool t = true;
        return _inUse->compare_exchange_weak(t, desired);
    }
    // WHen we initialize structures;
    CyclicMemoryPool(byte* externalBuffer, size_t size) : _state(new ((byte*)externalBuffer) State(size)),
														 _buffer((byte*)(externalBuffer + sizeof(State))),
                                                         _external(true),
														 _offset(&_state->_offset),
														 _size(&_state->_size),
														 _inUse(&_state->_inUse) {
        
    }
    // When won't initialize;
    CyclicMemoryPool(byte* buffer) : _state((State*)buffer),
        _buffer((byte*)(buffer + sizeof(State))),
        _external(true),
        _offset(&_state->_offset), _size(&_state->_size), _inUse(&_state->_inUse) {

    }
    
    CyclicMemoryPool(size_t size) :
		_state(new State(size)),
        _buffer(nullptr),
        _external(false), _offset(&_state->_offset), _size(&_state->_size), _inUse(&_state->_inUse) {
        _buffer = new byte[*_size];
    }
    byte* Get(size_t offset)
    {
        return _buffer + offset;
    }
    template<typename T>
	T* GetAs(size_t offset)
    {
        return (T*)(_buffer + offset);
    }
    size_t Size() const { return *_size; }
    byte* End() { return _buffer + *_offset; }

    template<typename T, typename... Args>
    T* Write(Args&&... args)
    {
        auto scope = this->GetWriteSpan(sizeof(T));
        auto ptr = new (scope.Start) T(std::forward<Args>(args)...);
        scope.Commit(sizeof(T));
        return ptr;
    }

    Span GetWriteSpan(size_t minSize) {
        if (minSize > *_size) {
            throw std::runtime_error("Requested size exceeds buffer capacity.");
        }

        size_t freeSpace = Remaining();
        bool expected = false;

        // Try to acquire the lock
        if (!_inUse->compare_exchange_strong(expected, true)) {
            throw std::runtime_error("Buffer is already in use.");
        }

        // Check if there is enough space, or reset the pointer to reuse the buffer
        if (freeSpace < minSize) {
            *_offset = 0;
            freeSpace = *_size;
        }

        return Span(this->End(), freeSpace, this);
    }
};


