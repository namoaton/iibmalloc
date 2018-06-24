/* -------------------------------------------------------------------------------
 * Copyright (c) 2018, OLogN Technologies AG
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * -------------------------------------------------------------------------------
 * 
 * Per-thread bucket allocator
 * 
 * v.1.00    May-09-2018    Initial release
 * 
 * -------------------------------------------------------------------------------*/

 
#ifndef SERIALIZABLE_ALLOCATOR_KEEPING_PAGES_H
#define SERIALIZABLE_ALLOCATOR_KEEPING_PAGES_H

#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector> // potentially, a temporary solution

#include "bucket_allocator_common.h"
#include "page_allocator.h"


class SerializableAllocatorBase;
extern thread_local SerializableAllocatorBase g_AllocManager;


constexpr size_t ALIGNMENT = 2 * sizeof(uint64_t);
constexpr uint8_t ALIGNMENT_EXP = sizeToExp(ALIGNMENT);
static_assert( ( 1 << ALIGNMENT_EXP ) == ALIGNMENT, "" );
constexpr size_t ALIGNMENT_MASK = expToMask(ALIGNMENT_EXP);
static_assert( 1 + ALIGNMENT_MASK == ALIGNMENT, "" );

constexpr size_t PAGE_SIZE = 4 * 1024;
constexpr uint8_t PAGE_SIZE_EXP = sizeToExp(PAGE_SIZE);
constexpr size_t PAGE_SIZE_MASK = expToMask(PAGE_SIZE_EXP);
static_assert( ( 1 << PAGE_SIZE_EXP ) == PAGE_SIZE, "" );
static_assert( 1 + PAGE_SIZE_MASK == PAGE_SIZE, "" );


//#define USE_ITEM_HEADER
#define USE_SOUNDING_PAGE_ADDRESS

#ifdef USE_SOUNDING_PAGE_ADDRESS
template<class BasePageAllocator, size_t bucket_cnt_exp, size_t reservation_size_exp, size_t commit_page_cnt_exp>
class SoundingAddressPageAllocator : public BasePageAllocator
{
	static constexpr size_t reservation_size = (1 << reservation_size_exp);
	static constexpr size_t bucket_cnt = (1 << bucket_cnt_exp);
	static_assert( reservation_size_exp >= bucket_cnt_exp + PAGE_SIZE_EXP, "revise implementation" );
	static constexpr size_t pages_per_bucket_exp = reservation_size_exp - bucket_cnt_exp - PAGE_SIZE_EXP;
	static constexpr size_t pages_in_single_commit_exp = (pages_per_bucket_exp >= 1 ? pages_per_bucket_exp - 1 : 0);
	static constexpr size_t pages_in_single_commit = (1 << pages_in_single_commit_exp);
	static constexpr size_t pages_per_bucket = (1 << pages_per_bucket_exp);
	static constexpr size_t commit_page_cnt = (1 << commit_page_cnt_exp);
	static constexpr size_t commit_size = (1 << (commit_page_cnt_exp + PAGE_SIZE_EXP));
	static_assert( commit_page_cnt_exp <= reservation_size_exp - bucket_cnt_exp - PAGE_SIZE_EXP, "value mismatch" );

	struct MemoryBlockHeader
	{
		MemoryBlockListItem block;
		MemoryBlockHeader* next;
	};
//	MemoryBlockHeader* memoryBlockHead = nullptr;
	
	struct PageBlockDescriptor
	{
		PageBlockDescriptor* next = nullptr;
		void* blockAddress = nullptr;
//		size_t usageMask = 0;
		uint16_t nextToUse[ bucket_cnt ];
		uint16_t nextToCommit[ bucket_cnt ];
//		static_assert( sizeof( size_t ) * 8 >= bucket_cnt, "revise implementation" );
		static_assert( UINT16_MAX > pages_per_bucket , "revise implementation" );
	};
	PageBlockDescriptor pageBlockListStart;
	PageBlockDescriptor* pageBlockListCurrent;
	PageBlockDescriptor* indexHead[bucket_cnt];

	void* getNextBlock()
	{
		void* pages = this->AllocateAddressSpace( reservation_size );
		return pages;
	}

	void* createNextBlockAndGetPage( size_t reasonIdx )
	{
		assert( reasonIdx < bucket_cnt );
		PageBlockDescriptor* pb = new PageBlockDescriptor; // TODO: consider using our own allocator
		pb->blockAddress = getNextBlock();
//printf( "createNextBlockAndGetPage(): descriptor allocated at 0x%zx; block = 0x%zx\n", (size_t)(pb), (size_t)(pb->blockAddress) );
		memset( pb->nextToUse, 0, sizeof( uint16_t) * bucket_cnt );
		memset( pb->nextToCommit, 0, sizeof( uint16_t) * bucket_cnt );
		pb->next = nullptr;
		pageBlockListCurrent->next = pb;
		pageBlockListCurrent = pb;
//		void* ret = idxToPageAddr( pb->blockAddress, reasonIdx );
		void* ret = idxToPageAddr( pb->blockAddress, reasonIdx, 0 );
//	printf("createNextBlockAndGetPage(): before commit, %zd, 0x%zx -> 0x%zx\n", reasonIdx, (size_t)(pb->blockAddress), (size_t)(ret) );
//		void* ret2 = this->CommitMemory( ret, PAGE_SIZE );
//		this->CommitMemory( ret, PAGE_SIZE );
		commitRangeOfPageIndexes( pb->blockAddress, reasonIdx, 0, commit_page_cnt );
		pb->nextToUse[ reasonIdx ] = 1;
		static_assert( commit_page_cnt <= UINT16_MAX, "" );
		pb->nextToCommit[ reasonIdx ] = (uint16_t)commit_page_cnt;
*reinterpret_cast<uint8_t*>(ret) += 1; // test write
//	printf("createNextBlockAndGetPage(): after commit 0x%zx\n", (size_t)(ret2) );
		return ret;
	}

	void resetLists()
	{
		pageBlockListStart.blockAddress = nullptr;
//		pageBlockListStart.usageMask = 0;
		for ( size_t i=0; i<bucket_cnt; ++i )
			pageBlockListStart.nextToUse[i] = pages_per_bucket; // thus triggering switching to a next block whatever bucket is selected
		for ( size_t i=0; i<bucket_cnt; ++i )
			pageBlockListStart.nextToCommit[i] = pages_per_bucket; // thus triggering switching to a next block whatever bucket is selected
		pageBlockListStart.next = nullptr;

		pageBlockListCurrent = &pageBlockListStart;
		for ( size_t i=0; i<bucket_cnt; ++i )
			indexHead[i] = pageBlockListCurrent;
	}

public:
	static constexpr size_t reserverdSizeAtPageStart() { return sizeof( MemoryBlockHeader ); }

public:
//	SoundingAddressPageAllocator( BasePageAllocator& pageAllocator_ ) : pageAllocator( pageAllocator_ ) {}
	SoundingAddressPageAllocator() {}

//	static FORCE_INLINE size_t addressToIdx( void* ptr ) { return ( (uintptr_t)(ptr) >> PAGE_SIZE_EXP ) & ( bucket_cnt - 1 ); }
//	static FORCE_INLINE size_t addressToIdx( void* ptr ) { return ( (uintptr_t)(ptr) >> (reservation_size_exp - bucket_cnt_exp) ) & ( bucket_cnt - 1 ); }
	static FORCE_INLINE size_t addressToIdx( void* ptr ) 
	{ 
		// TODO: make sure computations are optimal
		uintptr_t padr = (uintptr_t)(ptr) >> PAGE_SIZE_EXP;
		constexpr uintptr_t meaningfulBitsMask = ( 1 << (bucket_cnt_exp + pages_per_bucket_exp) ) - 1;
		uintptr_t meaningfulBits = padr & meaningfulBitsMask;
		return meaningfulBits >> pages_per_bucket_exp;
	}
/*	static FORCE_INLINE void* idxToPageAddr( void* blockptr, size_t idx ) 
	{ 
		assert( idx < bucket_cnt );
		uintptr_t startAsIdx = addressToIdx( blockptr );
		void* ret = (void*)( ( ( ( (uintptr_t)(blockptr) >> ( PAGE_SIZE_EXP + bucket_cnt_exp ) ) << bucket_cnt_exp ) + idx + (( idx < startAsIdx ) << bucket_cnt_exp) ) << PAGE_SIZE_EXP );
		assert( addressToIdx( ret ) == idx );
		return ret;
	}*/
	static FORCE_INLINE void* idxToPageAddr( void* blockptr, size_t idx, size_t pagesUsed ) 
	{ 
		assert( idx < bucket_cnt );
		uintptr_t startAsIdx = addressToIdx( blockptr );
		assert( ( (uintptr_t)(blockptr) & PAGE_SIZE_MASK ) == 0 );
		uintptr_t startingPage =  (uintptr_t)(blockptr) >> PAGE_SIZE_EXP;
		uintptr_t basePage =  ( startingPage >> (reservation_size_exp - PAGE_SIZE_EXP) ) << (reservation_size_exp - PAGE_SIZE_EXP);
		uintptr_t baseOffset = startingPage - basePage;
//		uintptr_t stepOffset = baseOffset & (pages_per_bucket - 1);
//		bool below = pagesUsed < stepOffset;
		bool below = (idx << pages_per_bucket_exp) + pagesUsed < baseOffset;
		uintptr_t ret = basePage + (idx << pages_per_bucket_exp) + pagesUsed + (below << (pages_per_bucket_exp + bucket_cnt_exp));
		ret <<= PAGE_SIZE_EXP;
		assert( addressToIdx( (void*)( ret ) ) == idx );
		assert( (uint8_t*)blockptr <= (uint8_t*)ret && (uint8_t*)ret < (uint8_t*)blockptr + reservation_size );
		return (void*)( ret );

/*		void* ret = (void*)( ( ( ( ( (uintptr_t)(blockptr) >> reservation_size_exp ) << bucket_cnt_exp ) + idx + (( idx < startAsIdx ) << bucket_cnt_exp) ) << ( reservation_size_exp - bucket_cnt_exp ) ) + ( pagesUsed << PAGE_SIZE_EXP ) );
		assert( addressToIdx( ret ) == idx );
		assert( (uint8_t*)blockptr <= (uint8_t*)ret && (uint8_t*)ret < (uint8_t*)blockptr + reservation_size );
		return ret;*/
	}
	static FORCE_INLINE size_t getOffsetInPage( void * ptr ) { return (uintptr_t)(ptr) & PAGE_SIZE_MASK; }
	static FORCE_INLINE void* ptrToPageStart( void * ptr ) { return (void*)( ( (uintptr_t)(ptr) >> PAGE_SIZE_EXP ) << PAGE_SIZE_EXP ); }

	void initialize( uint8_t blockSizeExp )
	{
		BasePageAllocator::initialize( blockSizeExp );
		resetLists();
	}

	void commitRangeOfPageIndexes( void* blockptr, size_t bucketIdx, size_t pageIdx, size_t rangeSize )
	{
		uint8_t* start = reinterpret_cast<uint8_t*>( idxToPageAddr( blockptr, bucketIdx, pageIdx ) );
		uint8_t* prevNext = start;
		uint8_t* next;
		for ( size_t i=1; i<rangeSize; ++i )
		{
			next = reinterpret_cast<uint8_t*>( idxToPageAddr( blockptr, bucketIdx, pageIdx + i ) );
			if ( next - prevNext == PAGE_SIZE )
			{
				prevNext = next;
				continue;
			}
			else
			{
				this->CommitMemory( start, prevNext - start + PAGE_SIZE );
				start = next;
				prevNext = next;
			}
		}
		this->CommitMemory( start, prevNext - start + PAGE_SIZE );
	}

	void* getPage( size_t idx )
	{
		assert( idx < bucket_cnt );
		assert( indexHead[idx] );
		if ( indexHead[idx]->nextToUse[idx] < pages_per_bucket )
		{
			assert( indexHead[idx]->nextToUse[idx] <= indexHead[idx]->nextToCommit[idx] );
			if ( indexHead[idx]->nextToUse[idx] == indexHead[idx]->nextToCommit[idx] )
			{
				commitRangeOfPageIndexes( indexHead[idx]->blockAddress, idx, indexHead[idx]->nextToCommit[idx], commit_page_cnt );
				indexHead[idx]->nextToCommit[ idx ] += commit_page_cnt;
			}
			void* ret = idxToPageAddr( indexHead[idx]->blockAddress, idx, indexHead[idx]->nextToUse[idx] );
			++(indexHead[idx]->nextToUse[idx]);
			assert( indexHead[idx]->nextToUse[idx] <= indexHead[idx]->nextToCommit[idx] );
//			this->CommitMemory( ret, PAGE_SIZE );
*reinterpret_cast<uint8_t*>(ret) += 1; // test write
			return ret;
		}
		else if ( indexHead[idx]->next == nullptr ) // next block is to be created
		{
			assert( indexHead[idx] == pageBlockListCurrent );
			void* ret = createNextBlockAndGetPage( idx );
			indexHead[idx] = pageBlockListCurrent;
			assert( indexHead[idx]->next == nullptr );
*reinterpret_cast<uint8_t*>(ret) += 1; // test write
			return ret;
		}
		else // next block is just to be used first time
		{
			indexHead[idx] = indexHead[idx]->next;
			assert( indexHead[idx]->blockAddress );
//			assert( ( indexHead[idx]->usageMask & ( ((size_t)1) << idx ) ) == 0 );
//			indexHead[idx]->usageMask |= ((size_t)1) << idx;
//			void* ret = idxToPageAddr( indexHead[idx]->blockAddress, idx );
			assert( indexHead[idx]->nextToUse[idx] == 0 );
			assert( indexHead[idx]->nextToCommit[idx] == 0 );
			if ( indexHead[idx]->nextToUse[idx] == indexHead[idx]->nextToCommit[idx] )
			commitRangeOfPageIndexes( indexHead[idx]->blockAddress, idx, indexHead[idx]->nextToCommit[idx], commit_page_cnt );
			indexHead[idx]->nextToCommit[idx] = commit_page_cnt;
			void* ret = idxToPageAddr( indexHead[idx]->blockAddress, idx, indexHead[idx]->nextToUse[idx] );
			indexHead[idx]->nextToUse[idx] = 1;
			assert( indexHead[idx]->nextToUse[idx] <= indexHead[idx]->nextToCommit[idx] );
//	printf("getPage(): before commit, %zd, 0x%zx -> 0x%zx\n", idx, (size_t)(indexHead[idx]->blockAddress), (size_t)(ret) );
//			void* ret2 = this->CommitMemory( ret, PAGE_SIZE );
//	printf("getPage(): after commit 0x%zx\n", (size_t)(ret2) );
//			this->CommitMemory( ret, PAGE_SIZE );
*reinterpret_cast<uint8_t*>(ret) += 1; // test write
			return ret;
		}
	}

	void freePage( MemoryBlockListItem* chk )
	{
		assert( false );
		// TODO: decommit
	}

	void deinitialize()
	{
//printf( "Entering deinitialize()...\n" );
		PageBlockDescriptor* next = pageBlockListStart.next;
		while( next )
		{
//printf( "in block 0x%zx about to delete 0x%zx of size 0x%zx\n", (size_t)( next ), (size_t)( next->blockAddress ), PAGE_SIZE * bucket_cnt );
			assert( next->blockAddress );
			this->freeChunkNoCache( reinterpret_cast<MemoryBlockListItem*>( next->blockAddress ), reservation_size );
			PageBlockDescriptor* tmp = next->next;
			delete next;
			next = tmp;
		}
		resetLists();
		BasePageAllocator::deinitialize();
	}
};
#endif


//#define BULKALLOCATOR_HEAVY_DEBUG

template<class BasePageAllocator, size_t commited_block_size, uint16_t max_pages>
class BulkAllocator : public BasePageAllocator
{
	static_assert( ( commited_block_size >> PAGE_SIZE_EXP ) > 0 );
	static_assert( ( commited_block_size & PAGE_SIZE_MASK ) == 0 );
	static_assert( max_pages < PAGE_SIZE );
	static constexpr size_t pagesPerAllocatedBlock = commited_block_size >> PAGE_SIZE_EXP;

public:
	struct AnyChunkHeader
	{
	private:
		uintptr_t prev;
		uintptr_t next;
	public:
		AnyChunkHeader* prevInBlock() {return (AnyChunkHeader*)( prev & ~((uintptr_t)(PAGE_SIZE_MASK)) ); }
		const AnyChunkHeader* prevInBlock() const {return (const AnyChunkHeader*)( prev & ~((uintptr_t)(PAGE_SIZE_MASK)) ); }
		AnyChunkHeader* nextInBlock() {return (AnyChunkHeader*)( next & ~((uintptr_t)(PAGE_SIZE_MASK) ) ); }
		const AnyChunkHeader* nextInBlock() const {return (const AnyChunkHeader*)( next & ~((uintptr_t)(PAGE_SIZE_MASK) ) ); }
		void setPrevInBlock( AnyChunkHeader* prev_ ) { assert( ((uintptr_t)prev_ & PAGE_SIZE_MASK) == 0 ); prev = ( (uintptr_t)prev_ & ~(uintptr_t)(PAGE_SIZE_MASK) ) + (prev & ((uintptr_t)(PAGE_SIZE_MASK))); }
//		void setNext( AnyChunkHeader* next_ ) { assert( ((uintptr_t)next_ & PAGE_SIZE_MASK) == 0 ); next = ( (uintptr_t)next_ & ~(uintptr_t)(PAGE_SIZE_MASK) ) + (next & ((uintptr_t)(PAGE_SIZE_MASK))); }
//		void setPageCount( uint16_t cnt ) { assert( cnt < max_pages ); prev = ( prev & ~(uintptr_t)(PAGE_SIZE_MASK) ) + cnt; }
		uint16_t getPageCount() const { return prev & ((uintptr_t)(PAGE_SIZE_MASK)); }
//		void setIsFree( bool isFree ) { next = ( next & ~(uintptr_t)(PAGE_SIZE_MASK) ) + isFree; }
		bool isFree() const { return next & ((uintptr_t)(PAGE_SIZE_MASK)); }
		void set( AnyChunkHeader* prevInBlock_, AnyChunkHeader* nextInBlock_, uint16_t pageCount, bool isFree )
		{
			assert( ((uintptr_t)prevInBlock_ & PAGE_SIZE_MASK) == 0 );
			assert( ((uintptr_t)nextInBlock_ & PAGE_SIZE_MASK) == 0 );
			assert( pageCount <= (commited_block_size>>PAGE_SIZE_EXP) );
			prev = ((uintptr_t)prevInBlock_) + pageCount;
			next = ((uintptr_t)nextInBlock_) + isFree;
		}
	};

	constexpr size_t maxAllocatableSize() {return ((size_t)max_pages) << PAGE_SIZE_EXP; }
	static constexpr size_t reserverdSizeAtPageStart() { return sizeof( AnyChunkHeader ); }

private:
	std::vector<AnyChunkHeader*> blockList;

	struct FreeChunkHeader : public AnyChunkHeader
	{
		FreeChunkHeader* prevFree;
		FreeChunkHeader* nextFree;
	};
	FreeChunkHeader* freeListBegin[ max_pages + 1 ];

	void removeFromFreeList( FreeChunkHeader* item )
	{
		if ( item->prevFree )
		{
			assert( item->getPageCount() == item->prevFree->getPageCount() );
			item->prevFree->nextFree = item->nextFree;
		}
		else
		{
			uint16_t idx = item->getPageCount() - 1;
			if ( idx > max_pages )
				idx = max_pages + 1;
			assert( freeListBegin[idx]->nextFree == item );
			freeListBegin[idx]->nextFree = item->nextFree;
		}
		if ( item->nextFree )
		{
			assert( item->getPageCount() == item->nextFree->getPageCount() );
			item->nextFree->prevFree = item->prevFree;
		}
	}

	void dbgValidateBlock( const AnyChunkHeader* h )
	{
		assert( h != nullptr );
		size_t szTotal = 0;
		const AnyChunkHeader* curr = h;
		while ( curr )
		{
			szTotal += curr->getPageCount();
			const AnyChunkHeader* next = curr->nextInBlock();
			if ( next )
			{
				assert( next > curr );
				assert( !(curr->isFree() && next->isFree()) );
				assert( next->prevInBlock() == curr );
				assert( reinterpret_cast<const uint8_t*>(curr) + (curr->getPageCount() << PAGE_SIZE_EXP) == reinterpret_cast<const uint8_t*>( next ) );
			}
			curr = next;
		}
		curr = h->prevInBlock();
		assert( curr == nullptr || ( curr->isFree() != h->isFree() ) );
		while ( curr )
		{
			szTotal += curr->getPageCount();
			const AnyChunkHeader* prev = curr->prevInBlock();
			if ( prev )
			{
				assert( prev < curr );
				assert( !(prev->isFree() && curr->isFree()) );
				assert( prev->nextInBlock() == curr );
				assert( reinterpret_cast<const uint8_t*>(prev) + (prev->getPageCount() << PAGE_SIZE_EXP) == reinterpret_cast<const uint8_t*>( curr ) );
			}
			curr = prev;
		}
		assert( (szTotal << PAGE_SIZE_EXP) == commited_block_size );
	}

	void dbgValidateAllBlocks()
	{
		for ( size_t i=0; i<blockList.size(); ++i )
		{
			AnyChunkHeader* start = reinterpret_cast<AnyChunkHeader*>( blockList[i] );
			dbgValidateBlock( start );
		}
	}

	void dbgValidateFreeList( const FreeChunkHeader* h, const uint16_t pageCnt )
	{
		assert( h != nullptr );
		const FreeChunkHeader* curr = h;
		while ( curr )
		{
			assert( curr->getPageCount() == pageCnt || ( pageCnt > max_pages && curr->getPageCount() >= pageCnt ) );
			assert( curr->isFree() );
			const FreeChunkHeader* next = curr->nextFree;
			assert( next == nullptr || next->prevFree == curr );
			curr = next;
		}
		curr = h;
		while ( curr )
		{
			assert( curr->getPageCount() == pageCnt || ( pageCnt > max_pages && curr->getPageCount() >= pageCnt ) );
			assert( curr->isFree() );
			const FreeChunkHeader* prev = curr->prevFree;
			assert( prev == nullptr || prev->nextFree == curr );
			curr = prev;
		}
	}

	void dbgValidateAllFreeLists()
	{
		for ( uint16_t i=0; i<=max_pages; ++i )
		{
			FreeChunkHeader* h = freeListBegin[i];
			if ( h !=nullptr )
				dbgValidateFreeList( h, i + 1 );
		}
	}

public:
	void initialize( uint8_t blockSizeExp )
	{
		BasePageAllocator::initialize( blockSizeExp );
		for ( size_t i=0; i<=max_pages; ++i )
			freeListBegin[i] = nullptr;
		new ( &blockList ) std::vector<AnyChunkHeader*>;
#ifdef BULKALLOCATOR_HEAVY_DEBUG
		dbgValidateAllBlocks();
		dbgValidateAllFreeLists();
#endif
	}

	AnyChunkHeader* allocate( size_t szIncludingHeader )
	{
#ifdef BULKALLOCATOR_HEAVY_DEBUG
		dbgValidateAllBlocks();
		dbgValidateAllFreeLists();
#endif

		AnyChunkHeader* ret = nullptr;

		size_t pageCount = ((uintptr_t)(-((intptr_t)((((uintptr_t)(-((intptr_t)szIncludingHeader))))) >> PAGE_SIZE_EXP )));

		if ( pageCount <= max_pages )
		{
			assert( pageCount <= UINT16_MAX );
			assert( pageCount <= max_pages );

			if ( freeListBegin[pageCount - 1] == nullptr )
			{
				if ( freeListBegin[ max_pages ] == nullptr )
				{
					FreeChunkHeader* h = reinterpret_cast<FreeChunkHeader*>( this->getFreeBlockNoCache( commited_block_size ) );
					assert( h!= nullptr );
					blockList.push_back( h );
					freeListBegin[ max_pages ] = h;
					freeListBegin[ max_pages ]->set( nullptr, nullptr, pagesPerAllocatedBlock, true );
					freeListBegin[ max_pages ]->nextFree = nullptr;
					freeListBegin[ max_pages ]->prevFree = nullptr;
				}

				assert( freeListBegin[ max_pages ] != nullptr );
				assert( freeListBegin[ max_pages ]->getPageCount() > max_pages );
				assert( freeListBegin[ max_pages ]->prevFree == nullptr );

				ret = freeListBegin[ max_pages ];
				freeListBegin[ max_pages ] = freeListBegin[ max_pages ]->nextFree; // pop
				FreeChunkHeader* updatedBegin = reinterpret_cast<FreeChunkHeader*>( reinterpret_cast<uint8_t*>(ret) + (pageCount << PAGE_SIZE_EXP) );
				updatedBegin->set( ret, ret->nextInBlock(), ret->getPageCount() - (uint16_t)pageCount, true );
//				updatedBegin->nextFree = freeListBegin[ max_pages ]->nextFree;
				updatedBegin->prevFree = nullptr;

				ret->set( ret->prevInBlock(), updatedBegin, (uint16_t)pageCount, false );
				assert( freeListBegin[ max_pages ] != updatedBegin );

				uint16_t remainingPageCnt = updatedBegin->getPageCount();
				if ( remainingPageCnt > max_pages )
				{
					updatedBegin->nextFree = freeListBegin[ max_pages ];
					if ( freeListBegin[ max_pages ] != nullptr )
						freeListBegin[ max_pages ]->prevFree = updatedBegin;
					freeListBegin[ max_pages ] = updatedBegin;
//					if ( freeListBegin[ max_pages ]->nextInBlock() )
//						freeListBegin[ max_pages ]->nextInBlock()->setPrevInBlock( ret );
				}
				else
				{
					freeListBegin[ max_pages ] = updatedBegin->nextFree;
					if ( freeListBegin[ max_pages ] != nullptr )
						freeListBegin[ max_pages ]->prevFree = nullptr;
					updatedBegin->nextFree = freeListBegin[ remainingPageCnt - 1 ];
					if ( freeListBegin[ remainingPageCnt - 1 ] != nullptr )
						freeListBegin[ remainingPageCnt - 1 ]->prevFree = updatedBegin;
					freeListBegin[ remainingPageCnt - 1 ] = updatedBegin;
				}
			}
			else
			{
				assert( freeListBegin[pageCount - 1]->nextFree == nullptr || freeListBegin[pageCount - 1]->nextFree->prevFree == freeListBegin[pageCount - 1] );
				assert( freeListBegin[pageCount - 1]->prevFree == nullptr );
				ret = freeListBegin[pageCount - 1];
				freeListBegin[pageCount - 1] = freeListBegin[pageCount - 1]->nextFree;
				if ( freeListBegin[pageCount - 1] != nullptr )
					freeListBegin[pageCount - 1]->prevFree = nullptr;
			}
		}
		else
		{
			ret = reinterpret_cast<FreeChunkHeader*>( this->getFreeBlockNoCache( pageCount << PAGE_SIZE_EXP ) );
			ret->set( (FreeChunkHeader*)(void*)(pageCount<<PAGE_SIZE_EXP), nullptr, 0, false );
		}


#ifdef BULKALLOCATOR_HEAVY_DEBUG
		dbgValidateAllBlocks();
		dbgValidateAllFreeLists();
#endif

		return ret;
	}

	void deallocate( void* ptr )
	{
		AnyChunkHeader* h = reinterpret_cast<AnyChunkHeader*>( ptr );
		if ( h->getPageCount() != 0 )
		{
#ifdef BULKALLOCATOR_HEAVY_DEBUG
		dbgValidateAllBlocks();
		dbgValidateAllFreeLists();
#endif

			AnyChunkHeader* prev = h->prevInBlock();
			if ( prev && prev->isFree() )
			{
				assert( prev->prevInBlock() == nullptr || !prev->prevInBlock()->isFree() );
				assert( prev->nextInBlock() == h );
				assert( reinterpret_cast<uint8_t*>(prev->prevInBlock()) + (prev->prevInBlock()->getPageCount() << PAGE_SIZE_EXP) == reinterpret_cast<uint8_t*>( h ) );
				removeFromFreeList( reinterpret_cast<FreeChunkHeader*>(prev) );
				prev->set( prev->prevInBlock(), h->nextInBlock(), prev->getPageCount() + h->getPageCount(), true );
				h = prev;
			}
			AnyChunkHeader* next = h->nextInBlock();
			if ( next && next->isFree() )
			{
				assert( next->nextInBlock() == nullptr || !next->nextInBlock()->isFree() );
				assert( next->prevInBlock() == h );
				assert( reinterpret_cast<uint8_t*>(h) + h->getPageCount() == reinterpret_cast<uint8_t*>( next ) );
				removeFromFreeList( reinterpret_cast<FreeChunkHeader*>(next) );
				h->set( h->prevInBlock(), next->nextInBlock(), h->getPageCount() + next->getPageCount(), true );
			}

			FreeChunkHeader* hfree = reinterpret_cast<FreeChunkHeader*>(h);
			uint16_t idx = hfree->getPageCount() - 1;
			if ( idx > max_pages )
				idx = max_pages + 1;
			hfree->prevFree = nullptr;
			hfree->nextFree = freeListBegin[idx];
			freeListBegin[idx] = hfree;

#ifdef BULKALLOCATOR_HEAVY_DEBUG
		dbgValidateAllBlocks();
		dbgValidateAllFreeLists();
#endif
		}
		else
		{
			size_t deallocSize = (size_t)(h->prevInBlock()) << PAGE_SIZE_EXP;
			this->freeChunkNoCache( ptr, deallocSize );
		}

	}

	void deinitialize()
	{
		for ( size_t i=0; i<blockList.size(); ++i )
		{
			assert( blockList[i] != nullptr );
			this->freeChunkNoCache( blockList[i], commited_block_size );
		}
		blockList.clear();
		for ( size_t i=0; i<=max_pages; ++i )
			freeListBegin[i] = nullptr;
#ifdef BULKALLOCATOR_HEAVY_DEBUG
		dbgValidateAllBlocks();
		dbgValidateAllFreeLists();
#endif
	}
};


class SerializableAllocatorBase
{
protected:
	static constexpr size_t MaxBucketSize = PAGE_SIZE / 16;
	static constexpr size_t BucketCountExp = 4;
	static constexpr size_t BucketCount = 1 << BucketCountExp;
	void* buckets[BucketCount];
	static constexpr size_t large_block_idx = 0xFF;

	struct ChunkHeader
	{
		MemoryBlockListItem block;
		ChunkHeader* next;
		size_t idx;
	};
	
	static constexpr size_t reservation_size_exp = 23;
	typedef BulkAllocator<PageAllocatorWithCaching, 1 << reservation_size_exp, 32> BulkAllocatorT;
	BulkAllocatorT bulkAllocator;

#ifdef USE_SOUNDING_PAGE_ADDRESS
	typedef SoundingAddressPageAllocator<PageAllocatorWithCaching, BucketCountExp, reservation_size_exp, 4> PageAllocatorT;
//	typedef SoundingAddressPageAllocator<PageAllocatorNoCachingForTestPurposes, BucketCountExp, reservation_size_exp, 4> PageAllocatorT;
	PageAllocatorT pageAllocator;
#else
	PageAllocatorWithCaching pageAllocator;

	ChunkHeader* nextPage = nullptr;

	FORCE_INLINE
	ChunkHeader* getChunkFromUsrPtr(void* ptr)
	{
		return reinterpret_cast<ChunkHeader*>(alignDownExp(reinterpret_cast<uintptr_t>(ptr), PAGE_SIZE_EXP));
	}
#endif

#ifdef USE_ITEM_HEADER
	struct ItemHeader
	{
		uint8_t idx;
		uint8_t reserved[7];
	};
	static_assert( sizeof( ItemHeader ) == 8, "" );
#endif // USE_ITEM_HEADER

protected:
public:
	static constexpr
	FORCE_INLINE size_t indexToBucketSize(uint8_t ix) // Note: currently is used once per page formatting
	{
		return 1ULL << (ix + 3);
	}
	static constexpr
	FORCE_INLINE size_t indexToBucketSizeHalfExp(uint8_t ix) // Note: currently is used once per page formatting
	{
		size_t ret = ( 1ULL << ((ix>>1) + 3) ) + ( ( ( ( ix + 1 ) & 1 ) - 1 ) & ( 1ULL << ((ix>>1) + 2) ) );
		return alignUpExp( ret, 3 ); // this is because of case ix = 1, ret = 12 (keeping 8-byte alignment)
	}

#if defined(_MSC_VER)
#if defined(_M_IX86)
	static
		FORCE_INLINE uint8_t sizeToIndex(uint32_t sz)
	{
		unsigned long ix;
		uint8_t r = _BitScanReverse(&ix, sz - 1);
		return (sz <= 8) ? 0 : static_cast<uint8_t>(ix - 2);
	}
#elif defined(_M_X64)
	static
	FORCE_INLINE uint8_t sizeToIndex(uint64_t sz)
	{
		unsigned long ix;
		uint8_t r = _BitScanReverse64(&ix, sz - 1);
		return (sz <= 8) ? 0 : static_cast<uint8_t>(ix - 2);
	}
	static
	FORCE_INLINE uint8_t sizeToIndexHalfExp(uint64_t sz)
	{
		if ( sz <= 8 )
			return 0;
		sz -= 1;
		unsigned long ix;
		uint8_t r = _BitScanReverse64(&ix, sz);
//		printf( "ix = %zd\n", ix );
		uint8_t addition = 1 & ( sz >> (ix-1) );
		ix = ((ix-2)<<1) + addition - 1;
		return static_cast<uint8_t>(ix);
	}
#else
#error Unknown 32/64 bits architecture
#endif

#elif defined(__GNUC__)
#if defined(__i386__)
	static
		FORCE_INLINE uint8_t sizeToIndex(uint32_t sz)
	{
		uint32_t ix = __builtin_clzl(sz - 1);
		return (sz <= 8) ? 0 : static_cast<uint8_t>(29ul - ix);
	}
#elif defined(__x86_64__)
	static
		FORCE_INLINE uint8_t sizeToIndex(uint64_t sz)
	{
		uint64_t ix = __builtin_clzll(sz - 1);
		return (sz <= 8) ? 0 : static_cast<uint8_t>(61ull - ix);
	}
	static
		FORCE_INLINE uint8_t sizeToIndexHalfExp(uint64_t sz)
	{
		if ( sz <= 8 )
			return 0;
		sz -= 1;
//		uint64_t ix = __builtin_clzll(sz - 1);
//		return (sz <= 8) ? 0 : static_cast<uint8_t>(61ull - ix);
		uint64_t ix = __builtin_clzll(sz);
		ix = 63ull - ix;
//		printf( "ix = %zd\n", ix );
		uint8_t addition = 1ull & ( sz >> (ix-1) );
		ix = ((ix-2)<<1) + addition - 1;
		return static_cast<uint8_t>(ix);
	}
#else
#error Unknown 32/64 bits architecture
#endif	

#else
#error Unknown compiler
#endif
	
public:
	SerializableAllocatorBase() { initialize(); }
	SerializableAllocatorBase(const SerializableAllocatorBase&) = delete;
	SerializableAllocatorBase(SerializableAllocatorBase&&) = default;
	SerializableAllocatorBase& operator=(const SerializableAllocatorBase&) = delete;
	SerializableAllocatorBase& operator=(SerializableAllocatorBase&&) = default;

	void enable() {}
	void disable() {}


	NOINLINE void* allocateInCaseNoFreeBucket( size_t sz, uint8_t szidx )
	{
#ifdef USE_SOUNDING_PAGE_ADDRESS
		uint8_t* block = reinterpret_cast<uint8_t*>( pageAllocator.getPage( szidx ) );
		constexpr size_t memStart = alignUpExp( PageAllocatorT::reserverdSizeAtPageStart(), ALIGNMENT_EXP );
#else
		uint8_t* block = reinterpret_cast<uint8_t*>( pageAllocator.getFreeBlock( PAGE_SIZE ) );
		constexpr size_t memStart = alignUpExp( sizeof( ChunkHeader ), ALIGNMENT_EXP );
		ChunkHeader* h = reinterpret_cast<ChunkHeader*>( block );
		h->idx = szidx;
		h->next = nextPage;
		nextPage = h;
#endif
		uint8_t* mem = block + memStart;
//		size_t bucketSz = indexToBucketSize( szidx ); // TODO: rework
		size_t bucketSz = indexToBucketSizeHalfExp( szidx ); // TODO: rework
		assert( bucketSz >= sizeof( void* ) );
#ifdef USE_ITEM_HEADER
		bucketSz += sizeof(ItemHeader);
#endif // USE_ITEM_HEADER
		size_t itemCnt = (PAGE_SIZE - memStart) / bucketSz;
		assert( itemCnt );
		for ( size_t i=bucketSz; i<(itemCnt-1)*bucketSz; i+=bucketSz )
			*reinterpret_cast<void**>(mem + i) = mem + i + bucketSz;
		*reinterpret_cast<void**>(mem + (itemCnt-1)*bucketSz) = nullptr;
		buckets[szidx] = mem + bucketSz;
#ifdef USE_ITEM_HEADER
		reinterpret_cast<ItemHeader*>( mem )->idx = szidx;
		return mem + sizeof( ItemHeader );
#else
		return mem;
#endif // USE_ITEM_HEADER
	}

	NOINLINE void* allocateInCaseTooLargeForBucket(size_t sz)
	{
#ifdef USE_ITEM_HEADER
		constexpr size_t memStart = alignUpExp( sizeof( ChunkHeader ) + sizeof( ItemHeader ), ALIGNMENT_EXP );
#elif defined USE_SOUNDING_PAGE_ADDRESS
//		constexpr size_t memStart = alignUpExp( sizeof( size_t ), ALIGNMENT_EXP );
		constexpr size_t memStart = alignUpExp( BulkAllocatorT::reserverdSizeAtPageStart(), ALIGNMENT_EXP );
#else
		constexpr size_t memStart = alignUpExp( sizeof( ChunkHeader ), ALIGNMENT_EXP );
#endif // USE_ITEM_HEADER
//		size_t fullSz = alignUpExp( sz + memStart, PAGE_SIZE_EXP );
//		void* block = pageAllocator.getFreeBlock( fullSz );
		void* block = bulkAllocator.allocate( sz + memStart );

#ifdef USE_ITEM_HEADER
#else
/*		size_t allocatedSz = ( reinterpret_cast<MemoryBlockListItem*>( block ) )->getSize();
		size_t* h = reinterpret_cast<size_t*>( block );
//		*h = sz;
		*h = allocatedSz;*/
#endif

//		usedNonBuckets.pushFront(chk);
#ifdef USE_ITEM_HEADER
		( reinterpret_cast<ItemHeader*>( reinterpret_cast<uint8_t*>(block) + memStart ) - 1 )->idx = large_block_idx;
#endif // USE_ITEM_HEADER
		return reinterpret_cast<uint8_t*>(block) + memStart;
	}

	FORCE_INLINE void* allocate(size_t sz)
	{
		if ( sz <= MaxBucketSize )
		{
//			uint8_t szidx = sizeToIndex( sz );
			uint8_t szidx = sizeToIndexHalfExp( sz );
			assert( szidx < BucketCount );
			if ( buckets[szidx] )
			{
				void* ret = buckets[szidx];
				buckets[szidx] = *reinterpret_cast<void**>(buckets[szidx]);
#ifdef USE_ITEM_HEADER
				reinterpret_cast<ItemHeader*>( ret )->idx = szidx;
				return reinterpret_cast<uint8_t*>(ret) + sizeof( ItemHeader );
#else
				return ret;
#endif // USE_ITEM_HEADER
			}
			else
				return allocateInCaseNoFreeBucket( sz, szidx );
		}
		else
			return allocateInCaseTooLargeForBucket( sz );

		return nullptr;
	}

	FORCE_INLINE void deallocate(void* ptr)
	{
		if(ptr)
		{
#ifdef USE_ITEM_HEADER
			ItemHeader* ih = reinterpret_cast<ItemHeader*>(ptr) - 1;
			if ( ih->idx != large_block_idx )
			{
				uint8_t idx = ih->idx;
				*reinterpret_cast<void**>( ih ) = buckets[idx];
				buckets[idx] = ih;
			}
			else
			{
				ChunkHeader* ch = getChunkFromUsrPtr( ptr );
//				assert( reinterpret_cast<uint8_t*>(ch) == reinterpret_cast<uint8_t*>(ih) );
				pageAllocator.freeChunk( reinterpret_cast<MemoryBlockListItem*>(ch) );
			}
#elif defined USE_SOUNDING_PAGE_ADDRESS
			size_t offsetInPage = PageAllocatorT::getOffsetInPage( ptr );
			if ( offsetInPage > alignUpExp( sizeof( size_t ), ALIGNMENT_EXP ) )
			{
				size_t idx = PageAllocatorT::addressToIdx( ptr );
				*reinterpret_cast<void**>( ptr ) = buckets[idx];
				buckets[idx] = ptr;
			}
			else
			{
				void* pageStart = PageAllocatorT::ptrToPageStart( ptr );
/*				MemoryBlockListItem* h = reinterpret_cast<MemoryBlockListItem*>(pageStart);
				h->size = *reinterpret_cast<size_t*>(pageStart);
				h->sizeIndex = 0xFFFFFFFF; // TODO: address properly!!!
				h->prev = nullptr;
				h->next = nullptr;
				pageAllocator.freeChunk( reinterpret_cast<MemoryBlockListItem*>(h) );*/
				bulkAllocator.deallocate( pageStart );
			}
#else
			ChunkHeader* h = getChunkFromUsrPtr( ptr );
			if ( h->idx != large_block_idx )
			{
				*reinterpret_cast<void**>( ptr ) = buckets[h->idx];
				buckets[h->idx] = ptr;
			}
			else
				pageAllocator.freeChunk( reinterpret_cast<MemoryBlockListItem*>(h) );
#endif // USE_ITEM_HEADER
		}
	}
	
	const BlockStats& getStats() const { return pageAllocator.getStats(); }
	
	void printStats()
	{
		pageAllocator.printStats();
	}

	void initialize(size_t size)
	{
		initialize();
	}

	void initialize()
	{
		memset( buckets, 0, sizeof( void* ) * BucketCount );
		pageAllocator.initialize( PAGE_SIZE_EXP );
		bulkAllocator.initialize( PAGE_SIZE_EXP );
	}

	void deinitialize()
	{
#ifdef USE_SOUNDING_PAGE_ADDRESS
		// ...
#else
		while ( nextPage )
		{
			ChunkHeader* next = nextPage->next;
			pageAllocator.freeChunk( reinterpret_cast<MemoryBlockListItem*>(nextPage) );
			nextPage = next;
		}
#endif // USE_SOUNDING_PAGE_ADDRESS
		pageAllocator.deinitialize();
		bulkAllocator.deinitialize();
	}

	~SerializableAllocatorBase()
	{
		deinitialize();
	}
};

#endif //SERIALIZABLE_ALLOCATOR_KEEPING_PAGES_H
