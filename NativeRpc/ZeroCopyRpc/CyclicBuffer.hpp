#pragma once
#include "TypeDefs.h"
#include "CyclicMemoryPool.hpp"
#include "Export.h"
#include <iostream>
#include <boost/log/trivial.hpp>

#include "ZeroCopyRpcException.h"


class  CyclicBuffer
{
public:
    struct Entry;
private:
    struct State
    {
        std::atomic<ulong> _nextIndex;
        std::atomic<ulong> _currentSize;
        unsigned long _capacity;
        
        State(unsigned long capacity) : _capacity(capacity)
        {
        
        }
    };

public:
    struct  Entry
    {
        size_t Size;
        ulong Type;
        size_t Offset;
    };
    struct  Accessor
    {
        Entry* Item = nullptr;
        CyclicBuffer* Buffer = nullptr;
        template<typename T>
        T* As() const
        {
            return (T*)(Buffer->_memory->Get(Item->Offset));
        }
        inline bool IsValid() { return Item != nullptr; }
        inline uint32_t Size() { return Item->Size; }
        inline uint64_t Type() { return Item->Type;  }
        Accessor() : Item(nullptr), Buffer(nullptr)
        {
	        
        }
        Accessor(const Accessor&) = delete;
        Accessor(Accessor&& other) noexcept : Item(other.Item), Buffer(other.Buffer) {
            other.Item = nullptr;
            other.Buffer = nullptr;
        }
        Accessor& operator=(Accessor&& other) noexcept {
            if (this != &other) {
                Item = other.Item;
                Buffer = other.Buffer;
                other.Item = nullptr;
                other.Buffer = nullptr;
            }
            return *this;
        }
        Accessor(::CyclicBuffer::Entry* item, CyclicBuffer* buffer)
            : Item(item),
            Buffer(buffer)
        {
        }

        inline byte* Get() { return Buffer->_memory->Get(Item->Offset); }
    };
    struct  WriterScope
    {
        CyclicMemoryPool::Span Span;
        unsigned long Type;
        ~WriterScope()
        {
            auto written = Span.CommitedSize();
            if (written > 0)
            {
	            auto nxAtm = _parent->_nextIndex;
                ulong nx = nxAtm->load();
            	unsigned long capacity = *_parent->_capacity;
                	            
                long prvSize = nx >= capacity ? _parent->_items[(nx - capacity) % capacity].Size : 0;
                _parent->_state->_currentSize.fetch_add(static_cast<int64_t>(written) - static_cast<int64_t>(prvSize));
                _parent->_items[nx % capacity] = Entry{ written, Type, Span.StartOffset() };
                nxAtm->fetch_add(1);
            }
        }
        WriterScope(const WriterScope&) = delete;
        WriterScope(WriterScope&& other) noexcept
            : Span(std::move(other.Span)),
            Type(other.Type),
            _parent(other._parent)
        {
            other._parent = nullptr;
        }
        WriterScope(CyclicMemoryPool::Span&& span, unsigned long type, CyclicBuffer* parent)
            : Span(std::move(span)), // Move the span
            _parent(parent),
            Type(type)
        {
        }

    private:
        CyclicBuffer* _parent;

    };
    /// <summary>
    /// Cursor can act as a chaser. Once it's returned, it will yield data that will be written, not that had been written.
    /// </summary>
    struct Cursor
    {
        ulong Index;
        
        ulong Remaining() const { return *_parent->_nextIndex - Index - 1; }
        Accessor Data() const
        {
            auto item = &(_parent->_items[(Index) % *_parent->_capacity]);
            return Accessor(item, _parent);
        }
        Cursor(ulong index, CyclicBuffer* parent)
            : Index(index),
            _parent(parent)
        {
        }
        // Disallow copying
        Cursor(const Cursor&) = delete;
        bool TryRead()
        {
            //std::cout << "CLIENT: TryRead, parent->nextIndex: " << _parent->_nextIndex << " Cursor.Index: " << Index << std::endl;
            auto diff = *_parent->_nextIndex - Index;
            if (diff > 1)
            {
                //std::cout << "CLIENT: DIFF is positive, incrementing Index by 1." << std::endl;

                Index += 1;
                return true;
            }
            return false;
        }
        // Allow move semantics
        Cursor(Cursor&& other) noexcept :
            Index(other.Index), _parent(other._parent)
        {
            other.Index = 0ul - 1;
            other._parent = nullptr;
        }
    private:
        CyclicBuffer* _parent;


    };
    Cursor OpenCursor()
    {
        return OpenCursor(*_nextIndex);
    }
    template<typename T, typename... Args>
    void Write(ulong type, Args&&... args) {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
        auto scope = this->WriteScope(sizeof(T), type);
        auto& span = scope.Span;
        auto ptr = new (span.Start) T(std::forward<Args>(args)...);
        span.Commit(sizeof(T));
    }
    // return size all items in the buffer, it is more than the size of the buffer, it means that some items had been overriden.
    ulong BufferItemsSize()
    {
	    return _state->_currentSize.load();
    }
    Cursor OpenCursor(ulong at)
    {
        return Cursor(at-1, this);
    }
    WriterScope WriteScope(ulong minSize, ulong type)
    {
        auto span = _memory->GetWriteSpan(minSize);
        return WriterScope(std::move(span), type, this); // Explicitly use std::move for the span
    }
    ulong NextIndex() const
    {
        return *_nextIndex;
    }

    bool Unlock()
    {
        return _memory->Unlock();
    }
   
    

    CyclicBuffer(unsigned long capacity, unsigned long size) :
		_state(new State(capacity)),
        _external(false),
		_nextIndex(&_state->_nextIndex),
		_capacity(&_state->_capacity),
        _items(new Entry[capacity]),
		_memory(new CyclicMemoryPool(size))
    {
        if (!_nextIndex->is_lock_free())
            throw new ZeroCopyRpcException("Atomic<ulong> is not lock-free.");

        _nextIndex->store(0);
    }
    // Invoked when buffer was already initialized and we need to rebuild local pointers and structures.
    CyclicBuffer(byte* externalBuffer) :
		_state((State*)externalBuffer),
		_external(true),
		_nextIndex(&_state->_nextIndex),
        _capacity(&_state->_capacity),
        _items((Entry*)(externalBuffer + ItemsOffset())),
		_memory(new CyclicMemoryPool(externalBuffer + MemoryPoolOffset(_state->_capacity)))
    {
        if (!_nextIndex->is_lock_free())
            throw new ZeroCopyRpcException("Atomic<ulong> is not lock-free.");
    }
    // Invoked when fresh new memory was allocated and we need to initialize structures
    CyclicBuffer(byte* externalBuffer, unsigned long capacity, unsigned long size) :
        _state(new (externalBuffer) State(capacity)),
		_external(true),
        _nextIndex(&_state->_nextIndex),
		_capacity(&_state->_capacity),
        _items((Entry*)(externalBuffer + ItemsOffset())),
		_memory(new CyclicMemoryPool(externalBuffer + MemoryPoolOffset(capacity), size))
    {
        if (!_nextIndex->is_lock_free())
            throw new ZeroCopyRpcException("Atomic<ulong> is not lock-free.");

        _nextIndex->store(0);
    }
    static size_t ItemsOffset() { return sizeof(State); }
    static size_t MemoryPoolOffset(unsigned long capacity) { return ItemsOffset() + capacity * sizeof(Entry); }
    static size_t SizeOf(unsigned long capacity, unsigned long size)
    {
        return sizeof(State) + capacity * sizeof(Entry) + CyclicMemoryPool::SizeOf(size);
    }
    ~CyclicBuffer()
    {
	    if(!_external)
	    {
            delete[] _items;
            delete _state;
	    }
        delete _memory;
    }
private:
    State* _state;
    bool _external;
    
    std::atomic<ulong>* _nextIndex;
    unsigned long* _capacity; // pointer to capacity in state
    Entry* _items; // pointer to the same memory that is 

    CyclicMemoryPool* _memory;
};



