#pragma once
#include "TypeDefs.h"
#include "CyclicMemoryPool.hpp"
#include "Export.h"
#include <iostream>


template<unsigned long TSIZE, unsigned long TCAPACITY>
class  CyclicBuffer
{
public:
    struct  Item
    {
        size_t Size;
        ulong Type;
        size_t Offset;
    };
    struct  Accessor
    {
        Item* Item;
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
        Accessor(::CyclicBuffer<TSIZE, TCAPACITY>::Item* item, CyclicBuffer<TSIZE, TCAPACITY>* buffer)
            : Item(item),
            Buffer(buffer)
        {
        }

        inline byte* Get() { return Buffer->_memory.Get(Item->Offset); }
    };
    struct  WriterScope
    {
        CyclicMemoryPool<TSIZE>::Span Span;
        unsigned long Type;
        ~WriterScope()
        {
            auto written = Span.CommitedSize();
            if (written > 0)
            {
                _parent->_items[_parent->_nextIndex++ % TCAPACITY] = Item{ written, Type, Span.StartOffset() };
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
        WriterScope(CyclicMemoryPool<TSIZE>::Span&& span, unsigned long type, CyclicBuffer<TSIZE, TCAPACITY>* parent)
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
        
        unsigned long Remaining() const { return _parent->_nextIndex - Index; }
        Accessor Data() const
        {
            auto item = &(_parent->_items[(Index - 1) % TCAPACITY]);
            return Accessor(item, _parent);
        }
        Cursor(unsigned long index, CyclicBuffer<TSIZE, TCAPACITY>* parent)
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
            if (diff > 0)
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
    Cursor ReadNext()
    {
        return ReadNext(_nextIndex);
    }
    template<typename T, typename... Args>
    void Write(ulong type, Args&&... args) {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
        auto scope = this->WriteScope(sizeof(T), type);
        auto& span = scope.Span;
        auto ptr = new (span.Start) T(std::forward<Args>(args)...);
        span.Commit(sizeof(T));
    }
    Cursor ReadNext(ulong nextValue)
    {
        return Cursor(nextValue, this);
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
private:
    std::atomic<ulong> _nextIndex = 0;

    // We constantly check if there is enough memory in the CyclicMemoryPool for all the Items in the ring.
    std::atomic<ulong> _messageQueueItemsSize;

    CyclicMemoryPool<TSIZE> _memory;
    Item _items[TCAPACITY];

};

