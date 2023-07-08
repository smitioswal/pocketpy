#pragma once

#include "common.h"

namespace pkpy{

const int b2_blockSizeCount = 14;

struct b2Block;
struct b2Chunk;

/// This is a small object allocator used for allocating small
/// objects that persist for more than one time step.
/// See: http://www.codeproject.com/useritems/Small_Block_Allocator.asp
class b2BlockAllocator
{
public:
	b2BlockAllocator();
	~b2BlockAllocator();

	/// Allocate memory. This will use b2Alloc if the size is larger than b2_maxBlockSize.
	void* Allocate(int size);

	/// Free memory. This will use b2Free if the size is larger than b2_maxBlockSize.
	void Free(void* p, int size);

	void Clear();

private:

	b2Chunk* m_chunks;
	int m_chunkCount;
	int m_chunkSpace;

	b2Block* m_freeLists[b2_blockSizeCount];
};

void* pool_alloc(int size);
void pool_dealloc(void* ptr);

template<typename T>
void* pool_alloc() {
    return pool_alloc(sizeof(T));
}

template <typename T>
struct shared_ptr {
    int* counter;

    T* _t() const noexcept { return (T*)(counter + 1); }
    void _inc_counter() { if(counter) ++(*counter); }
    void _dec_counter() { if(counter && --(*counter) == 0) {((T*)(counter + 1))->~T(); pool_dealloc(counter);} }

public:
    shared_ptr() : counter(nullptr) {}
    shared_ptr(int* counter) : counter(counter) {}
    shared_ptr(const shared_ptr& other) : counter(other.counter) {
        _inc_counter();
    }
    shared_ptr(shared_ptr&& other) noexcept : counter(other.counter) {
        other.counter = nullptr;
    }
    ~shared_ptr() { _dec_counter(); }

    bool operator==(const shared_ptr& other) const { return counter == other.counter; }
    bool operator!=(const shared_ptr& other) const { return counter != other.counter; }
    bool operator<(const shared_ptr& other) const { return counter < other.counter; }
    bool operator>(const shared_ptr& other) const { return counter > other.counter; }
    bool operator<=(const shared_ptr& other) const { return counter <= other.counter; }
    bool operator>=(const shared_ptr& other) const { return counter >= other.counter; }
    bool operator==(std::nullptr_t) const { return counter == nullptr; }
    bool operator!=(std::nullptr_t) const { return counter != nullptr; }

    shared_ptr& operator=(const shared_ptr& other) {
        _dec_counter();
        counter = other.counter;
        _inc_counter();
        return *this;
    }

    shared_ptr& operator=(shared_ptr&& other) noexcept {
        _dec_counter();
        counter = other.counter;
        other.counter = nullptr;
        return *this;
    }

    T& operator*() const { return *_t(); }
    T* operator->() const { return _t(); }
    T* get() const { return _t(); }

    int use_count() const { 
        return counter ? *counter : 0;
    }

    void reset(){
        _dec_counter();
        counter = nullptr;
    }
};

template <typename T, typename... Args>
shared_ptr<T> make_sp(Args&&... args) {
    int* p = (int*)pool_alloc(sizeof(int) + sizeof(T));
    *p = 1;
    new(p+1) T(std::forward<Args>(args)...);
    return shared_ptr<T>(p);
}

};  // namespace pkpy
