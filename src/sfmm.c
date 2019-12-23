/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"


// Global variables
const size_t set2lsbstozero = (~0) << 2;
sf_block sf_free_list_heads[NUM_FREE_LISTS];
sf_prologue *sf_pro;
sf_epilogue *sf_epi;


// Helper function for adding to free list
void addToFreeList(sf_block *currBlock, size_t currBlockSize) {
    // Find which free list to put new block in
    int listNum = 0;
    size_t listSize = 32;
    while (listSize < currBlockSize) {
        listSize *= 2;
        listNum++;
        if (listNum == 8) {
            break;
        }
    }

    // Now add to beginning of correct free list
    sf_block *currFreeListNext = sf_free_list_heads[listNum].body.links.next;
    sf_free_list_heads[listNum].body.links.next = currBlock;
    currBlock->body.links.next = currFreeListNext;
    currFreeListNext->body.links.prev = currBlock;
    currBlock->body.links.prev = &sf_free_list_heads[listNum];
}


// Helper function for removing from free list
void removeFromFreeList(sf_block *currBlock, size_t currBlockSize) {
    // Find which current free list free block is in
    int listNum = 0;
    size_t listSize = 32;
    while (listSize < currBlockSize) {
        listSize *= 2;
        listNum++;
        if (listNum == 8) {
            break;
        }
    }

    // Now find block in its free list to delete
    sf_block *current = &sf_free_list_heads[listNum];
    while (currBlock != current) {
        current = current->body.links.next;
    }

    // Block found so remove it now
    sf_block *prevListBlock = current->body.links.prev;
    sf_block *nextListBlock = current->body.links.next;
    prevListBlock->body.links.next = nextListBlock;
    nextListBlock->body.links.prev = prevListBlock;
}


// Helper function for adding first page to heap
int sf_init_page() {

    // Store ptr from sf_mem_grow and check for null
    void *heapStart = sf_mem_grow();
    if (heapStart == NULL) {
        return -1;
    }

    // Take care of free list stuff
    for (int i = 0; i < NUM_FREE_LISTS; i++) {
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
    }

    // Prologue for heap
    sf_pro = heapStart;
    sf_pro->padding1 = 0;
    sf_pro->header = 32 | THIS_BLOCK_ALLOCATED | PREV_BLOCK_ALLOCATED;
    sf_pro->unused1 = NULL;
    sf_pro->unused2 = NULL;
    sf_pro->footer = sf_pro->header ^ sf_magic();

    // Main free block
    sf_block *newFreeBlock = (void *)sf_pro + 32;
    newFreeBlock->prev_footer = sf_pro->header ^ sf_magic();
    newFreeBlock->header = (PAGE_SZ-48) | PREV_BLOCK_ALLOCATED;
    newFreeBlock->body.links.next = &sf_free_list_heads[7];
    newFreeBlock->body.links.prev = &sf_free_list_heads[7];

    // Update free list (new page is 127)
    sf_free_list_heads[7].body.links.next = newFreeBlock;
    sf_free_list_heads[7].body.links.prev = newFreeBlock;

    // Footer block for single free block
    sf_footer *lastFooter = sf_mem_end() - 16;
    *lastFooter = newFreeBlock->header ^ sf_magic();

    // Epilogue for heap
    sf_epi = (sf_mem_end() - 8);
    sf_epi->header = THIS_BLOCK_ALLOCATED;

    // End of function so return
    return 0;
}


// Helper function for splitting found free block
int splitBlock(size_t blockSize, sf_block *block, size_t splitBlockSize) {

    // Store footer location and change current block head
    size_t sizeTillFooter = block->header & set2lsbstozero;

    // Set prev block allocation appropriately
    if ((block->header & PREV_BLOCK_ALLOCATED) > 0) {
        block->header = blockSize | THIS_BLOCK_ALLOCATED | PREV_BLOCK_ALLOCATED;
    }  else {
        block->header = blockSize | THIS_BLOCK_ALLOCATED;
    }

    // Make new free block from the split
    sf_block *newFreeBlock = (void *)block + blockSize;
    newFreeBlock->prev_footer = block->header ^ sf_magic();
    newFreeBlock->header = splitBlockSize | PREV_BLOCK_ALLOCATED;

    // Take care of the old footer
    sf_footer *freeBlockFooter = (void *)block + sizeTillFooter;
    *freeBlockFooter = newFreeBlock->header ^ sf_magic();

    // Update next block prev allocation
    if (freeBlockFooter != sf_mem_end() - 16) {
        sf_block *nextBlock = (void *)freeBlockFooter;
        size_t nextBlockSize = nextBlock->header & set2lsbstozero;
        nextBlock->header = nextBlock->header & ~PREV_BLOCK_ALLOCATED;
        sf_footer *nextBlockFooter = (void *)nextBlock + nextBlockSize;
        *nextBlockFooter = nextBlock->header ^ sf_magic();
    }

    // Now add to correct free list
    addToFreeList(newFreeBlock, splitBlockSize);

    // End of function so return
    return 0;
}


// Helper function for adding pages to heap if needed
int addMoreHeapSpace() {

    // Store current last block footer
    sf_footer *lastFooter = sf_mem_end() - 16;

    // Call sf_mem_grow to obtain moreheap space
    void *heapStart = sf_mem_grow();
    if (heapStart == NULL) {
        return -1;
    }

    // Set the new epilogue at the end of new page
    sf_epi = (sf_mem_end() - 8);
    sf_epi->header = THIS_BLOCK_ALLOCATED;

    // Coalesce the new block if there is a free block
    if (((*lastFooter ^ sf_magic()) & THIS_BLOCK_ALLOCATED) == 0) {
        // Previous block is free so coalesce
        size_t prevBlockSize = (*lastFooter ^ sf_magic()) & set2lsbstozero;
        sf_block *prevBlock = (void *)lastFooter - prevBlockSize;

        // Now remove from free list
        removeFromFreeList(prevBlock, prevBlockSize);

        // Now make one big free block
        prevBlock->header = prevBlock->header + PAGE_SZ;
        lastFooter = sf_mem_end() - 16;
        *lastFooter = prevBlock->header ^ sf_magic();

        // Now add to beginning of correct free list
        sf_block *currFreeListNext = sf_free_list_heads[8].body.links.next;
        sf_free_list_heads[8].body.links.next = prevBlock;
        prevBlock->body.links.next = currFreeListNext;
        currFreeListNext->body.links.prev = prevBlock;
        prevBlock->body.links.prev = &sf_free_list_heads[8];
    } else {
        // Previous block is allocated
        sf_block *newFreeBlock = (void *)lastFooter;
        newFreeBlock->prev_footer = *lastFooter;
        newFreeBlock->header = (PAGE_SZ) | PREV_BLOCK_ALLOCATED;
        lastFooter = sf_mem_end() - 16;
        *lastFooter = newFreeBlock->header ^ sf_magic();

        // Now add to beginning of correct free list
        sf_block *currFreeListNext = sf_free_list_heads[7].body.links.next;
        sf_free_list_heads[7].body.links.next = newFreeBlock;
        newFreeBlock->body.links.next = currFreeListNext;
        currFreeListNext->body.links.prev = newFreeBlock;
        newFreeBlock->body.links.prev = &sf_free_list_heads[7];
    }

    // End of function so return
    return 0;
}


// Helper function for finding avaliable free block
void *availableBlock(int listNum, size_t blockSize) {

    // Boolean value, 0 for false 1 for true
    int foundBlock = 0;
    sf_block *freeBlock = NULL;

    // Label for jumping to after adding page
    lookForBlockAgain :

    // Loop for finding first available free block
    for (int i = listNum; i < 9; i++) {
        sf_block *currSentinal = &sf_free_list_heads[i];
        sf_block *nextNode = sf_free_list_heads[i].body.links.next;

        // Iterate until sentinal block or free block found
        while (currSentinal != nextNode) {
            if ((nextNode->header & set2lsbstozero) >= blockSize) {
                freeBlock = nextNode;

                // Remove nextNode from free lists
                sf_block *prevBlock = nextNode->body.links.prev;
                sf_block *nextBlock = nextNode->body.links.next;
                prevBlock->body.links.next = nextBlock;
                nextBlock->body.links.prev = prevBlock;

                foundBlock = 1;
                goto freeBlockFound;
            }
            nextNode = nextNode->body.links.next;
        }
    }

    // Label for jumping to if free block found
    freeBlockFound :

    // Either split or add new page
    if (foundBlock) {
        // Block found so split if needed
        sf_header freeBlockHeader = freeBlock->header;
        size_t freeBlockSize = freeBlockHeader & set2lsbstozero;

        // Split if left over block size >= 32
        if (freeBlockSize - blockSize >= 32) {
            splitBlock(blockSize, freeBlock, freeBlockSize - blockSize);
        } else {
            sf_block *currBlock = freeBlock;
            size_t currBlockSize = currBlock->header & set2lsbstozero;
            currBlock->header = freeBlockHeader | THIS_BLOCK_ALLOCATED;
            sf_footer *currBlockFooter = (void *)currBlock + currBlockSize;
            *currBlockFooter = currBlock->header ^ sf_magic();

            // Update next block prev allocation
            if (currBlockFooter != sf_mem_end() - 16) {
                sf_block *nextBlock = (void *)currBlockFooter;
                size_t nextBlockSize = nextBlock->header & set2lsbstozero;
                nextBlock->header = nextBlock->header | PREV_BLOCK_ALLOCATED;
                sf_footer *nextBlockFooter = (void *)nextBlock + nextBlockSize;
                *nextBlockFooter = nextBlock->header ^ sf_magic();
            } else {
                sf_epi->header = THIS_BLOCK_ALLOCATED | PREV_BLOCK_ALLOCATED;
            }
        }
    } else {
        // Block not found so add page
        if (addMoreHeapSpace() == -1) {
            return NULL;
        }

        // After adding page jump to start of function again
        goto lookForBlockAgain;
    }

    // End of function so return
    return freeBlock;
}


// Malloc function that returns pointer to free block
void *sf_malloc(size_t size) {

    // Return NULL if size is 0
    if (size == 0) {
        return NULL;
    }

    // If first call then call sf_mem_grow
    if (sf_mem_start() == sf_mem_end()) {
        if (sf_init_page() == -1) {
            sf_errno = ENOMEM;
            return NULL;
        }
    }

    // Get actual block size needed
    size_t blockSize = size + 16;
    if (blockSize % 16 != 0) {
        blockSize += 16 - (blockSize % 16);
    }

    // Find smallest free list
    int listNum = 0;
    size_t listSize = 32;
    while (listSize < blockSize) {
        listSize *= 2;
        listNum++;
        if (listNum == 8) {
            break;
        }
    }

    // Find first avaliable block
    void *blockPtr = availableBlock(listNum, blockSize);
    if (blockPtr == NULL) {
        sf_errno = ENOMEM;
        return NULL;
    }

    // End of function so return
    return blockPtr + 16;
}


// Helper function for coalescing with previous block
void *coalesceWithPrevBlock(sf_block *currBlock) {

    // Get previous block using current block
    size_t prevBlockSize = (currBlock->prev_footer  ^ sf_magic()) & set2lsbstozero;
    sf_block *prevBlock = (void *)currBlock - prevBlockSize;

    removeFromFreeList(prevBlock, prevBlockSize);

    // Now make one big free block
    prevBlock->header = (prevBlock->header + (currBlock->header & set2lsbstozero)) & ~THIS_BLOCK_ALLOCATED;
    size_t newBlockSize = prevBlock->header & set2lsbstozero;
    sf_footer *currFooter = (void *)prevBlock + newBlockSize;
    *currFooter = prevBlock->header ^ sf_magic();

    // End of function so return
    return prevBlock;
}


// Helper function for coalescing with next block
void *coalesceWithNextBlock(sf_block *currBlock) {

    // Get next block using current block
    size_t currBlockSize = currBlock->header & set2lsbstozero;
    sf_block *nextBlock = (void *)currBlock + currBlockSize;
    size_t nextBlockSize = nextBlock->header & set2lsbstozero;

    removeFromFreeList(nextBlock, nextBlockSize);

    // Now make one big free block
    currBlock->header = (currBlock->header + (nextBlock->header & set2lsbstozero)) & ~THIS_BLOCK_ALLOCATED;
    size_t newBlockSize = currBlock->header & set2lsbstozero;
    sf_footer *currFooter = (void *)currBlock + newBlockSize;
    *currFooter = currBlock->header ^ sf_magic();

    // End of function so return
    return currBlock;
}


// Function for free block from heap
void sf_free(void *pp) {

    // If pointer is null then abort
    if (pp == NULL) {
        abort();
    }

    // Get current block pointer is pointing to
    sf_block *currBlock = pp - 16;
    size_t currBlockSize = currBlock->header & set2lsbstozero;
    sf_footer *currBlockFooter = (void *)currBlock + currBlockSize;

    // If block is current block is free then abort
    if ((currBlock->header & THIS_BLOCK_ALLOCATED) == 0) {
        abort();
    }

    // If block size is less than 32 then abort
    if (currBlockSize < 32) {
        abort();
    }

    // If block is before prologue then abort
    if ((void *)currBlock <= (void *)sf_pro) {
        abort();
    }

    // If block is after epilogue then abort
    if ((void *)currBlock >= (void *)sf_epi) {
        abort();
    }

    // If allocation of prev block is wrong then abort
    if ((currBlock->header & PREV_BLOCK_ALLOCATED) == 0) {
        if (((currBlock->prev_footer ^ sf_magic()) & THIS_BLOCK_ALLOCATED) != 0) {
            abort();
        }
    }

    // If footer is not correct then abort
    if (currBlock->header != (*currBlockFooter ^ sf_magic())) {
        abort();
    }

    // Pointer is valid so free and coalesce if needed
    sf_block *currBlockCoalesced = currBlock;
    if ((currBlock->header & PREV_BLOCK_ALLOCATED) == 0) {
        // Prev block is free
        currBlockCoalesced = coalesceWithPrevBlock(currBlock);
        currBlockSize = currBlockCoalesced->header & set2lsbstozero;
    }
    sf_block *nextBlock = (void *)currBlockCoalesced + currBlockSize;
    if ((nextBlock->header & THIS_BLOCK_ALLOCATED) == 0) {
        // Next block is free
        currBlockCoalesced = coalesceWithNextBlock(currBlockCoalesced);
    }

    // Add current free block to correct free list
    currBlock = currBlockCoalesced;
    currBlock->header = currBlock->header & ~THIS_BLOCK_ALLOCATED;
    currBlockSize = currBlock->header & set2lsbstozero;
    currBlockFooter = (void *)currBlock + currBlockSize;
    *currBlockFooter = currBlock->header ^ sf_magic();

    // Update next block header if any
    if ((void *)currBlock + currBlockSize != sf_mem_end() - 16) {
        nextBlock = (void *)currBlock + currBlockSize;
        nextBlock->header = nextBlock->header & ~PREV_BLOCK_ALLOCATED;
        size_t nextBlockSize = nextBlock->header & set2lsbstozero;
        sf_footer *nextBlockFooter = (void *)nextBlock + nextBlockSize;
        *nextBlockFooter = nextBlock->header ^ sf_magic();
    } else {
        sf_epi->header = THIS_BLOCK_ALLOCATED;
    }

    // Now add to correct free list
    addToFreeList(currBlock, currBlockSize);

    // End of function so return
    return;
}

// Helper function for coalescing with next block
void *coalesceForReallocate(sf_block *currBlock) {

    // Get next block using current block
    size_t currBlockSize = currBlock->header & set2lsbstozero;
    sf_block *nextBlock = (void *)currBlock + currBlockSize;
    size_t nextBlockSize = nextBlock->header & set2lsbstozero;

    removeFromFreeList(currBlock, currBlockSize);
    removeFromFreeList(nextBlock, nextBlockSize);

    // Now make one big free block
    currBlock->header = (currBlock->header + (nextBlock->header & set2lsbstozero)) & ~THIS_BLOCK_ALLOCATED;
    size_t newBlockSize = currBlock->header & set2lsbstozero;
    sf_footer *currFooter = (void *)currBlock + newBlockSize;
    *currFooter = currBlock->header ^ sf_magic();

    // End of function so return
    return currBlock;
}


// Helper function for reallocating to smaller block
void *reallocateToSmaller(sf_block *currBlock, size_t currBlockSize, size_t blockSize) {

    // See if split causes splinter
    if (currBlockSize - blockSize >= 32) {
        // Split since there will be no splinter
        splitBlock(blockSize, currBlock, currBlockSize - blockSize);

        // Coalesce if next block is free
        sf_block *freedBlock = (void *)currBlock + blockSize;
        size_t freedBlockSize = freedBlock->header & set2lsbstozero;
        sf_block *nextBlock = (void *)freedBlock + freedBlockSize;

        // Next block is free
        if ((nextBlock->header & THIS_BLOCK_ALLOCATED) == 0) {
            // Coalesce with next block
            currBlock = coalesceForReallocate(freedBlock);

            // Update the new coalesced block
            currBlock->header = currBlock->header & ~THIS_BLOCK_ALLOCATED;
            currBlockSize = currBlock->header & set2lsbstozero;
            sf_footer *currBlockFooter = (void *)currBlock + currBlockSize;
            *currBlockFooter = currBlock->header ^ sf_magic();

            // Now add to correct fee list
            addToFreeList(currBlock, currBlockSize);

            // Update next block header if any
            if ((void *)currBlock + currBlockSize != sf_mem_end() - 16) {
                nextBlock = (void *)currBlock + currBlockSize;
                nextBlock->header = nextBlock->header & ~PREV_BLOCK_ALLOCATED;
                size_t nextBlockSize = nextBlock->header & set2lsbstozero;
                sf_footer *nextBlockFooter = (void *)nextBlock + nextBlockSize;
                *nextBlockFooter = nextBlock->header ^ sf_magic();
            }
        }
    }

    // End of function so return
    return NULL;
}


// Helper function for reallocating to larger block
void *reallocateToLarger(void *ptr, size_t rsize, size_t blockSize) {

    // Reallocate to a larger size
    void *newPointer = sf_malloc(rsize);
    if (newPointer == NULL) {
        return NULL;
    }

    // Cop data to new block and free old block
    memcpy(newPointer, ptr, rsize);
    sf_free(ptr);

    // End of function so return
    return newPointer;
}


// Function for reallocating a block
void *sf_realloc(void *pp, size_t rsize) {

    // If pointer is null then //abort
    if (pp == NULL) {
        sf_errno = EINVAL;
        return NULL;
    }

    // Get current block pointer is pointing to
    sf_block *currBlock = pp - 16;
    size_t currBlockSize = currBlock->header & set2lsbstozero;
    sf_footer *currBlockFooter = (void *)currBlock + currBlockSize;

    // If block is current block is free then abort
    if ((currBlock->header & THIS_BLOCK_ALLOCATED) == 0) {
        sf_errno = EINVAL;
        return NULL;
    }

    // If block size is less than 32 then abort
    if (currBlockSize < 32) {
        sf_errno = EINVAL;
        return NULL;
    }

    // If block is before prologue then abort
    if ((void *)currBlock <= (void *)sf_pro) {
        sf_errno = EINVAL;
        return NULL;
    }

    // If block is after epilogue then abort
    if ((void *)currBlock >= (void *)sf_epi) {
        sf_errno = EINVAL;
        return NULL;
    }

    // If allocation of prev block is wrong then //abort
    if ((currBlock->header & PREV_BLOCK_ALLOCATED) == 0) {
        if (((currBlock->prev_footer ^ sf_magic()) & THIS_BLOCK_ALLOCATED) != 0) {
            sf_errno = EINVAL;
            return NULL;
        }
    }

    // If footer is not correct then //abort
    if (currBlock->header != (*currBlockFooter ^ sf_magic())) {
        sf_errno = EINVAL;
        return NULL;
    }

    // Size is 0 so free current block
    if (rsize == 0) {
        sf_free(pp);
        return NULL;
    }

    // Get actual block size needed
    size_t blockSize = rsize + 16;
    if (blockSize % 16 != 0) {
        blockSize += 16 - (blockSize % 16);
    }

    // If the size is same as the current block
    if (blockSize == currBlockSize) {
        return pp;
    }

    // Reallocate appropriately
    if (blockSize > currBlockSize) {
        // Reallocate to larger size
        pp = reallocateToLarger(pp, rsize, blockSize);
    } else {
        // Reallocate to a smaller size
        reallocateToSmaller(currBlock, currBlockSize, blockSize);
    }

    // End of function so return
    return pp;
}
