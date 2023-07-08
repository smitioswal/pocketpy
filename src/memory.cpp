#include "pocketpy/memory.h"

#include <limits.h>
#include <string.h>
#include <stddef.h>

namespace pkpy{

static const int b2_chunkSize = 16 * 1024;
static const int b2_maxBlockSize = 640;
static const int b2_chunkArrayIncrement = 128;

// These are the supported object sizes. Actual allocations are rounded up the next size.
static const int b2_blockSizes[b2_blockSizeCount] =
{
	16,		// 0
	32,		// 1
	64,		// 2
	96,		// 3
	128,	// 4
	160,	// 5
	192,	// 6
	224,	// 7
	256,	// 8
	320,	// 9
	384,	// 10
	448,	// 11
	512,	// 12
	640,	// 13
};

// This maps an arbitrary allocation size to a suitable slot in b2_blockSizes.
struct b2SizeMap
{
	b2SizeMap()
	{
		int j = 0;
		values[0] = 0;
		for (int i = 1; i <= b2_maxBlockSize; ++i)
		{
			PK_DEBUG_ASSERT(j < b2_blockSizeCount);
			if (i <= b2_blockSizes[j])
			{
				values[i] = (uint8_t)j;
			}
			else
			{
				++j;
				values[i] = (uint8_t)j;
			}
		}
	}

	uint8_t values[b2_maxBlockSize + 1];
};

static const b2SizeMap b2_sizeMap;

struct b2Chunk
{
	int blockSize;
	b2Block* blocks;
};

struct b2Block
{
	b2Block* next;
};

b2BlockAllocator::b2BlockAllocator()
{
	PK_DEBUG_ASSERT(b2_blockSizeCount < UCHAR_MAX);

	m_chunkSpace = b2_chunkArrayIncrement;
	m_chunkCount = 0;
	m_chunks = (b2Chunk*)malloc(m_chunkSpace * sizeof(b2Chunk));
	
	memset(m_chunks, 0, m_chunkSpace * sizeof(b2Chunk));
	memset(m_freeLists, 0, sizeof(m_freeLists));
}

b2BlockAllocator::~b2BlockAllocator()
{
	for (int i = 0; i < m_chunkCount; ++i)
	{
		free(m_chunks[i].blocks);
	}

	free(m_chunks);
}

void* b2BlockAllocator::Allocate(int size)
{
	if (size == 0)
	{
		return nullptr;
	}

	PK_DEBUG_ASSERT(0 < size);

	if (size > b2_maxBlockSize)
	{
		return malloc(size);
	}

	int index = b2_sizeMap.values[size];
	PK_DEBUG_ASSERT(0 <= index && index < b2_blockSizeCount);

	if (m_freeLists[index])
	{
		b2Block* block = m_freeLists[index];
		m_freeLists[index] = block->next;
		return block;
	}
	else
	{
		if (m_chunkCount == m_chunkSpace)
		{
			b2Chunk* oldChunks = m_chunks;
			m_chunkSpace += b2_chunkArrayIncrement;
			m_chunks = (b2Chunk*)malloc(m_chunkSpace * sizeof(b2Chunk));
			memcpy(m_chunks, oldChunks, m_chunkCount * sizeof(b2Chunk));
			memset(m_chunks + m_chunkCount, 0, b2_chunkArrayIncrement * sizeof(b2Chunk));
			free(oldChunks);
		}

		b2Chunk* chunk = m_chunks + m_chunkCount;
		chunk->blocks = (b2Block*)malloc(b2_chunkSize);
#if PK_DEBUG_EXTRA_CHECK
		memset(chunk->blocks, 0xcd, b2_chunkSize);
#endif
		int blockSize = b2_blockSizes[index];
		chunk->blockSize = blockSize;
		int blockCount = b2_chunkSize / blockSize;
		PK_DEBUG_ASSERT(blockCount * blockSize <= b2_chunkSize);
		for (int i = 0; i < blockCount - 1; ++i)
		{
			b2Block* block = (b2Block*)((int8_t*)chunk->blocks + blockSize * i);
			b2Block* next = (b2Block*)((int8_t*)chunk->blocks + blockSize * (i + 1));
			block->next = next;
		}
		b2Block* last = (b2Block*)((int8_t*)chunk->blocks + blockSize * (blockCount - 1));
		last->next = nullptr;

		m_freeLists[index] = chunk->blocks->next;
		++m_chunkCount;

		return chunk->blocks;
	}
}

void b2BlockAllocator::Free(void* p, int size)
{
	if (size == 0)
	{
		return;
	}

	PK_DEBUG_ASSERT(0 < size);

	if (size > b2_maxBlockSize)
	{
		free(p);
		return;
	}

	int index = b2_sizeMap.values[size];
	PK_DEBUG_ASSERT(0 <= index && index < b2_blockSizeCount);

#if PK_DEBUG_EXTRA_CHECK
	// Verify the memory address and size is valid.
	int blockSize = b2_blockSizes[index];
	bool found = false;
	for (int i = 0; i < m_chunkCount; ++i)
	{
		b2Chunk* chunk = m_chunks + i;
		if (chunk->blockSize != blockSize)
		{
			PK_DEBUG_ASSERT(	(int8*)p + blockSize <= (int8*)chunk->blocks ||
						(int8*)chunk->blocks + b2_chunkSize <= (int8*)p);
		}
		else
		{
			if ((int8*)chunk->blocks <= (int8*)p && (int8*)p + blockSize <= (int8*)chunk->blocks + b2_chunkSize)
			{
				found = true;
			}
		}
	}

	PK_DEBUG_ASSERT(found);

	memset(p, 0xfd, blockSize);
#endif

	b2Block* block = (b2Block*)p;
	block->next = m_freeLists[index];
	m_freeLists[index] = block;
}

void b2BlockAllocator::Clear()
{
	for (int i = 0; i < m_chunkCount; ++i)
	{
		free(m_chunks[i].blocks);
	}

	m_chunkCount = 0;
	memset(m_chunks, 0, m_chunkSpace * sizeof(b2Chunk));
	memset(m_freeLists, 0, sizeof(m_freeLists));
}

static b2BlockAllocator g_blockAllocator;

void* pool_alloc(int size)
{
    PK_GLOBAL_SCOPE_LOCK();
    return g_blockAllocator.Allocate(size);
}

void pool_free(void* p, int size)
{
    PK_GLOBAL_SCOPE_LOCK();
    g_blockAllocator.Free(p, size);
}

};  // namespace pkpy