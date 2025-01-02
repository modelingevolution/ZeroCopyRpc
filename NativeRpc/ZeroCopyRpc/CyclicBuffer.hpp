#pragma once
#include "TypeDefs.h"
#include "CyclicMemoryPool.hpp"
#include "Export.h"
#include <iostream>



class  CyclicBuffer
{
public:
    struct  Entry
    {
        size_t Size;
        ulong Type;
        size_t Offset;
    };
    struct  Accessor
    {
        Entry* Item;
        CyclicBuffer* Buffer;
        template<typename T>
        T* As() const
        {
            return (T*)(Buffer->_memory.Get(Item->Offset));
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

        inline byte* Get() { return Buffer->_memory.Get(Item->Offset); }
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
                _parent->_items[_parent->_nextIndex++ % _parent->_capacity] = Entry{ written, Type, Span.StartOffset() };
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
        unsigned long Index;
        
        unsigned long Remaining() const { return _parent->_nextIndex - Index - 1; }
        Accessor Data() const
        {
            auto item = &(_parent->_items[(Index) % _parent->_capacity]);
            return Accessor(item, _parent);
        }
        Cursor(unsigned long index, CyclicBuffer* parent)
            : Index(index),
            _parent(parent)
        {
        }
        // Disallow copying
        Cursor(const Cursor&) = delete;
        bool TryRead()
        {
            //std::cout << "CLIENT: TryRead, parent->nextIndex: " << _parent->_nextIndex << " Cursor.Index: " << Index << std::endl;
            auto diff = _parent->_nextIndex - Index;
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
        return OpenCursor(_nextIndex);
    }
    template<typename T, typename... Args>
    void Write(ulong type, Args&&... args) {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
        auto scope = this->WriteScope(sizeof(T), type);
        auto& span = scope.Span;
        auto ptr = new (span.Start) T(std::forward<Args>(args)...);
        span.Commit(sizeof(T));
    }
    Cursor OpenCursor(ulong at)
    {
        return Cursor(at-1, this);
    }
    WriterScope WriteScope(ulong minSize, ulong type)
    {
        auto span = _memory.GetWriteSpan(minSize);
        return WriterScope(std::move(span), type, this); // Explicitly use std::move for the span
    }
    ulong NextIndex() const
    {
        return _nextIndex;
    }

    CyclicBuffer(unsigned long capacity, unsigned long size) :
        _external(false), _capacity(capacity), _memory(size)
    {
        _items = new Entry[capacity];
    }
    CyclicBuffer(byte* buffer, unsigned long capacity, unsigned long size) :
	_external(true), _capacity(capacity), _items(nullptr),
	_memory(buffer + MemoryPoolOffset(capacity), size)
    {
        auto ptr = buffer + sizeof(CyclicBuffer);
        _items =  new (ptr) Entry[capacity];
    }
    static size_t MemoryPoolOffset(unsigned long capacity) { return sizeof(CyclicBuffer) + capacity * sizeof(Entry); }
    static size_t SizeOf(unsigned long capacity, unsigned long size)
    {
        return capacity * sizeof(Entry) + sizeof(CyclicBuffer) + CyclicMemoryPool::SizeOf(size);
    }
    ~CyclicBuffer()
    {
	    if(!_external)
	    {
            delete[] _items;
	    }
    }
private:
    std::atomic<ulong> _nextIndex = 0;
    bool _external;
    unsigned long _capacity;
    Entry* _items;
    CyclicMemoryPool _memory;
};

