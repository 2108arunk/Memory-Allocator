#include <criterion/criterion.h>
#include <errno.h>
#include <signal.h>
#include "debug.h"
#include "sfmm.h"
#define TEST_TIMEOUT 15

/*
 * Assert the total number of free blocks of a specified size.
 * If size == 0, then assert the total number of all free blocks.
 */
void assert_free_block_count(size_t size, int index, int count) {
    int cnt = 0;
    for(int i = 0; i < NUM_FREE_LISTS; i++) {
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	while(bp != &sf_free_list_heads[i]) {
	    if(size == 0 || size == (bp->header & ~0x3f)) {
		cnt++;
		if(size != 0) {
		    cr_assert_eq(index, i, "Block %p (size %ld) is in wrong list for its size "
				 "(expected %d, was %d)",
				 (long *)(bp) + 1, bp->header & ~0x3f, index, i);
		}
	    }
	    bp = bp->body.links.next;
	}
    }
    if(size == 0) {
	cr_assert_eq(cnt, count, "Wrong number of free blocks (exp=%d, found=%d)",
		     count, cnt);
    } else {
	cr_assert_eq(cnt, count, "Wrong number of free blocks of size %ld (exp=%d, found=%d)",
		     size, count, cnt);
    }
}

Test(sfmm_basecode_suite, malloc_an_int, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz = sizeof(int);
	int *x = sf_malloc(sz);

	cr_assert_not_null(x, "x is NULL!");

	*x = 4;

	cr_assert(*x == 4, "sf_malloc failed to give proper space for an int!");

	assert_free_block_count(0, 0, 1);
	assert_free_block_count(8000, 8, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
	cr_assert(sf_mem_start() + PAGE_SZ == sf_mem_end(), "Allocated more than necessary!");
}

Test(sfmm_basecode_suite, malloc_four_pages, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;

	void *x = sf_malloc(32624);
	cr_assert_not_null(x, "x is NULL!");
	assert_free_block_count(0, 0, 0);
	cr_assert(sf_errno == 0, "sf_errno is not 0!");
}

Test(sfmm_basecode_suite, malloc_too_large, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	void *x = sf_malloc(524288);

	cr_assert_null(x, "x is not NULL!");
	assert_free_block_count(0, 0, 1);
	assert_free_block_count(130944, 8, 1);
	cr_assert(sf_errno == ENOMEM, "sf_errno is not ENOMEM!");
}

Test(sfmm_basecode_suite, free_no_coalesce, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_x = 8, sz_y = 200, sz_z = 1;
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);

	assert_free_block_count(0, 0, 2);
	assert_free_block_count(256, 3, 1);
	assert_free_block_count(7680, 8, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, free_coalesce, .timeout = TEST_TIMEOUT) {
	sf_errno = 0;
	size_t sz_w = 8, sz_x = 200, sz_y = 300, sz_z = 4;
	/* void *w = */ sf_malloc(sz_w);
	void *x = sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(y);
	sf_free(x);

	assert_free_block_count(0, 0, 2);
	assert_free_block_count(576, 5, 1);
	assert_free_block_count(7360, 8, 1);

	cr_assert(sf_errno == 0, "sf_errno is not zero!");
}

Test(sfmm_basecode_suite, freelist, .timeout = TEST_TIMEOUT) {
        size_t sz_u = 200, sz_v = 300, sz_w = 200, sz_x = 500, sz_y = 200, sz_z = 700;
	void *u = sf_malloc(sz_u);
	/* void *v = */ sf_malloc(sz_v);
	void *w = sf_malloc(sz_w);
	/* void *x = */ sf_malloc(sz_x);
	void *y = sf_malloc(sz_y);
	/* void *z = */ sf_malloc(sz_z);

	sf_free(u);
	sf_free(w);
	sf_free(y);

	assert_free_block_count(0, 0, 4);
	assert_free_block_count(256, 3, 3);
	assert_free_block_count(5696, 8, 1);

	// First block in list should be the most recently freed block.
	int i = 3;
	sf_block *bp = sf_free_list_heads[i].body.links.next;
	cr_assert_eq(bp, (char *)y - 16,
		     "Wrong first block in free list %d: (found=%p, exp=%p)",
                     i, bp, (char *)y - 16);
}

Test(sfmm_basecode_suite, realloc_larger_block, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(int), sz_y = 10, sz_x1 = sizeof(int) * 20;
	void *x = sf_malloc(sz_x);
	/* void *y = */ sf_malloc(sz_y);
	x = sf_realloc(x, sz_x1);

	cr_assert_not_null(x, "x is NULL!");
	sf_block *bp = (sf_block *)((char *)x - 16);
	cr_assert(*((long *)(bp) + 1) & 0x1, "Allocated bit is not set!");
	cr_assert((*((long *)(bp) + 1) & ~0x3f) == 128,
		  "Realloc'ed block size (%ld) not what was expected (%ld)!",
		  *((long *)(bp) + 1) & ~0x3f, 128);

	assert_free_block_count(0, 0, 2);
	assert_free_block_count(64, 0, 1);
	assert_free_block_count(7808, 8, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_splinter, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(int) * 20, sz_y = sizeof(int) * 16;
	void *x = sf_malloc(sz_x);
	void *y = sf_realloc(x, sz_y);

	cr_assert_not_null(y, "y is NULL!");
	cr_assert(x == y, "Payload addresses are different!");

	sf_block *bp = (sf_block *)((char*)y - 16);
	cr_assert(*((long *)(bp) + 1) & 0x1, "Allocated bit is not set!");
	cr_assert((*((long *)(bp) + 1) & ~0x3f) == 128,
		  "Block size (%ld) not what was expected (%ld)!",
	          *((long *)(bp) + 1) & ~0x3f, 128);

	assert_free_block_count(0, 0, 1);
	assert_free_block_count(7936, 8, 1);
}

Test(sfmm_basecode_suite, realloc_smaller_block_free_block, .timeout = TEST_TIMEOUT) {
        size_t sz_x = sizeof(double) * 8, sz_y = sizeof(int);
	void *x = sf_malloc(sz_x);
	void *y = sf_realloc(x, sz_y);

	cr_assert_not_null(y, "y is NULL!");

	sf_block *bp = (sf_block *)((char *)y - 16);
	cr_assert(*((long *)(bp) + 1) & 0x1, "Allocated bit is not set!");
	cr_assert((*((long *)(bp) + 1) & ~0x3f) == 64,
		  "Realloc'ed block size (%ld) not what was expected (%ld)!",
		  *((long *)(bp) + 1) & ~0x3f, 64);

	assert_free_block_count(0, 0, 1);
	assert_free_block_count(8000, 8, 1);
}

//############################################
//STUDENT UNIT TESTS SHOULD BE WRITTEN BELOW
//DO NOT DELETE THESE COMMENTS
//############################################

Test(sfmm_student_suite, heap_one_pg_full_test, .timeout = TEST_TIMEOUT) {

	// 8192 inital size padding + header + epilogue took 128 bytes
	// => only 63 blocks of 128 bytes are left .. we fill them all and see if no free blocks are left
	// Next we free them all and see, if the final is single block of size (8192-128) = 8064
	// Above process is done to have two sf_mem_grow() calls.

        size_t sz_x = sizeof(double)*10;

        void *ptr[63+64];

        for ( int i = 0; i < 63+64; i++)
		ptr[i] = sf_malloc(sz_x);

	assert_free_block_count(0, 1, 0);
	assert_free_block_count(0, 2, 0);
	assert_free_block_count(0, 3, 0);
	assert_free_block_count(0, 4, 0);
	assert_free_block_count(0, 5, 0);
	assert_free_block_count(0, 6, 0);
	assert_free_block_count(0, 7, 0);
	assert_free_block_count(0, 8, 0);
	assert_free_block_count(0, 0, 0);

        for ( int i = 0; i < 63+64; i++)
		sf_free(ptr[i]);

	assert_free_block_count(8064+8192, 8, 1);


}

Test(sfmm_student_suite, three_block_coalsce_on_free, .timeout = TEST_TIMEOUT) {

        size_t sz_x = sizeof(double)*10;
        size_t sz_y = sizeof(double)*20;
        size_t sz_z = sizeof(double)*30;
        size_t sz_a = sizeof(double);

        void *x = sf_malloc(sz_x);
        void *y = sf_malloc(sz_y);
        void *z = sf_malloc(sz_z);
        void *a = sf_malloc(sz_a);


        sf_block *bp_x = (sf_block *)((char *)x - 16);
	cr_assert(*((long *)(bp_x) + 1) & 0x1, "Allocated bit is not set!");

        sf_block *bp_y = (sf_block *)((char *)y - 16);
	cr_assert(*((long *)(bp_y) + 1) & 0x1, "Allocated bit is not set!");

        sf_block *bp_z = (sf_block *)((char *)z - 16);
	cr_assert(*((long *)(bp_z) + 1) & 0x1, "Allocated bit is not set!");

        sf_block *bp_a = (sf_block *)((char *)a - 16);
	cr_assert(*((long *)(bp_a) + 1) & 0x1, "Allocated bit is not set!");

        sf_free(y);
        sf_free(z);
        sf_free(x);

	assert_free_block_count(576, 5, 1);
	assert_free_block_count(7424, 8, 1);

        sf_free(a);

}

Test(sfmm_student_suite, relloc_size_zero_block_test, .timeout = TEST_TIMEOUT) {

	// if realloc call to allocate size zero,
	size_t sz_x = sizeof(double)*10;
	void *x = sf_malloc( sz_x );

        sf_block *bp_x = (sf_block *)((char *)x - 16);
	cr_assert(*((long *)(bp_x) + 1) & 0x1, "Allocated bit is not set!");
	sf_realloc(x, 0);

	assert_free_block_count(8064, 8, 1);

}

Test(sfmm_student_suite, colasce_blocks_accross_heap_test, .timeout = TEST_TIMEOUT) {

	// if realloc call to allocate size zero,
	size_t sz_x = sizeof(double)*600, sz_y = sizeof(double)*400 ;

	void *x = sf_malloc( sz_x );
	void *y = sf_malloc( sz_y );

        sf_block *bp_x = (sf_block *)((char *)x - 16);
	cr_assert(*((long *)(bp_x) + 1) & 0x1, "Allocated bit is not set!");

        sf_block *bp_y = (sf_block *)((char *)y - 16);
	cr_assert(*((long *)(bp_y) + 1) & 0x1, "Allocated bit is not set!");

	//sf_realloc(x, 0);
	sf_free (y);

	assert_free_block_count(11392, 8, 1);

}

Test(sfmm_student_suite, realloc_data_copy_into_bigblock, .timeout = TEST_TIMEOUT) {


    long int *ptr1 = sf_malloc( sizeof(long int) );
    *ptr1 = 12345678;
    long int test = *ptr1;

     sf_block *bp_x = (sf_block *)((char *)ptr1 - 16);
     cr_assert(*((long *)(bp_x) + 1) & 0x1, "Allocated bit is not set!");


    long int *ptr2 = sf_realloc(ptr1, sizeof(long int)*2 );

    //printf("*ptr1 = %ld \n", *ptr1);
    //printf("*ptr2 = %ld \n", *ptr2);

    cr_assert((*ptr2 == test), "data correctly not copy");

}

Test(sfmm_student_suite, invalid_pointer_sigabrt_test,  .signal = SIGABRT, .timeout = TEST_TIMEOUT) {


     void *ptr1 = sf_malloc( sizeof(long int)*10 );
     sf_block *bp_x = (sf_block *)((char *)ptr1 - 16);
     cr_assert(*((long *)(bp_x) + 1) & 0x1, "Allocated bit is not set!");

     sf_free(++ptr1); // invalid pointetr

}

Test(sfmm_student_suite, pointer_after_alloc_heap_test,  .signal = SIGABRT, .timeout = TEST_TIMEOUT) {

     void *ptr1 = sf_malloc( sizeof(long int)*10 );
     sf_block *bp_x = (sf_block *)((char *)ptr1 - 16);
     cr_assert(*((long *)(bp_x) + 1) & 0x1, "Allocated bit is not set!");

     ptr1 = ptr1 + 100000;
     sf_free(ptr1);

}

Test(sfmm_student_suite, pointer_before_alloc_heap_test,  .signal = SIGABRT, .timeout = TEST_TIMEOUT) {

     void *ptr1 = sf_malloc( sizeof(long int)*10 );
     sf_block *bp_x = (sf_block *)((char *)ptr1 - 16);
     cr_assert(*((long *)(bp_x) + 1) & 0x1, "Allocated bit is not set!");

     ptr1 = ptr1 - 200;
     sf_free(ptr1);

}