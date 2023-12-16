/*
 * mm.c
 *
 * Name: Shiao Zhuang
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 * Also, read malloclab.pdf carefully and in its entirety before beginning.
 *
 * I majorly referenced the textbook "A Programmer's Perspective 3rd ed", and also the review session slides. Also Thanks to Professor Julia, TA Youssef & Joshua provides me lots of help on understanding many specific details.
 * 
 * The premitive data structure I used is exactly same as the slides(paddings, prologue, header, usr space, footer, epilogue).
 * 
 * Improvements:
 * 1. find_fit: find the best fit WITHIN the first "max_fit_count" fit block
 * 2. coalesce
 * 3. segregated explicit free list: division of sizes that the free list can hold is -> 2^(i) to 2^(i+1) , you can modify the number of classes by changing NUM_CLASSES 
 * 4. footer optimization: central logic ->  when using malloc or free, set prev_alloc_bit to the next block's header's 2nd lowest bit. Then adapt all other function based on that.
 * 
 * 
 * Pity and further improvements:
 * 1. Introduce small blocks with only 16 bytes (Header + 8 bytes userSpace) After doing data analysis on all trace files, I found that a great portion of the allocate/reallocate requests centers from 1 byte to 24 bytes. 
 * I do believe it's the footer optimization that helps me digest those nearly 150000 times 24 bytes allocate requests and boost my score immediately. 
 * 
 * 2. Revise the realloc: So far I only implemented a simple realloc, which is to always malloc a new block and memcpy the old block's content to the new block. 
 * I tried to combine curr + next block when the re-alloc size is smaller than the curr block size, but the utilization didn't get improved. Thus, need further investigation.
 * 
 * 
 * It's a chanllenaging and interesting lab, I'm really suffering from & having "fun" with it :)
 * 
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include "mm.h"
#include "memlib.h"

/*
 * If you want to enable your debugging output and heap checker code,
 * uncomment the following line. Be sure not to have debugging enabled
 * in your final submission.
 */
// #define DEBUG

#ifdef DEBUG
// When debugging is enabled, the underlying functions get called
#define dbg_// printf(...) // printf(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#else
// When debugging is disabled, no code gets generated
#define dbg_// printf(...)
#define dbg_assert(...)
#endif // DEBUG

// do not change the following!
#ifdef DRIVER
// create aliases for driver tests
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mm_memset
#define memcpy mm_memcpy
#endif // DRIVER

#define ALIGNMENT 16

// struct for the explicit free list's each node
typedef struct node{
    struct node* prev; 
    struct node* next; 
} node_t; 

//node_t* free_list_head; // global variable to keep track of the head of the free list

enum {
/* 
num of classes = log2(max_block_size / min_block_size) 
Current min & max blk sizes:2^5 -> 2^15 + 1 list for more than 2^15 = 11
*/
    NUM_CLASSES = 10
};

typedef struct free_list_class {
    node_t* head;
    //bool init;
} class_t;

// declare gloabl free list classes
class_t freelist_classes[NUM_CLASSES]; 

// init free list head
void init_freelists() {
    for (size_t i = 0; i < NUM_CLASSES; i++) {
        freelist_classes[i].head = NULL;
        // freelist_classes[i].init = false;
    }
}


// rounds up to the nearest multiple of ALIGNMENT
static size_t align(size_t x)
{
    return ALIGNMENT * ((x+ALIGNMENT-1)/ALIGNMENT);
}

// Gain idea from the textbook, reformated the marco to static function
static void PUT(void* p, size_t val) {
    *((size_t*)p) = val;
}

// PACK for OR the size and allocated bit
// Gain idea from the text book, reformated the marco to static function
static uint64_t PACK(size_t size, size_t alloc) {
    return (size | alloc);
}

enum {
    BSIZE = 8 // representing block size no matter header/footer/prologue/epilogue/paddings...
    
};
enum {
    MIN_BLOCK_SIZE = 32 // minimum block size

};
void* heap_curr; // global variable to keep track of the current heap position


/*
 * Returns whether the pointer is in the heap.
 * May be useful for debugging.
 */
static bool in_heap(const void* p)
{
    return p <= mm_heap_hi() && p >= mm_heap_lo();
}

/* DO NOT USE 0xFFFFFFF0 !!!!!!!!!!!! IT GOES WRONG WHEN ENCOUNTERING LARGE NUMBERS!!!!!!!!!!!!

extend_heap (size=4294983696) at mm.c:310
310         bp = mm_sbrk(size);

(gdb) next
311         if (bp == (void *)-1)

(gdb) 
315         PUT((char *)bp - BSIZE, size);// New block header - not allocated

(gdb) 
316         PUT((char *)bp + size - 2 * BSIZE, PACK(size, 0)); // New block footer - not allocated

(gdb) print(*(size_t *)((char *)bp - BSIZE) & ~0x7)
$4 = 4294983696

(gdb) print(*(size_t *)((char *)bp - BSIZE) & 0xFFFFFFF0)
$5 = 16400

SEE THE DIFFERENCE HERE!!!RERERERE
*/

// get block content from its header
static size_t get_header_content(void* ptr){
    return *(size_t *)((char *)ptr - BSIZE);
}

// get block size from its header; define block as (header + usr space + footer)
static size_t get_blk_size(void* ptr){
    return *(size_t *)((char *)ptr - BSIZE) & ~0x7; // get rid of the alloc bit(last 4 bits for now)
}

static size_t get_lst_blk_size(void* ptr){ // via last block's footer
    return *(size_t *)((char *)ptr - 2 * BSIZE) & ~0x7; // get rid of the alloc bit(last 4 bits for now)
}

static size_t get_next_blk_size(void* ptr){ // via next block's header
    return *(size_t *)((char *)ptr + get_blk_size(ptr) - BSIZE) & ~0x7; // get rid of the alloc bit(last 4 bits for now) 
}

// get block alloc status from its header in bool; (1 is true, 0 is false)
static bool get_blk_alloc(void* ptr){
    return *(size_t *)((char *)ptr - BSIZE) & (0x1); // get last bit
}

static bool get_lst_blk_alloc(void* ptr){ // via last block's footer
    return *(size_t *)((char *)ptr - 2 * BSIZE) & (0x1); // get last bit
}

static bool get_next_blk_alloc(void* ptr){ // via next block's footer
    return *(size_t *)((char *)ptr + get_blk_size(ptr) - BSIZE) & (0x1); // get last bit
}

// input: start of blk user space; get prev block alloc status from current header
static bool get_prev_alloc_bit(void* ptr){
    return *(size_t *)((char *)ptr - BSIZE) & (0x2); // get second last bit
}
// // check if is the end of the heap (by epilogue)
static bool is_epilogue(void* ptr){
    if (get_blk_size(ptr) == 0 && get_blk_alloc(ptr) == 1){
        return true;
    }
    return false ; 
}
// function for get the next block's usr space
static void *get_next_blk(void *ptr){
    if (ptr == NULL) return NULL;

    return (char *)ptr + get_blk_size(ptr);
}


// Helper function to extend the heap by size bytes and initialize the header/footer of the new user space
static void *extend_heap(size_t size) {
    char *bp; // the block pointer to be returned by sbrk
    // size_t alloc_size; // header + size + footer

    // to maintain 16 byte alignment for request allocation(usr space)
    // didn't check if already aligned or not, may need to be improved later
    // alloc_size = align(size);
    // alloc_size += 2 * BSIZE; // size + (header + footer)

    // Request memory from the system
    bp = mm_sbrk(size);
    if (bp == (void *)-1)
        return NULL;

    // Initialize the header and footer of the new block
    PUT((char *)bp - BSIZE, PACK(size, 0));// New block header - not allocated
    // PUT((char *)bp + size - 2 * BSIZE, PACK(size, 0)); // New block footer - not allocated
    PUT((char *)bp + size - BSIZE, PACK(0, 3)); // New epilogue header
    
    
    return bp;
}

/* 
input: block size that what to look for
output: the index of the free list class that the block belongs to
index range: 0 - 10
range explaination:
in each class, the block size range is from 2^(i+5) to 2^(i+6)
i = index; 5 = min block size
example: i = 0, block size range: 2^5 - 2^6
to be notice: 1. the largest class is 2^(NUM_CLASSES - 1 + 5) to infinity, which is the last class
              2. the smallest class is 2^5 - 2^6, BUT it does CONTAIN ANY blocks size <= 2^5 = 32
*/
static int get_class_index(size_t size) {
    size_t i;
    size_t class_index = NUM_CLASSES - 1; // default to the largest/last class

    /* get the index by suitable range, if no suitable, retunr   */
    for (i = 0; i < NUM_CLASSES - 1; i++) {
        if (size < (size_t)(1 << (i + 1 + 5))) { // i here represent the power of 2: 2^(power); +5 means start from 2^5, can be changed
            class_index = i;
        
            break;
        }

    }
    return (int)class_index;
}

// unlink the node from the free list
static void unlink_node(node_t *node){
    int class_index = get_class_index(get_blk_size(node));
    // class_index = 0;
    // printf("unlink_node size: %zu\n", get_blk_size(node));
    // printf("class_index: %zu\n", class_index);
    if (node->prev == NULL && node->next == NULL){
        // if this is the last node in the free list,clean the head
        freelist_classes[class_index].head = NULL;
    }
    else if(node == freelist_classes[class_index].head){
        node->next->prev = NULL;
        freelist_classes[class_index].head = node->next;
    }
    else if (node->next == NULL){
        node->prev->next = NULL;
    }
    else{
        node->prev->next = node->next;
        node->next->prev = node->prev; 
    }
}


// link the node from the free list
static void link_node(void *node) {
    int class_index = get_class_index(get_blk_size(node));
    // class_index = 0;
    node_t *new_node = (node_t *)node;
    new_node->next = freelist_classes[class_index].head;
    // printf("new_node->next: %p\n", new_node->next);
    new_node->prev = NULL;
    if (freelist_classes[class_index].head != NULL){
        // printf("new_node: %p\n", new_node);
        freelist_classes[class_index].head->prev = new_node;
        // printf("free_list_head->prev: %p\n", free_list_head->prev);
    }
    freelist_classes[class_index].head = new_node;
}


/*
 * mm_init: returns false on error, true on success.
 */

bool mm_init(void)
{   
    // init free list head
    for (int i = 0; i < NUM_CLASSES; i++) {
        freelist_classes[i].head = NULL;
        // freelist_classes[i].init = false;
    }

    // printf("class_index: %zu\n", get_class_index(16384));
    // init space for pro/epilogue & padding (32 bytes)
    heap_curr = mm_sbrk(4 * BSIZE);

    // check succuess or not on sbrk
    if (heap_curr == (void*)-1) {
        return false;
    }

    // printf("heap_curr-1: %p\n", heap_curr);

    PUT(heap_curr, 0);// Padding, is filling 0s necessary?
    PUT(heap_curr + (1 * 8), PACK((2 * BSIZE), 1));// Prologue header
    PUT(heap_curr + (2 * 8), PACK((2 * BSIZE), 1));// Prologue footer
    PUT(heap_curr + (3 * 8), PACK(0, 3)); // Epilogue header: prev_alloc_bit = 1, self-alloc_bit = 1
    
    heap_curr += (4 * BSIZE); // for checkHeap
    
    // NOTICE: the epilogue will be initialized when extend the heap

    /* init the SINGLE explict free list by open a free minimum size block and insert it to the list */
        // heap_curr = extend_heap(MIN_BLOCK_SIZE);

        
        // node_t* free_block_node = (node_t*)heap_curr; // store at the start of the free block usr space
        // // printf("free_block_node: %p\n", free_block_node);
        // free_block_node->prev = NULL;
        // free_block_node->next = NULL;
        
        // free_list_head = free_block_node;
    
    
    /* init the segregated explict free list*/
        // init_freelists();
        // extend heap for every class and link the free block to the list
        // for (size_t i = 0; i < NUM_CLASSES; i++) {
        //     void* curr;
        //     if ((curr = extend_heap((size_t)(1 << (i + 5)))) == NULL) {
        //         return false;
        //     }
        //     // link_node(curr, i);
        //     node_t* free_block_node = (node_t*)curr; // store at the start of the free block usr space
        //     // printf("free_block_node: %p\n", free_block_node);
        //     free_block_node->prev = NULL;
        //     free_block_node->next = NULL;
            
        //     freelist_classes[i].head = free_block_node;
        //     // printf("freelist_classes[%zu].head: %p\n", i, freelist_classes[i].head);
        // }
    return true;
}



// find the best (first) fit of free blk for malloc
static char *find_fit(size_t alloc_adj_size){
    // start with the first block's usr space OR the end of epilogue
    int expected_index = get_class_index(alloc_adj_size);

    /*EXPLICIT FREE LIST: find the first fit*/

    // for (int i = expected_index; i < NUM_CLASSES; i++) {
    //     for (node_t *curr = freelist_classes[i].head; in_heap(curr) && curr != NULL; curr = curr->next){
        
    //         if (get_blk_size(curr) >= alloc_adj_size){
    //             // unlink the node from the free list
    //             unlink_node(curr);
    //             return (char *)curr;
    //         }
    //     }
    // }
    // return NULL;
    

    /*EXPLICIT FREE LIST: find the best fit within the first "max_fit_count" fit*/
    /* After testing, set max_fit_count to 6 provides the best balance between utilization & throughput*/
    int fit_count = 0;
    node_t* smallest_fit = NULL;
    int max_fit_count = 6;

    for (int i = expected_index; i < NUM_CLASSES; i++) {
        for (node_t* curr = freelist_classes[i].head; in_heap(curr) && curr != NULL; curr = curr->next) {
            if (get_blk_size(curr) >= alloc_adj_size) {
                fit_count++;
                if (smallest_fit == NULL || get_blk_size(curr) < get_blk_size(smallest_fit)) {
                    smallest_fit = curr;
                }
                if (fit_count == max_fit_count) {
                    // Unlink the smallest fit node from the free list
                    unlink_node(smallest_fit);
                    return (char *)smallest_fit;
                }
            }
        }
    }

    if (smallest_fit != NULL) {
        // if overall_fit_count < max_fit_count
        unlink_node(smallest_fit);
    }
    // the smallest fit CAN BE NULL
    return (char *)smallest_fit;
}
// } 



static void *coalesce(void *bp)
{
    // printf("coalescing\n");
    bool prev_alloc = get_prev_alloc_bit(bp); // h + usr + f + h +(bp)usr + f BY FOOTER
    size_t next_alloc = get_blk_alloc(get_next_blk(bp));
    size_t curr_size = get_blk_size(bp);

    if (prev_alloc && next_alloc){
        // if all allocated, then no coalesce
        return NULL;
    }

    if (!prev_alloc && next_alloc){
        // if prev not alloc, but next alloced
        size_t prev_size = get_lst_blk_size(bp); // get last blk's size BY FOOTER
        size_t new_size = curr_size + prev_size;

        
        // unlink the prev node
        unlink_node((node_t *)(bp - prev_size));
        // set header based on prev prev alloc bit

        /* The LAST 4 lines of comment is an unnessary implement, because there should be no prev-prev blk that is free, due to prev-coaleace*/
        // bool prev_prev_alloc_bit = get_prev_alloc_bit(bp - prev_size);
        // if (prev_prev_alloc_bit == 0){
        //     PUT((char *)bp - prev_size - BSIZE, PACK(new_size, 0)); // set new header: prev_alloc bit = 0 ; self-alloc = 0
        // }else{

            PUT((char *)bp - prev_size - BSIZE, PACK(new_size, 2)); // PUT(new header, prev_alloc bit = 1 ; self-alloc = 0)
        // }

        PUT((char *)bp + curr_size - 2 * BSIZE, PACK(new_size, 0)); // PUT(new footer, new size)
        
        // PUT next blk's prev_alloc bit to 0
        PUT((char *)bp + curr_size - BSIZE, (get_header_content(get_next_blk(bp))^2) ); // PUT(next header, next size)

        // coalesce the free linked list nodes
        // node_t *curr_node = (node_t *)bp;
        // unlink_node(curr_node);
        // node_t *prev_node = (node_t *)(bp - prev_size); // or = curr_node->prev ?? NO IT"S NOT!!!!

        

        bp = (char *)bp - prev_size;
        link_node((node_t *)(bp));
        return bp;
    }
    if (prev_alloc && !next_alloc){
        // if prev is alloc, but next NOT alloced

        //get next blk pointer
        size_t *next_blk = get_next_blk(bp);
        size_t next_size = get_blk_size(next_blk);
        size_t new_size = curr_size + next_size;

        PUT((char *)bp - BSIZE, PACK(new_size, 2)); // PUT(new header, new size)
        PUT((char *)bp + new_size - 2 * BSIZE, PACK(new_size,0)); // PUT(new footer, new size)

        /* NO NEED, since the next blk already set free to its next blk before: PUT next blk's prev_alloc bit to 0 */
        // PUT((char *)bp + new_size - BSIZE, (get_header_content(get_next_blk(next_blk))^2) ); // PUT(next header, next size)

        //if is end of the blocks
        node_t *next_node = (node_t *)next_blk;
        unlink_node(next_node);

        link_node(bp);
        
        return bp;
    }
    if (!prev_alloc && !next_alloc){
        // if both prev and next are not alloced
        size_t prev_size = get_lst_blk_size(bp); // get last blk's size BY FOOTER
        size_t next_size = get_blk_size(get_next_blk(bp));
        size_t new_size = curr_size + prev_size + next_size;

        node_t *next_node = (node_t *)get_next_blk(bp);
        unlink_node(next_node);
        unlink_node((node_t *)((char *)bp - prev_size));
        PUT((char *)bp - prev_size - BSIZE, PACK(new_size, 2)); // PUT(new header, new size with prev_alloc bit = 1)
        PUT((char *)bp + curr_size + next_size - 2 * BSIZE, PACK(new_size,0)); // PUT(new footer, new size)
        /* NO NEED, since the next blk already set free to its next blk before: PUT next blk's prev_alloc bit to 0 */
        // PUT((char *)bp + curr_size + next_size - BSIZE, get_header_content(get_next_blk(next_node))^2 ); 
        
        // go to the prev node and link it
        bp = (char *)bp - prev_size;
        link_node(bp);
        return bp;
    }
    return NULL;
}




/*
 * malloc
 */
void* malloc(size_t size)
{
    // IMPLEMENT THIS
    char *bp; // the block pointer to be returned by sbrk, 
    // size_t adj_size; // adjusted size to maintain 16 byte alignment
    size_t alloc_adj_size; // adjusted size to include header/footer
    // check if size is 0
    if (size == 0) {
        return NULL;
    }

    // adj_size = align(size);
    alloc_adj_size = align(size + BSIZE); // size + (header +usr space+ footer)

    /* IMPORTANT: Ensure the MIN_BLOCK_SIZE is still 32!
        REASON: The HEADER = 8 bytes, AND the NODE STRUCT = 16 bytes!
        So the MIN_BLOCK_SIZE has to be align(Header + Node struct) = 32 bytes still!
    */
    if (alloc_adj_size < MIN_BLOCK_SIZE){
        alloc_adj_size = MIN_BLOCK_SIZE;
    }

    // (TBD) find best fit free block
    if ((bp = find_fit(alloc_adj_size)) != NULL){
        /* bp May Return BIGGER than REQUEST*/
        /* split the block if too large */
        size_t allocated_size = get_blk_size(bp);
        size_t remainder_size = allocated_size - alloc_adj_size;
        void * next_blk = get_next_blk(bp);
        if (remainder_size >= MIN_BLOCK_SIZE){
        // if (false){
            // adjusted original block: Initialize the header and footer 
            bool prev_alloc_bit = get_prev_alloc_bit(bp);

            PUT((char *)bp - BSIZE, PACK(alloc_adj_size, 1+2*(int)prev_alloc_bit)); // New block header - prev_block_alloc=0 + self-allocated
            

            // adjusted remainder block: Initialize the header and footer
            PUT((char *)bp + alloc_adj_size - BSIZE, PACK(remainder_size, 2));// New block header - prev_block_alloc=1 + self-free
            PUT((char *)bp + allocated_size - 2 * BSIZE, PACK(remainder_size, 0)); // New block footer - free ; (== bp + alloc_adj_size + remainder_size - 2 * BSIZE)

            /* I think the next line is needed, but it didn't improve anything after adding it */
            PUT((char *)bp + allocated_size  - BSIZE, get_header_content(next_blk)^2); // add prev_alloc bit to the remainder block's next block's header!!!!!!
            /* Failed: try coalesce for remainder block, but ZERO Improvement gain*/
            // void * new_bp;
            // if ((new_bp = coalesce(get_next_blk(bp))) != NULL){
            //     return (void *)new_bp;
            // }

            // add remainder block to free list
            link_node(get_next_blk(bp));

            mm_checkheap(__LINE__);

            return (void *)bp;
            

        }
        else{
            void* next_blk = get_next_blk(bp);
             // whole find_fit block: Initialize the header and footer
            PUT((char *)bp - BSIZE, PACK(get_header_content(bp), 1));// New block header - prev_block_alloc + self-allocated
            PUT((char *)next_blk - BSIZE, PACK(get_header_content(next_blk), 2)); // NEXT block header - allocated
            
            mm_checkheap(__LINE__);

            return (void *)bp;
        }
    }
    /* Extend Heap */
    // extend heap & check NULL for extend_heap
    if ((bp = extend_heap(alloc_adj_size)) == NULL) {
        return NULL;
    }
    
    // Initialize the header and footer of the new block

    // get the prev_alloc bit of the new block by check the next block's prev_alloc bit
    bool prev_alloc_bit = get_prev_alloc_bit((char *)bp + alloc_adj_size);
    
    /* here is a smarter way than this if-else, but need to double check: PUT((char *)bp - BSIZE, PACK(alloc_adj_size, 1+2*(int)prev_alloc_bit));*/
    /* implemented the above thought*/
    PUT((char *)bp - BSIZE, PACK(alloc_adj_size, 1+2*(int)prev_alloc_bit));// New block header - prev_block_alloc=0 + self-allocated
    
    // the line below is not need, since the epilogue block is already set in the extent_heap func
        PUT((char *)bp + alloc_adj_size - BSIZE, PACK(0, 3)); // Epilogue Header: prev_block_alloc  +  self-allocated

    mm_checkheap(__LINE__);

    return (void *)bp;
    
}



/*
 * free
 */
void free(void* ptr)
{
    // IMPLEMENT THIS

    // check NULL ptr
    if (ptr == NULL) {
        return;
    }

    // check if ptr is in the heap
    if (in_heap(ptr) == false ){
        return;
    }

    // get the size of the block
    size_t size = get_blk_size(ptr); // get rid of the alloc bit(LAST 3 BITS for now)


    if (coalesce(ptr) == NULL){
        // flip the header's alloc
        bool prev_alloc_bit = get_prev_alloc_bit(ptr);
        PUT((char *)ptr - BSIZE, PACK(size, 2*(size_t)prev_alloc_bit)); // PUT(header, original header content with self-alloc bit = 0))) 
        // flip the footer's alloc
        PUT((char *)ptr + size - 2 * BSIZE, size); // PUT(footer, size with alloc bit flipped)

        // get the next block
        void *next_blk = get_next_blk(ptr);
        // set the next block's prev_alloc bit to 0 by XOR the original content with
        PUT((char *)ptr + size  - BSIZE, get_header_content(next_blk) ^ 2); // PUT(next_blk_header, original next_blk_header content with prev_alloc bit flipped)

        /*add to the free list*/
        
        link_node(ptr);

        mm_checkheap(__LINE__);

    }
    mm_checkheap(__LINE__);
}

/*
 * realloc
 */
void* realloc(void* oldptr, size_t size)
{
    // IMPLEMENT THIS
    // size_t adj_size; // adjusted size to maintain 16 byte alignment
    size_t alloc_adj_size; // adjusted size to include header/footer
    size_t old_size; // whole size of the old block
    
    // if ptr is NULL
    if (oldptr == NULL) {
        return malloc(size);
    }
    // if size is 0
    if (size == 0) {
        free(oldptr);
        return NULL;
    }   

    /* IMPORTANT: Ensure the MIN_BLOCK_SIZE is still 32!
        REASON: The HEADER = 8 bytes, AND the NODE STRUCT = 16 bytes!
        So the MIN_BLOCK_SIZE has to be align(Header + Node struct) = 32 bytes still!
    */
    alloc_adj_size = align(size + BSIZE); // size + (header + footer)
    if (alloc_adj_size < MIN_BLOCK_SIZE){
        alloc_adj_size = MIN_BLOCK_SIZE;
    }

    old_size = get_blk_size(oldptr); // get the size of the old block


    // if size is smaller than the old block
    // BAD approch below, fragmentation occurs
    // if (alloc_adj_size <= old_size) {
    //     return oldptr;
    // }
    // BETTER approch below, no fragmentation, need colaesce and free list to reuse the freed space later
    // if (alloc_adj_size <= old_size) {
    //     free(oldptr);
    // }


    
    
    if (alloc_adj_size <= old_size) {
        // if size is smaller than the old block
        void *newptr = malloc(size); 
        if (newptr == NULL) { // malloc check
            return NULL;
        }

        memcpy(newptr, oldptr, alloc_adj_size -  BSIZE); // bug solve: overlap by wrt old size to smaller newAloc, then next block's header is overwritten
        free(oldptr);
        return newptr;  
        // ONLY copy the old block WITH NEW SHRINKED SIZE to the new block!!!
    } else{

        /* the following commented area is waiting to be improved, for details, go to the top of the whole script to check out why */
        /* if size is greater than the old block */

        // check if the next block is free
        // void *next_blk = get_next_blk(oldptr);
        // size_t next_blk_size = get_blk_size(next_blk);
        // size_t next_blk_alloc = get_blk_alloc(next_blk);
        // // size_t next_blk_header = get_header_content(next_blk);
        // void *next_next_blk = (char *)next_blk + next_blk_size;

        // if (next_blk_alloc == 0 && next_blk_size > (alloc_adj_size - old_size)) {
        //     // if the next block is free and (curr+next) has enough space to fit the new block
        //     // remove the next block from the free list
        //     unlink_node(next_blk);
        //     // set the header of the new block
        //     PUT((char *)oldptr - BSIZE, PACK((old_size+next_blk_size), 3)); // PUT(header, size with alloc bit = 1)
        //     // set the header of the next block's next blk
        //     PUT((char *)next_blk + next_blk_size - BSIZE, PACK(get_header_content(next_next_blk),2)); // PUT(next_blk_footer, size with prev_alloc bit = 1)
        //     // return the oldptr
        //     return oldptr;
        // }
        // else{ // malloc new place and integrate it to there
            void *newptr = malloc(size);
            if (newptr == NULL) { // malloc check
                return NULL;
            }
            memcpy(newptr, oldptr, old_size - BSIZE);
            free(oldptr);
            return newptr;
        // }
      
    }

    
}

/*
 * calloc
 * This function is not tested by mdriver, and has been implemented for you.
 */
void* calloc(size_t nmemb, size_t size)
{
    void* ptr;
    size *= nmemb;
    ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}



/*
 * Returns whether the pointer is aligned.
 * May be useful for debugging.
 */
static bool aligned(const void* p)
{
    size_t ip = (size_t) p;
    return align(ip) == ip;
}

/*
 * mm_checkheap
 * You call the function via mm_checkheap(__LINE__)
 * The line number can be used to print the line number of the calling
 * function where there was an invalid heap.
 */
bool mm_checkheap(int line_number)
{
#ifdef DEBUG
    // Write code to check heap invariants here
    // IMPLEMENT THIS
    
    /*Checks on free-list*/
    if (false){
        for (int i = 0; i < NUM_CLASSES; i++) {
            for (node_t *curr = freelist_classes[i].head; in_heap(curr) && curr != NULL; curr = curr->next){

                /* Is every node's size in the range of the freelist class where it belongs to?*/
                if (get_class_index(get_blk_size((char *)curr)) != i){
                    printf("line %d: node's size is not in the range of the freelist class where it belongs to\n", line_number);
                    return false;
                }
                
                /* Is every block in the free list marked as free? */
                if (get_blk_alloc((char *)curr) != 0){
                    printf("line %d: block in the free list is not marked as free\n", line_number);
                    return false;
                }
                
                /* Is all nodes' prev/next pointers are consistent*/
                if (curr->next != NULL && curr->next->prev != curr){
                    printf("line %d: next pointer is not consistent\n", line_number);
                    return false;
                }
                
                /* Do all free list nodes point to valid heap addresses? */
                if (!in_heap(curr)){
                    printf("line %d: The node does not point to valid heap addresses\n", line_number);
                    return false;
                }

                /* Is every free block can be found in the right class of free list? */
                if (get_blk_alloc(curr) == 0){
                    int correct_class_index = get_class_index(get_blk_size(curr));
                    bool found = false;

                    for (node_t *curr_free = freelist_classes[correct_class_index].head; in_heap(curr_free) && curr_free != NULL; curr_free = curr_free->next){
                        if (curr_free == curr){
                            found = true;
                            break;
                        } 
                    }
                    if (!found){
                        printf("line %d: free block cannot be found in the right class of free list\n", line_number);
                        return false;
                    }
                }
            }
        }
    }

    /*Checks on blocks*/
    if (false){
        for (char *curr = heap_curr; in_heap(curr) && !is_epilogue(curr); curr = get_next_blk(curr)){

            /* Check FREE Block's Header/Footer size consistancy */
            if (get_blk_alloc(curr) != 1 && get_blk_size(curr) != get_blk_size(get_next_blk(curr) - BSIZE)){
                printf("line %d: Block's Header/Footer size is not consistant\n", line_number);
                return false;
            }

            /* Check FREE Block's Header/Footer alloc consistancy */
            if (get_blk_alloc(curr) != 1 && get_blk_alloc(curr) != get_blk_alloc(get_next_blk(curr) - BSIZE)){
                printf("line %d: Block's Header/Footer alloc is not consistant\n", line_number);
                return false;
            }
                   
            /* Check whether the current block's alloc status is consistant with the next blk's header's prev_alloc_bit*/
            if (is_epilogue(curr)!= true && get_blk_alloc(curr) != get_prev_alloc_bit(get_next_blk(curr))){
                printf("line %d: Block's alloc status is not consistant with the next blk's header's prev_alloc_bit\n", line_number);
                return false;
            }else{
                printf("!!!fine\n");
            }

            
            


            


        }
    }
    
    
    

    
#endif // DEBUG
    return true;
}