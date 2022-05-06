/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "debug.h"
#include "sfmm.h"


#define headsize  8
#define align_lim 64
#define byte 8
#define hexnum 0xFFFF

extern void *ptr_epiloge;
extern void *ptr_prologue;

int blocksize_fib(size_t req_size);
int blocksize_calculation(size_t size);

int fiblist_index(size_t size);
void *traverse_list(size_t size);

void insert_block_fiblist(void* , size_t index);
void remove_block_fiblist(void*);

void *split_largeblock(void*, size_t);
void *split_largeblock_realloc_old(void*, size_t blocksize);
void *split_largeblock_realloc(void*, size_t blocksize);

void create_free_block(void * , size_t size);
void initilize_fiblist_list();
void intial_sf_malloc_call();

void prologue_init(void* ptr);
void epilogue_init(void* ptr);

void epilogue_check_allocated(sf_block* ptr); // to check if block previous to epilogue is allocated or not and modify epilogue accordingly

void next_block_pal_update(sf_block* ptr);
void next_block_pal_update_free(sf_block* ptr);
void* coalesce_free_blocks(void* ptr);

size_t sizeof_block(sf_block*ptr); // accepts the prev_footer of the next block as input

void *sf_malloc(size_t size) {

    if (size == 0) {
        return NULL; // if requested size is zero, spurious request return null
    }

    if (sf_mem_start() == sf_mem_end()) { // start of program .. heap is empty
        initilize_fiblist_list();
        intial_sf_malloc_call();
    }

    size_t blocksize = blocksize_calculation(size);
    //printf("\n No more memory available ---, size =  %ld  blocksize =  %ld sentinel list [index] = %d \n", size, blocksize ,fiblist_index(blocksize));

    void* tr_ptr = traverse_list(blocksize);
    //printf("\n No more memory available ---, size =  %ld  blocksize =  %ld  size(tr_ptr) = %ld \n", size, blocksize, sizeof_block(tr_ptr) );


    if (blocksize == sizeof_block(tr_ptr)) {

        remove_block_fiblist(tr_ptr);
        sf_block *ptr = tr_ptr;

        size_t t_mask = 1;
        t_mask = (t_mask << 6)-1;
        ptr -> header = ((ptr -> header) & (t_mask))|blocksize|THIS_BLOCK_ALLOCATED;
        epilogue_check_allocated(ptr);
        next_block_pal_update(ptr);
        return (ptr -> body.payload);

    } else if (blocksize < sizeof_block(tr_ptr)) {

        sf_block *ptr;
        ptr = split_largeblock(tr_ptr, blocksize);
        size_t t_mask = 1;
        t_mask = (t_mask << 6)-1;
        ptr -> header = ((ptr -> header) & (t_mask))|blocksize|THIS_BLOCK_ALLOCATED;
        epilogue_check_allocated(ptr);
        next_block_pal_update(ptr);
        return (ptr -> body.payload);
    }

    // if above condition didn't get hit .. we need to request more memory///

    do {



        sf_header *epilogue_header = (sf_header*)(sf_mem_end() - sizeof(sf_header));
        //printf("--- No more memory available ---%p \n", epilogue_header);

        void * ptr;
        if (( ptr = sf_mem_grow()) == NULL){
            sf_errno = ENOMEM;
            return NULL;

        };

        sf_block* new_block_header = (sf_block*)((void*)epilogue_header - sizeof(sf_header));
        new_block_header ->header =  (new_block_header -> header)| PAGE_SZ;
        sf_free( (void*)(new_block_header)+ 2 *sizeof(sf_header) ) ;
        epilogue_init(sf_mem_end()  );

        return sf_malloc(size);


    } while (tr_ptr != NULL);

    sf_show_heap();

    return NULL;

}

void next_block_pal_update(sf_block* ptr){

    if (  ((void*)ptr + sizeof(sf_header) + sizeof_block(ptr) ) != ( sf_mem_end()-sizeof(sf_header) ) ){

        sf_block* next_block = (sf_block *)((void*)ptr + sizeof_block(ptr));

        if (next_block -> header & THIS_BLOCK_ALLOCATED){
            next_block -> header = next_block -> header|PREV_BLOCK_ALLOCATED;
        }

        else{
            next_block -> header = next_block -> header|PREV_BLOCK_ALLOCATED;
            sf_header* ptr_footer = (sf_header*) ( (void*)next_block + sizeof_block(next_block));
            *ptr_footer = next_block -> header ;
        }

    }

}

void epilogue_check_allocated(sf_block* ptr){

    if (  ((void*)ptr + sizeof(sf_header) + sizeof_block(ptr) ) == ( sf_mem_end()-sizeof(sf_header) ) ){

        sf_header* ptr_footer = (sf_header*) (sf_mem_end() - sizeof(sf_header));
        *ptr_footer = *ptr_footer | PREV_BLOCK_ALLOCATED;

    }

}

void intial_sf_malloc_call(){

        sf_mem_grow(); // intialize head for the first time
        prologue_init(sf_mem_start());
        epilogue_init(sf_mem_end()  );

        size_t remaing_blocksize = PAGE_SZ - ( 64 + 8 + 56);

        void* p_leftblock = (sf_mem_start() + 120);

        int index = fiblist_index(remaing_blocksize);// insert into the freelist
        create_free_block(p_leftblock, remaing_blocksize);
        insert_block_fiblist(p_leftblock, index); // put remaining block into sentinal list

        return;



}

// helper function to initialize prologue -- TBD -- start of the heap passed
void prologue_init(void* ptr){

        //printf("ptr heap end -------%p\n", ptr);
        ptr = ptr+ (byte*7) - sizeof(sf_header);

        sf_block *prologue_block = ptr; // cast block on pointer
        prologue_block -> header = (prologue_block -> header)| THIS_BLOCK_ALLOCATED;

        int reqsize = 64;
        (prologue_block ->header) = (reqsize)|prologue_block -> header;

        next_block_pal_update(prologue_block);

        return;

}

// helper function to initialize epiloge -- TBD
void epilogue_init(void* ptr){


        ptr = ptr - sizeof(sf_header);
        sf_header *p_epilogue = (sf_header*) ptr; // cast void pointer to sf_header type
        *p_epilogue = 0;
        *p_epilogue = (*p_epilogue)|(THIS_BLOCK_ALLOCATED); // prev block allotment information not available
        //printf("\n p_epilogue -> header = %ld p_epilogue = %p\n ", *p_epilogue, p_epilogue);
}

// this will take the large free block and
// the size of the required block -- add function
void *split_largeblock(void* ptr_largeblock, size_t blocksize){

    size_t largeblock_size = sizeof_block(ptr_largeblock);
    size_t remaing_blocksize = largeblock_size - blocksize;

    remove_block_fiblist(ptr_largeblock);

    create_free_block(ptr_largeblock+8+blocksize, remaing_blocksize);

    /// check coalasce condition ///
    void * ptr_coalesce;
    ptr_coalesce = coalesce_free_blocks(ptr_largeblock+blocksize);
    size_t coal_blocksize = sizeof_block(ptr_coalesce);
    //printf("\n--blocksize = %ld", coal_blocksize);
    insert_block_fiblist(ptr_coalesce + 8, fiblist_index(coal_blocksize));
    next_block_pal_update_free(ptr_coalesce);

    sf_block* alloc_block = (sf_block*)( ptr_largeblock );

    return alloc_block;

}

void remove_block_fiblist(void* ptr){

    size_t size = sizeof_block(ptr);
    int index = fiblist_index(size)-1;

    while( index < NUM_FREE_LISTS ){

        sf_block *terminate = &sf_free_list_heads[index];
        sf_block *block_search = &sf_free_list_heads[index];

        do {
                sf_block *prev = block_search;
                block_search = block_search->body.links.next;

                if ( block_search == ptr ){

                    block_search = block_search->body.links.next;

                    prev->body.links.next = block_search;
                    block_search->body.links.prev = prev;

                    return;
                }

        } while ( block_search != terminate );

       index++;

    }

}

// helper function to calculate the final blocksize to be alloted which satisfy the alignment constraints etc
// for eg.. 8 bytes request we need size of 64, for 32 bytes we need 128 etc.
int blocksize_calculation(size_t size){

    size_t requested_size = size;
    size_t blocksize = 0;
    size_t size_header = 8;

    blocksize = blocksize + requested_size +size_header;

    if (blocksize < 64) // get 64 as the blocksize, is total size is <64
        blocksize = 64;
    else if (blocksize%64 != 0) // get it to 1+ multiple of 64
        blocksize = ( (blocksize/64) + 1) * 64;

    return blocksize;

}





void sf_free(void *pp) {



//    printf("xxx\n");

    // NUll ptr or pointer not 64 byte aligned
    if ( pp == NULL || ( (size_t)(pp) % 64 != 0 ) ){
//        printf("xxx\n");
        abort();

    }

    void * ptr = pp - 2 * sizeof(sf_header);// points to pointer block - additional subtraction for footer+heater
                                            // because currently it points to payload
    sf_block * p_freeit = (sf_block *) ptr;
    size_t blocksize = sizeof_block(p_freeit);

    // header at lower memory location than header of free block
    if (  (void*)( ptr + sizeof(sf_header) ) < ( sf_mem_start()+ 7 *byte + 8 *byte ) ){
        abort();

    }

    // footer at lower memory location than header of free block
    if (  (void*)( ptr + sizeof(sf_header) + blocksize ) > ( (void*)(sf_mem_end()) - 8) ) {
        abort();

    }



    //printf("block_header = %ld \n", p_freeit ->header);

    create_free_block(pp -  sizeof(sf_header), blocksize);
    next_block_pal_update_free(ptr);

    //printf("after CFB -- block_header = %ld \n", p_freeit ->header);

    void * ptr_coalesce;
    ptr_coalesce = coalesce_free_blocks(ptr);
    blocksize = sizeof_block(ptr_coalesce);
    create_free_block(ptr_coalesce +  sizeof(sf_header), blocksize);
    insert_block_fiblist(ptr_coalesce +  sizeof(sf_header), fiblist_index(blocksize));

    return;
}



void* coalesce_free_blocks(void* ptr){

    sf_block* block_ptr = (sf_block *) ptr;
    size_t prev_alloc = ((block_ptr -> header) & PREV_BLOCK_ALLOCATED);

    sf_block *next_block_ptr = (sf_block *)((void*)ptr + sizeof_block(block_ptr));
    size_t next_alloc = (next_block_ptr->header & THIS_BLOCK_ALLOCATED);

    //printf( "\n block_ptr->header = %ld prev_alloc = %ld next_alloc = %ld \n", block_ptr -> header, prev_alloc, next_alloc);


    //case 1: if both previous and next blocks are allocated -- do nothing
    if (prev_alloc && next_alloc) return ptr;


    //case 2: if only previous block is allocated, and next block is free.
    if (prev_alloc && !next_alloc) {

        size_t nextblock_size = sizeof_block(next_block_ptr);
        size_t currblock_size = sizeof_block(block_ptr);
        size_t coalesce_block_size = nextblock_size + currblock_size;

        size_t t_mask = 1;
        t_mask = (t_mask << 6)-1;
        block_ptr -> header = ((block_ptr -> header) & (t_mask))|coalesce_block_size;

        sf_header *p_footer = (ptr + coalesce_block_size - 1*8) ;// sizeof(sf_header);
        *p_footer = 0;
        *p_footer = block_ptr -> header;


        remove_block_fiblist(next_block_ptr);

        return (void*)(block_ptr);

    }


    //case 3: if only next block is allocated, but previous block is free

    if (!prev_alloc && next_alloc) {

        sf_header* prev_block_footer = (sf_header*)(block_ptr);// this points to prev block footer
        size_t prev_block_size = ((*prev_block_footer) & (size_t)(hexnum<<6));
        sf_block* prev_block = (sf_block*)((void*)prev_block_footer - prev_block_size);

        remove_block_fiblist(prev_block);
        size_t currblock_size = sizeof_block(block_ptr);
        size_t coalesce_block_size = prev_block_size + currblock_size;

        size_t t_mask = 1;
        t_mask = (t_mask << 6)-1;

        prev_block -> header = ((prev_block -> header) & (t_mask))|coalesce_block_size;

        sf_header *p_footer = ((void*)(prev_block) + coalesce_block_size) ;// sizeof(sf_header);
        *p_footer = 0;
        *p_footer = prev_block -> header;


        return (void*)(prev_block);

    }

    //case 4: if both blocks are free

    if (!prev_alloc && !next_alloc) {

        /*** prev block calculation *****/
        sf_header* prev_block_footer = (sf_header*)(block_ptr);// this points to prev block footer
        size_t prev_block_size = ((*prev_block_footer) & (size_t)(hexnum<<6));
        sf_block* prev_block = (sf_block*)((void*)prev_block_footer - prev_block_size);
        remove_block_fiblist(prev_block);

        /*** next block calculation *****/
        size_t nextblock_size = sizeof_block(next_block_ptr);
        remove_block_fiblist(next_block_ptr);

        size_t currblock_size = sizeof_block(block_ptr);
        size_t coalesce_block_size = prev_block_size + currblock_size +nextblock_size;

        size_t t_mask = 1;
        t_mask = (t_mask << 6)-1;

        prev_block -> header = ((prev_block -> header) & (t_mask))|coalesce_block_size;
        return (void*)(prev_block);


    }


    return ptr;


}


void *sf_realloc(void *pp, size_t rsize) {

    // NUll ptr or pointer not 64 byte aligned

    if ( pp == NULL || ( (size_t)(pp) % 64 != 0 ) ){
        sf_errno = EINVAL;
       abort();

    }

    void * ptr = pp - 2 * sizeof(sf_header);// points to pointer block - additional subtraction for footer+heater
                                            // because currently it points to payload
    sf_block * init_block = (sf_block *) ptr;
    size_t blocksize = sizeof_block(init_block);

    if (  (void*)( ptr + sizeof(sf_header) ) < (void*)( (void*)(sf_mem_start())+ 7 *byte + 8 *byte ) ){
       sf_errno = EINVAL;
       abort();
    }

    // footer at lower memory location than header of free block
    if (  (void*)( ptr + sizeof(sf_header) + blocksize ) > ( (void*)(sf_mem_end()) - 8) ) {
        sf_errno = EINVAL;
        abort();
    }

    size_t realloc_block_size = blocksize_calculation(rsize);

    //printf("realloc_block_size, %ld \n", realloc_block_size);
    // if reqsize is zero ... free the block by calling sf_free

    if (rsize == 0){
        sf_free(pp);
        return NULL;
    }

    if ( realloc_block_size == blocksize){ // do nothing, return the original block
        return pp;
    }

    if ( (int)(blocksize - realloc_block_size) < 64 && ((int)(blocksize - realloc_block_size) > 0)) {
    // Splincter created .. return the original block
        return pp;;
    }


    if ( (int)(blocksize - realloc_block_size) >= 64) {
        sf_block* ptr_up = (sf_block*) split_largeblock(ptr, realloc_block_size);
        size_t t_mask = 1;
        t_mask = (t_mask << 6)-1;
        ptr_up -> header = ((ptr_up -> header) & (t_mask))|realloc_block_size|THIS_BLOCK_ALLOCATED;
        next_block_pal_update(ptr_up);

        return (ptr_up -> body.payload);
    }



    // Requested block has a bigger size compare to original block


   if ( (int)(realloc_block_size - blocksize) > 0) {
        void * ptr_malloc = sf_malloc(rsize);

        // Incase no memeory is available, set the sf_errno to ENOMEM,
        // and a NULL pointer is returned

        if (ptr_malloc == NULL) {
            sf_errno = ENOMEM;
            return NULL;
        }

        size_t n = blocksize - sizeof(sf_header);
        memcpy((void *)(ptr_malloc), (void *)(pp), n);
        sf_free(pp);// correct it
        return (ptr_malloc);
   }

    return pp;
}

// this will take the large free block, and split it
// But it does opposite of the split used earlier in malloc... it add the lower part to the free list
// and return the modified header......


void *split_largeblock_realloc(void* upper_block, size_t realloc_block_size){

    size_t upper_block_size = sizeof_block(upper_block);
    size_t upper_block_size_left = upper_block_size - realloc_block_size;

    //printf("upper_block_size = %ld upper_block_size_left = %ld \n", upper_block_size,  upper_block_size_left);
    //remove_block_fiblist(ptr_largeblock);
    //sf_show_heap();

    void* lower_block = (void*)( upper_block + upper_block_size_left);

    create_free_block(upper_block + 8, upper_block_size_left);
    next_block_pal_update_free(upper_block);
    void * ptr_coalesce;
    ptr_coalesce = coalesce_free_blocks(upper_block);
    size_t blocksize = sizeof_block(ptr_coalesce);
    printf("blocksize = %ld", blocksize);
    insert_block_fiblist(ptr_coalesce + 8, fiblist_index(blocksize));

    //modify header of original block
    size_t t_mask = 1;
    t_mask = (t_mask << 6)-1;
    sf_block * lower_block_mod = (sf_block *)lower_block;

    lower_block_mod -> header = (((lower_block_mod -> header) & (t_mask))|realloc_block_size)|THIS_BLOCK_ALLOCATED;

    return lower_block_mod;

}
void *split_largeblock_realloc_old(void* upper_block, size_t realloc_block_size){

    size_t upper_block_size = sizeof_block(upper_block);
    size_t lower_block_size = upper_block_size - realloc_block_size;

    //remove_block_fiblist(ptr_largeblock);
    //sf_show_heap();

    void* lower_block = (void*)( upper_block + realloc_block_size);

    create_free_block(lower_block + 8, lower_block_size);
    insert_block_fiblist(lower_block + 8, fiblist_index(lower_block_size));

    //modify header of original block
    size_t t_mask = 1;
    t_mask = (t_mask << 6)-1;
    sf_block * upper_block_mod = (sf_block *)upper_block;

    upper_block_mod -> header = ((upper_block_mod -> header) & (t_mask))|realloc_block_size;

    return upper_block_mod;

}

void next_block_pal_update_free(sf_block* ptr){

        sf_header* np_header = (sf_header*) ((void*)ptr + sizeof(sf_header) + sizeof_block(ptr));

        size_t mask = (hexnum << 2)| THIS_BLOCK_ALLOCATED;
        // update header and footer too of the free-block

        if (*np_header & THIS_BLOCK_ALLOCATED){
            // Only header will change for allocated block
            *np_header = *np_header & mask;
        }
        else {
            // Both header and footer will change for de-allocated block
            *np_header = *np_header & mask;
            size_t size = ((*np_header) & (hexnum<<6));
            sf_header* np_footer = (sf_header *)( (void*)(np_header) + size - sizeof(sf_header));
            *np_footer = *np_header;
        }

}




//function to return the index of the freelist where the block to be added
//Based on fibinacci sequence ... also is useful to search for the block in the list by [index]

int fiblist_index(size_t req_size){

    int f = 1, s = 2;
    int temp = 0;
    temp = temp *1;

    if (req_size <= f*align_lim) return 1;
    if (req_size <= s*align_lim) return 2;
    if (req_size <= (s+f) * align_lim) return 3;

    int m = 3;

    while ( req_size > ( f + s ) * align_lim){

        temp = f;
        f = s;
        s = temp+s;
        m++;

    }

    if (req_size > 34 * align_lim)
        return (NUM_FREE_LISTS);

    return m;
}

// helper functtion to create a free block ---
// pass the pointer to the header location

void create_free_block(void* ptr , size_t size){

    sf_block *p_init = (sf_block *)(ptr - sizeof(sf_header));

    p_init -> header = (p_init -> header) & (hexnum<<1);
    p_init -> header = (p_init -> header)|(size);

    p_init -> body.links.next = NULL;
    p_init -> body.links.prev = NULL;

    //next_block_pal_update_free(p_init);

    sf_header *p_footer = (ptr + size - 1*8) ;// sizeof(sf_header);
    *p_footer = 0;
    *p_footer = p_init -> header;

}

// function to inialize the  fibonacci free list sentinel structure
void initilize_fiblist_list(){

        for(size_t i = 0; i< NUM_FREE_LISTS; i++){

                sf_block *temp = &sf_free_list_heads[i];
                sf_free_list_heads[i].body.links.next = temp;
                sf_free_list_heads[i].body.links.prev = temp;
        }

}

void* traverse_list(size_t size) {

    int min_index = fiblist_index(size)-1;

    for (int i = min_index; i < NUM_FREE_LISTS; i++){

//        printf("min_index = %d \n", min_index);

        sf_block *terminate = &sf_free_list_heads[i];
        sf_block *block_search = &sf_free_list_heads[i];

        while ( block_search->body.links.next != terminate  ){

            if (   size <=  sizeof_block( block_search->body.links.next) ) {

                //printf("size_of_block %ld", sizeof_block(block_search->body.links.next) );
                void * ptr = (void*)(block_search->body.links.next);
                // next will point to the prev_footer of the next block
                return ptr;
            }

            block_search = block_search->body.links.next;

        }



    }

//    printf("min_index = %d \n", min_index);

    return NULL;
}

// This function accepts sf_block pointer
//(which points at prev_footer initially in the structure)
size_t sizeof_block( sf_block* ptr){

        if (ptr == NULL) return 0;

        size_t size = ((ptr->header) & (hexnum<<6)); // right shift by 6 to calculate the size
        return size;
}

// Insert every new block in the beginning to make the LIFO implementation
// Search will also begin from the beginning of sentinel nodes

void insert_block_fiblist(void* ptr , size_t index){

        index = index -1;

        sf_block * pblock  = (sf_block*)(ptr - sizeof(sf_header));
        sf_block*temp = sf_free_list_heads[index].body.links.next;

        sf_free_list_heads[index].body.links.next = pblock;
        pblock ->body.links.prev = &sf_free_list_heads[index];

        pblock ->body.links.next = temp;
        temp ->body.links.prev = pblock;

}

