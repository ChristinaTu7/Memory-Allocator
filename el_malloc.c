// el_malloc.c: implementation of explicit list allocator functions.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "el_malloc.h"

// Global control functions

// Global control variable for the allocator. Must be initialized in
// el_init().
el_ctl_t el_ctl = {};

// Create an initial block of memory for the heap using mmap(). Initialize the
// el_ctl data structure to point at this block. The initial size/position of
// the heap for the memory map are given in the symbols EL_HEAP_INITIAL_SIZE
// and EL_HEAP_START_ADDRESS. Initialize the lists in el_ctl to contain a
// single large block of available memory and no used blocks of memory.
int el_init() {
    void *heap = mmap(EL_HEAP_START_ADDRESS, EL_HEAP_INITIAL_SIZE,
                      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(heap == EL_HEAP_START_ADDRESS);

    el_ctl.heap_bytes = EL_HEAP_INITIAL_SIZE; // make the heap as big as possible to begin with
    el_ctl.heap_start = heap; // set addresses of start and end of heap
    el_ctl.heap_end = PTR_PLUS_BYTES(heap, el_ctl.heap_bytes);

    if (el_ctl.heap_bytes < EL_BLOCK_OVERHEAD) {
        fprintf(stderr,"el_init: heap size %ld to small for a block overhead %ld\n",
                el_ctl.heap_bytes,EL_BLOCK_OVERHEAD);
        return -1;
    }

    el_init_blocklist(&el_ctl.avail_actual);
    el_init_blocklist(&el_ctl.used_actual);
    el_ctl.avail = &el_ctl.avail_actual;
    el_ctl.used = &el_ctl.used_actual;

    // establish the first available block by filling in size in
    // block/foot and null links in head
    size_t size = el_ctl.heap_bytes - EL_BLOCK_OVERHEAD;
    el_blockhead_t *ablock = el_ctl.heap_start;
    ablock->size = size;
    ablock->state = EL_AVAILABLE;
    el_blockfoot_t *afoot = el_get_footer(ablock);
    afoot->size = size;
    el_add_block_front(el_ctl.avail, ablock);
    return 0;
}


// Clean up the heap area associated with the system
void el_cleanup() {
    munmap(el_ctl.heap_start, el_ctl.heap_bytes);
    el_ctl.heap_start = NULL;
    el_ctl.heap_end = NULL;
}


// Pointer arithmetic functions to access adjacent headers/footers

// Compute the address of the foot for the given head which is at a higher
// address than the head.
el_blockfoot_t *el_get_footer(el_blockhead_t *head) {
    size_t size = head->size;
    el_blockfoot_t *foot = PTR_PLUS_BYTES(head, sizeof(el_blockhead_t) + size);
    return foot;
}


// TODO
// Compute the address of the head for the given foot, which is at a
// lower address than the foot.
el_blockhead_t *el_get_header(el_blockfoot_t *foot) {
    size_t size = foot->size;                                                          
    el_blockhead_t *header = PTR_MINUS_BYTES(foot, sizeof(el_blockhead_t) + size);   
    return header;                                                                      
}


// Return a pointer to the block that is one block higher in memory
// from the given block. This should be the size of the block plus
// the EL_BLOCK_OVERHEAD which is the space occupied by the header and
// footer. Returns NULL if the block above would be off the heap.
// DOES NOT follow next pointer, looks in adjacent memory.
el_blockhead_t *el_block_above(el_blockhead_t *block) {
    el_blockhead_t *higher = PTR_PLUS_BYTES(block, block->size + EL_BLOCK_OVERHEAD);
    if ((void *) higher >= (void*) el_ctl.heap_end) {
        return NULL;
    } else {
        return higher;
    }
}


// TODO
// Return a pointer to the block that is one block lower in memory
// from the given block. Uses the size of the preceding block found
// in its foot. DOES NOT follow block->next pointer, looks in adjacent
// memory. Returns NULL if the block below would be outside the heap.
//
// WARNING: This function must perform slightly different arithmetic
// than el_block_above(). Take care when implementing it.
el_blockhead_t *el_block_below(el_blockhead_t *block) {
    el_blockfoot_t *foot = PTR_MINUS_BYTES (block, sizeof(el_blockfoot_t));                          //get footer of preceding block

    if ((void*) foot < el_ctl.heap_start) {                                                          //check if footer is outside of heap bounds
        return NULL;
    }

    size_t decr_foot = foot->size; 
    el_blockhead_t *blockBelow = PTR_MINUS_BYTES (foot, decr_foot + sizeof(el_blockhead_t));         //decrement from the foot ptr, size of data block, and head block

    if ((void*) blockBelow < el_ctl.heap_start) {
        return NULL;
    }

    return blockBelow;
}


// Block list operations

// Print an entire blocklist. The format appears as follows.
//
// {length:   2  bytes:  3400}
//   [  0] head @ 0x600000000000 {state: a  size:   128}
//         foot @ 0x6000000000a0 {size:   128}
//   [  1] head @ 0x600000000360 {state: a  size:  3192}
//         foot @ 0x600000000ff8 {size:  3192}
//
// Note that the '@' column uses the actual address of items which
// relies on a consistent mmap() starting point for the heap.
void el_print_blocklist(el_blocklist_t *list) {
    printf("{length: %3lu  bytes: %5lu}\n", list->length, list->bytes);
    el_blockhead_t *block = list->beg;
    for (int i=0 ; i < list->length; i++) {
        printf("  ");
        block = block->next;
        printf("[%3d] head @ %p ", i, block);
        printf("{state: %c  size: %5lu}\n", block->state, block->size);
        el_blockfoot_t *foot = el_get_footer(block);
        printf("%6s", "");          // indent
        printf("  foot @ %p ", foot);
        printf("{size: %5lu}", foot->size);
        printf("\n");
    }
}


// Print out basic heap statistics. This shows total heap info along
// with the Available and Used Lists. The output format resembles the following.
//
// HEAP STATS (overhead per node: 40)
// heap_start:  0x600000000000
// heap_end:    0x600000001000
// total_bytes: 4096
// AVAILABLE LIST: {length:   2  bytes:  3400}
//   [  0] head @ 0x600000000000 {state: a  size:   128}
//         foot @ 0x6000000000a0 {size:   128}
//   [  1] head @ 0x600000000360 {state: a  size:  3192}
//         foot @ 0x600000000ff8 {size:  3192}
// USED LIST: {length:   3  bytes:   696}
//   [  0] head @ 0x600000000200 {state: u  size:   312}
//         foot @ 0x600000000358 {size:   312}
//   [  1] head @ 0x600000000198 {state: u  size:    64}
//         foot @ 0x6000000001f8 {size:    64}
//   [  2] head @ 0x6000000000a8 {state: u  size:   200}
//         foot @ 0x600000000190 {size:   200}
void el_print_stats() {
    printf("HEAP STATS (overhead per node: %lu)\n", EL_BLOCK_OVERHEAD);
    printf("heap_start:  %p\n", el_ctl.heap_start);
    printf("heap_end:    %p\n", el_ctl.heap_end);
    printf("total_bytes: %lu\n", el_ctl.heap_bytes);
    printf("AVAILABLE LIST: ");
    el_print_blocklist(el_ctl.avail);
    printf("USED LIST: ");
    el_print_blocklist(el_ctl.used);
}


// Initialize the specified list to be empty. Sets the beg/end
// pointers to the actual space and initializes those data to be the
// ends of the list. Initializes length and size to 0.
void el_init_blocklist(el_blocklist_t *list) {
    list->beg = &(list->beg_actual);
    list->beg->state = EL_BEGIN_BLOCK;
    list->beg->size = EL_UNINITIALIZED;
    list->end = &(list->end_actual);
    list->end->state = EL_END_BLOCK;
    list->end->size = EL_UNINITIALIZED;
    list->beg->next = list->end;
    list->beg->prev = NULL;
    list->end->next = NULL;
    list->end->prev = list->beg;
    list->length = 0;
    list->bytes = 0;
}


// TODO
// Add to the front of list; links for block are adjusted as are links
// within list. Length is incremented and the bytes for the list are
// updated to include the new block's size and its overhead.
void el_add_block_front(el_blocklist_t *list, el_blockhead_t *block) {
    //makes sure you can actually do it
    if (list == NULL || block == NULL) {
        return;
    }

    //Block Related
    block->next = list->beg->next;                          //new block points to first real block
    block->prev = list->beg;                                //new block's previous is the beginning dummy node

    //List Related Adjustments
    list->beg->next = block;                                //update beginning node to point to new block
    block->next->prev = block;                              //makes sure the next node is pointing to the new block

    //Update Length and Byte
    list->length++;                                         //increment count of blocks in the list
    list->bytes += block->size + EL_BLOCK_OVERHEAD;         //add size of new block + overhead to total byte
}


// TODO
// Unlink block from the specified list.
// Updates the length and bytes for that list including
// the EL_BLOCK_OVERHEAD bytes associated with header/footer.
void el_remove_block(el_blocklist_t *list, el_blockhead_t *block) {
    if (block == list->beg || block == list->end) {             //ensure dummy node is not removed
      return;
    }

    if (list == NULL || block == NULL) {                        //check against null pointers
        return;
    }

    block->next->prev = block->prev;
    block->prev->next = block->next;

    list->length--;                                             //update list length
    
    if ( list->bytes < (block->size + EL_BLOCK_OVERHEAD) ){     //making sure bytes is updated correctly
        list->bytes = 0;
    }
    else{
        list->bytes -= (block->size + EL_BLOCK_OVERHEAD);       //update total bytes
    }
}


// Allocation-related functions

// TODO
// Find the first block in the available list with block size of at
// least (size + EL_BLOCK_OVERHEAD). Overhead is accounted for so this
// routine may be used to find an available block to split: splitting
// requires adding in a new header/footer. Returns a pointer to the
// found block or NULL if no of sufficient size is available.
el_blockhead_t *el_find_first_avail(size_t size) {
    size_t requiredSize = size + EL_BLOCK_OVERHEAD;
    el_blockhead_t *current = el_ctl.avail->beg;                //start from beginning of list

    while (current != NULL) {                                   //traverse through list
        if (current->size >= requiredSize) {                    //find a block with the required size
            return current;
        }
        current = current->next;
    }   

    return NULL;
}


// TODO
// Set the pointed to block to the given size and add a footer to it. Creates
// another block above it by creating a new header and assigning it the
// remaining space. Ensures that the new block has a footer with the correct
// size. Returns a pointer to the newly created block while the parameter block
// has its size altered to parameter size. Does not do any linking of blocks.
// If the parameter block does not have sufficient size for a split (at least
// new_size + EL_BLOCK_OVERHEAD for the new header/footer) makes no changes and
// returns NULL.
el_blockhead_t *el_split_block(el_blockhead_t *block, size_t new_size) {
    if (block == NULL) {                                                                 
        return NULL;
    }

    size_t minimum_size = new_size + EL_BLOCK_OVERHEAD;                                    //check there is sufficient size
    if (block->size < minimum_size) {
        return NULL;
    }

    //adjust old block
    size_t newBlockSize = block->size - new_size - EL_BLOCK_OVERHEAD;                      //calc size of new block
    block->size = new_size;                 
    el_blockfoot_t *origFooter = el_get_footer(block);                                     //update footer for resized orig block
    origFooter->size = new_size;

    //adjust new block
    el_blockhead_t *newBlock = PTR_PLUS_BYTES(origFooter, sizeof(el_blockfoot_t));         //find the block above it
    newBlock->size = newBlockSize;
    newBlock->state = EL_AVAILABLE;

    el_blockfoot_t *newFooter = el_get_footer(newBlock);                                   //set new block's footer
    newFooter->size = newBlockSize;

    return newBlock;

}


// TODO
// Return pointer to a block of memory with at least the given size
// for use by the user. The pointer returned is to the usable space,
// not the block header. Makes use of find_first_avail() to find a
// suitable block and el_split_block() to split it. Returns NULL if
// no space is available.
void *el_malloc(size_t nbytes) {
    if (nbytes == 0) {   
        return NULL;
    }

    el_blockhead_t *block = el_find_first_avail(nbytes);

    if (!block) {       
        return NULL;
    }

    el_remove_block(el_ctl.avail, block);                                      //remove orig block from avail list
    el_blockhead_t *remaining_block = el_split_block(block, nbytes);           //if block is larger than needed then split it

    if (remaining_block != NULL) {                         
        el_add_block_front(el_ctl.avail, remaining_block);                     //manage new remaining block in the avail list
        remaining_block->state = EL_AVAILABLE;
    }

    el_add_block_front (el_ctl.used, block);                                   //add resized orig block to used list

    block->state = EL_USED; 
    return PTR_PLUS_BYTES(block, sizeof(el_blockhead_t));                      //return a pointer to usable area of block
}

// De-allocation/free() related functions

// TODO
// Attempt to merge the block 'lower' with the next block in memory. Does
// nothing if lower is NULL or not EL_AVAILABLE and does nothing if the next
// higher block is NULL (because lower is the last block) or not EL_AVAILABLE.
//
// Otherwise, locates the next block with el_block_above() and merges these two
// into a single block. Adjusts the fields of lower to incorporate the size of
// higher block and the reclaimed overhead. Adjusts footer of higher to
// indicate the two blocks are merged. Removes both lower and higher from the
// available list and re-adds lower to the front of the available list.
void el_merge_block_with_above(el_blockhead_t *lower) {
    if (lower == NULL || lower->state != EL_AVAILABLE) {
        return;
    }
    
    el_blockhead_t *higher = el_block_above(lower);                                //checks the higher block

    if (higher == NULL || higher->state != EL_AVAILABLE) {
        return;
    }
    el_remove_block(el_ctl.avail, higher);                                         //remove the higher block from available list 
    el_remove_block(el_ctl.avail, lower);                                          //remove the lower block from available list 
    size_t new_size = lower->size + higher->size + EL_BLOCK_OVERHEAD;              //calculate the new size

    el_blockfoot_t *higher_footer = el_get_footer(higher);                         //get the footer of the higher block
    lower->size = new_size;                                                        //update the size of lower
    higher_footer->size = new_size;                                                //update the footer of the higher block

    el_add_block_front(el_ctl.avail, lower);                                       //readd the updated lower
}




// TODO
// Free the block pointed to by the given ptr. The area immediately
// preceding the pointer should contain an el_blockhead_t with information
// on the block size. Attempts to merge the free'd block with adjacent
// blocks using el_merge_block_with_above().
void el_free(void *ptr) {
    if (ptr == NULL) {
        return; 
    }

    el_blockhead_t *block = PTR_MINUS_BYTES(ptr, sizeof(el_blockhead_t));          //calculate Start of Header by Subtracting size of header

    if (block->state == EL_AVAILABLE) {                                            //check if the block is already free
        return;
    }

    block->state = EL_AVAILABLE;                                                   //mark the block as available

    el_remove_block(el_ctl.used, block);                                           //remove the block from used list and add to available list
    el_add_block_front(el_ctl.avail, block);

    el_merge_block_with_above(block);                                              //merge with the block above
    el_blockhead_t *below = el_block_below(block);
    el_merge_block_with_above(below);
}