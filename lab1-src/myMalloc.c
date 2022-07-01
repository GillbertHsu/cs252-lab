#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "myMalloc.h"
#include "printing.h"

/* Due to the way assert() prints error messges we use out own assert function
 * for deteminism when testing assertions
 */
#ifdef TEST_ASSERT
  inline static void assert(int e) {
    if (!e) {
      const char * msg = "Assertion Failed!\n";
      write(2, msg, strlen(msg));
      exit(1);
    }
  }
#else
  #include <assert.h>
#endif

/*
 * Mutex to ensure thread safety for the freelist
 */
static pthread_mutex_t mutex;

/*
 * Array of sentinel nodes for the freelists
 */
header freelistSentinels[N_LISTS];

/*
 * Pointer to the second fencepost in the most recently allocated chunk from
 * the OS. Used for coalescing chunks
 */
header * lastFencePost;

/*
 * Pointer to maintian the base of the heap to allow printing based on the
 * distance from the base of the heap
 */ 
void * base;

/*
 * List of chunks allocated by  the OS for printing boundary tags
 */
header * osChunkList [MAX_OS_CHUNKS];
size_t numOsChunks = 0;

/*
 * direct the compiler to run the init function before running main
 * this allows initialization of required globals
 */
static void init (void) __attribute__ ((constructor));

// Helper functions for manipulating pointers to headers
static inline header * get_header_from_offset(void * ptr, ptrdiff_t off);
static inline header * get_left_header(header * h);
static inline header * ptr_to_header(void * p);

// Helper functions for allocating more memory from the OS
static inline void initialize_fencepost(header * fp, size_t object_left_size);
static inline void insert_os_chunk(header * hdr);
static inline void insert_fenceposts(void * raw_mem, size_t size);
static header * allocate_chunk(size_t size);

// Helper functions for freeing a block
static inline void deallocate_object(void * p);

// Helper functions for allocating a block
static inline header * allocate_object(size_t raw_size);

// Helper functions for verifying that the data structures are structurally 
// valid
static inline header * detect_cycles();
static inline header * verify_pointers();
static inline bool verify_freelist();
static inline header * verify_chunk(header * chunk);
static inline bool verify_tags();

static void init();

static bool isMallocInitialized;

/**
 * @brief Helper function to retrieve a header pointer from a pointer and an 
 *        offset
 *
 * @param ptr base pointer
 * @param off number of bytes from base pointer where header is located
 *
 * @return a pointer to a header offset bytes from pointer
 */
static inline header * get_header_from_offset(void * ptr, ptrdiff_t off) {
	return (header *)((char *) ptr + off);
}

/**
 * @brief Helper function to get the header to the right of a given header
 *
 * @param h original header
 *
 * @return header to the right of h
 */
header * get_right_header(header * h) {
	return get_header_from_offset(h, get_object_size(h));
}

/**
 * @brief Helper function to get the header to the left of a given header
 *
 * @param h original header
 *
 * @return header to the right of h
 */
inline static header * get_left_header(header * h) {
  return get_header_from_offset(h, -h->object_left_size);
}

/**
 * @brief Fenceposts are marked as always allocated and may need to have
 * a left object size to ensure coalescing happens properly
 *
 * @param fp a pointer to the header being used as a fencepost
 * @param object_left_size the size of the object to the left of the fencepost
 */
inline static void initialize_fencepost(header * fp, size_t object_left_size) {
	set_object_state(fp,FENCEPOST);
	set_object_size(fp, ALLOC_HEADER_SIZE);
	fp->object_left_size = object_left_size;
}

/**
 * @brief Helper function to maintain list of chunks from the OS for debugging
 *
 * @param hdr the first fencepost in the chunk allocated by the OS
 */
inline static void insert_os_chunk(header * hdr) {
  if (numOsChunks < MAX_OS_CHUNKS) {
    osChunkList[numOsChunks++] = hdr;
  }
}

/**
 * @brief given a chunk of memory insert fenceposts at the left and 
 * right boundaries of the block to prevent coalescing outside of the
 * block
 *
 * @param raw_mem a void pointer to the memory chunk to initialize
 * @param size the size of the allocated chunk
 */
inline static void insert_fenceposts(void * raw_mem, size_t size) {
  // Convert to char * before performing operations
  char * mem = (char *) raw_mem;

  // Insert a fencepost at the left edge of the block
  header * leftFencePost = (header *) mem;
  initialize_fencepost(leftFencePost, ALLOC_HEADER_SIZE);

  // Insert a fencepost at the right edge of the block
  header * rightFencePost = get_header_from_offset(mem, size - ALLOC_HEADER_SIZE);
  initialize_fencepost(rightFencePost, size - 2 * ALLOC_HEADER_SIZE);
}

/**
 * @brief Allocate another chunk from the OS and prepare to insert it
 * into the free list
 *
 * @param size The size to allocate from the OS
 *
 * @return A pointer to the allocable block in the chunk (just after the 
 * first fencpost)
 */
static header * allocate_chunk(size_t size) {
  void * mem = sbrk(size);
  
  insert_fenceposts(mem, size);
  header * hdr = (header *) ((char *)mem + ALLOC_HEADER_SIZE);
  set_object_state(hdr, UNALLOCATED);
  set_object_size(hdr, size - 2 * ALLOC_HEADER_SIZE);
  hdr->object_left_size = ALLOC_HEADER_SIZE;
  return hdr;
}

 /**
  * My own helper function
  */

 static inline void move_to_appropriate_freelist(header * new_block) {
  int index = (get_object_size(new_block) - ALLOC_HEADER_SIZE)/8 -1;
  index = index > N_LISTS - 1 ? N_LISTS - 1 : index;

  header * rightplace = &freelistSentinels[index];

  new_block->next = rightplace->next; //insert to beginning
  rightplace->next = new_block;
  new_block->next->prev = new_block;
  new_block->prev = rightplace;
}

static inline header * split_block(header * left, size_t block_size) {
  int original_index = (get_object_size(left) - ALLOC_HEADER_SIZE)/8 - 1;  //used to check if I need to move block after split
  original_index = original_index > N_LISTS - 1  ? N_LISTS - 1 : original_index;

  set_object_size(left, get_object_size(left) - block_size); //remain size

  header * new_header = get_header_from_offset(left ,get_object_size(left)); //the allocated block
  set_object_size(new_header, block_size);
  

  new_header->object_left_size = get_object_size(left); //update left size

  header * right =  get_header_from_offset(new_header, get_object_size(new_header));
  right->object_left_size = get_object_size(new_header); //update right block's left size

  int new_index = (get_object_size(left) - ALLOC_HEADER_SIZE) /8 - 1;
  new_index = new_index > N_LISTS - 1 ? N_LISTS - 1 : new_index;

  if (new_index != original_index ) {
    left->prev->next = left->next;
    left->next->prev = left->prev;
    move_to_appropriate_freelist(left);
  }
  return new_header;
}

static inline void requre_more_chunks() {
  header * new_chunks = allocate_chunk(ARENA_SIZE);
  header * prevFencePost = get_left_header(new_chunks);
  header * left_left_fencepost = get_left_header(prevFencePost);
  lastFencePost = get_header_from_offset(new_chunks,get_object_size(new_chunks));
  

  if (get_object_state(left_left_fencepost) == FENCEPOST) {
    header * left_left = get_left_header(left_left_fencepost);
    int new_size;
    if (get_object_state(left_left) == UNALLOCATED) {
      new_size = get_object_size(new_chunks) + 2*ALLOC_HEADER_SIZE + get_object_size(left_left);
      
      int index_left = (get_object_size(left_left) - ALLOC_HEADER_SIZE)/8 - 1;
      index_left = index_left > N_LISTS - 1 ? N_LISTS - 1 : index_left;
      
      if (index_left < N_LISTS - 1) { 
        header * freelist_left = &freelistSentinels[index_left];
        header * current = freelist_left->next;
        while (current != freelist_left) {
          if (current == left_left) {
            current->prev->next = current->next;
            current->next->prev = current->prev; 
          }
        current = current->next;
        } //delete left block from list
      }
      set_object_size(left_left, new_size);
      lastFencePost->object_left_size = get_object_size(left_left);
      move_to_appropriate_freelist(left_left);

    } else if (get_object_state(left_left) == ALLOCATED) {
      new_size = get_object_size(new_chunks) + 2*ALLOC_HEADER_SIZE;
      new_chunks = get_header_from_offset(new_chunks, -2*ALLOC_HEADER_SIZE);
      set_object_size(new_chunks, new_size);
      lastFencePost->object_left_size = get_object_size(new_chunks);
      set_object_state(new_chunks, UNALLOCATED);
      move_to_appropriate_freelist(new_chunks);
    }
  } else {
    insert_os_chunk(prevFencePost);
    move_to_appropriate_freelist(new_chunks);
  }
}

/**
 * @brief Helper allocate an object given a raw request size from the user
 *
 * @param raw_size number of bytes the user needs
 *
 * @return A block satisfying the user's request
 */
static inline header * allocate_object(size_t raw_size) {
  // TODO implement allocation
  if (raw_size == 0){
    return NULL;
  }

  int round_size = raw_size;
  if (raw_size%8 != 0) {
    round_size = 8*(raw_size/8 +1);
  }

  /*
   * size of block and data  
   */
  int final_block_size = 32 > (round_size + ALLOC_HEADER_SIZE) ? 32 : (round_size + ALLOC_HEADER_SIZE);
  int final_data_size = round_size;
  int freelist_index = final_data_size/8 - 1;
  freelist_index = freelist_index > N_LISTS - 1 ? N_LISTS - 1 : freelist_index;
  

  int state = 1;
  header * headr = NULL;

  for (int i = freelist_index ; i < N_LISTS && state; i++) {
    header * freelist = &freelistSentinels[i];
    header * current =  freelist->next;
    while (current != freelist) {
      if( (get_object_size(current) == final_block_size)){
        current->prev->next = current->next;
        current->next->prev = current->prev;
        headr = current;
        state = 0;
        break;
      } else if ( (get_object_size(current) > final_block_size) && ((get_object_size(current) - final_block_size) < 32)) {
        current->prev->next = current->next;
        current->next->prev = current->prev;
        headr = current;
        state = 0;
        break;
      } else if ( (get_object_size(current) > final_block_size) && ((get_object_size(current) - final_block_size) >= 32)) {
        headr = split_block(current, final_block_size);
        state = 0;
        break;
      }
      current = current->next;
    }
  }

  //if there is no enough space
  while (headr == NULL) {
    requre_more_chunks();
    state = 1;
    for (int i = freelist_index ; i < N_LISTS && state; i++) {
    header * freelist = &freelistSentinels[i];
    header * current =  freelist->next;
    while (current != freelist) {
      if( (get_object_size(current) == final_block_size)){
        current->prev->next = current->next;
        current->next->prev = current->prev;
        headr = current;
        state = 0;
        break;
      } else if ( (get_object_size(current) > final_block_size) && ((get_object_size(current) - final_block_size) < 32)) {
        current->prev->next = current->next;
        current->next->prev = current->prev;
        headr = current;
        state = 0;
        break;
      } else if ( (get_object_size(current) > final_block_size) && ((get_object_size(current) - final_block_size) >= 32)) {
        headr = split_block(current, final_block_size);
        state = 0;
        break;
      }
      current = current->next;
    }
   }
  }
  
  set_object_state(headr, ALLOCATED);
  return (header *) headr->data;
}



/**
 * @brief Helper to get the header from a pointer allocated with malloc
 *
 * @param p pointer to the data region of the block
 *
 * @return A pointer to the header of the block
 */
static inline header * ptr_to_header(void * p) {
  return (header *)((char *) p - ALLOC_HEADER_SIZE); //sizeof(header));
}

/**
 * @brief Helper to manage deallocation of a pointer returned by the user
 *
 * @param p The pointer returned to the user by a call to malloc
 */
static inline void deallocate_object(void * p) {
  // TODO implement deallocation
  if (p == NULL) {
    return;
  }

  header * free = ptr_to_header(p);
  if (get_object_state(free) == UNALLOCATED) {
    fprintf(stderr, "Double Free Detected\n");
    assert(false);
    exit(1);
  }
  header * left_block = get_left_header(free);
  header * right_block = get_right_header(free);
  

  if (get_object_state(left_block) == UNALLOCATED && get_object_state(right_block) == UNALLOCATED) {
    int new_size = get_object_size(left_block) + get_object_size(free) + get_object_size(right_block);
    int index_right = (get_object_size(right_block) - ALLOC_HEADER_SIZE)/8 - 1;
    index_right = index_right > N_LISTS - 1 ? N_LISTS - 1 : index_right;

    int index_new = (new_size - ALLOC_HEADER_SIZE)/8 - 1;
    index_new = index_new > N_LISTS - 1 ? N_LISTS - 1 : index_new;

    header * freelist_right = &freelistSentinels[index_right];
    header * current = freelist_right->next;
    while (current != freelist_right) {
      if (current == right_block) {
      current->prev->next = current->next;
      current->next->prev = current->prev; 
      }
      current = current->next;
    } //delete right block from list
    int index_left = (get_object_size(left_block) - ALLOC_HEADER_SIZE)/8 - 1;
    index_left = index_left > N_LISTS - 1 ? N_LISTS - 1 : index_left;

    set_object_size(left_block, new_size);
    header * right =  get_header_from_offset(left_block, get_object_size(left_block));
    right->object_left_size = get_object_size(left_block); //update right block's left size
    if (index_left != index_new) {
      header * freelist_left = &freelistSentinels[index_left];
      header * current2 = freelist_left->next;
      while (current2 != freelist_left) {
        if (current2 == left_block) {
          current2->prev->next = current2->next;
          current2->next->prev = current2->prev; 
        }
        current2 = current2->next;
      } //delete left block from list
      move_to_appropriate_freelist(left_block);
    }

  } else if (get_object_state(left_block) == UNALLOCATED) {
    int new_size = get_object_size(left_block) + get_object_size(free);
    int index_left = (get_object_size(left_block) - ALLOC_HEADER_SIZE)/8 - 1;
    index_left = index_left > N_LISTS - 1 ? N_LISTS - 1 : index_left;

    int index_new = (new_size - ALLOC_HEADER_SIZE)/8 - 1;
    index_new = index_new > N_LISTS - 1 ? N_LISTS - 1 : index_new;

    set_object_size(left_block, new_size);
    header * right =  get_header_from_offset(left_block, get_object_size(left_block));
    right->object_left_size = get_object_size(left_block); //update right block's left size

    if (index_left != index_new) {
      header * freelist_left = &freelistSentinels[index_left];
      header * current = freelist_left->next;
      while (current != freelist_left) {
        if (current == left_block) {
          current->prev->next = current->next;
          current->next->prev = current->prev; 
        }
        current = current->next;
      } //delete left block from list
      move_to_appropriate_freelist(left_block);
    }
    
  } else if (get_object_state(right_block) == UNALLOCATED) {
    int new_size = get_object_size(free) + get_object_size(right_block);
    int index_right = (get_object_size(right_block) - ALLOC_HEADER_SIZE)/8 - 1;
    index_right = index_right > N_LISTS - 1 ? N_LISTS - 1 : index_right;

    int index_new = (new_size - ALLOC_HEADER_SIZE)/8 - 1;
    index_new = index_new > N_LISTS - 1 ? N_LISTS - 1 : index_new;

    int original_size = get_object_size(right_block);
    int state = 1;
    header * right =  get_header_from_offset(right_block, original_size);

    if (index_right != index_new) {
      header * freelist_right = &freelistSentinels[index_right];
      header * current = freelist_right->next;
      while (current != freelist_right) {
        if (current == right_block) {
          current->prev->next = current->next;
          current->next->prev = current->prev; 
        } 
        current = current->next;
      } //delete right block from list
      right_block = get_left_header(right_block); //move the pointer to the left
      set_object_size(right_block, new_size);
      move_to_appropriate_freelist(right_block);
      state = 0;
    }

    if (state) {
      right_block = get_left_header(right_block);
      set_object_size(right_block, new_size);
    } 
    right->object_left_size = get_object_size(right_block); //update right block's left size
  } else {
    move_to_appropriate_freelist(free);
  }
  set_object_state(free, UNALLOCATED);
}

/**
 * @brief Helper to detect cycles in the free list
 * https://en.wikipedia.org/wiki/Cycle_detection#Floyd's_Tortoise_and_Hare
 *
 * @return One of the nodes in the cycle or NULL if no cycle is present
 */
static inline header * detect_cycles() {
  for (int i = 0; i < N_LISTS; i++) {
    header * freelist = &freelistSentinels[i];
    for (header * slow = freelist->next, * fast = freelist->next->next; 
         fast != freelist; 
         slow = slow->next, fast = fast->next->next) {
      if (slow == fast) {
        return slow;
      }
    }
  }
  return NULL;
}

/**
 * @brief Helper to verify that there are no unlinked previous or next pointers
 *        in the free list
 *
 * @return A node whose previous and next pointers are incorrect or NULL if no
 *         such node exists
 */
static inline header * verify_pointers() {
  for (int i = 0; i < N_LISTS; i++) {
    header * freelist = &freelistSentinels[i];
    for (header * cur = freelist->next; cur != freelist; cur = cur->next) {
      if (cur->next->prev != cur || cur->prev->next != cur) {
        return cur;
      }
    }
  }
  return NULL;
}

/**
 * @brief Verify the structure of the free list is correct by checkin for 
 *        cycles and misdirected pointers
 *
 * @return true if the list is valid
 */
static inline bool verify_freelist() {
  header * cycle = detect_cycles();
  if (cycle != NULL) {
    fprintf(stderr, "Cycle Detected\n");
    print_sublist(print_object, cycle->next, cycle);
    return false;
  }

  header * invalid = verify_pointers();
  if (invalid != NULL) {
    fprintf(stderr, "Invalid pointers\n");
    print_object(invalid);
    return false;
  }

  return true;
}

/**
 * @brief Helper to verify that the sizes in a chunk from the OS are correct
 *        and that allocated node's canary values are correct
 *
 * @param chunk AREA_SIZE chunk allocated from the OS
 *
 * @return a pointer to an invalid header or NULL if all header's are valid
 */
static inline header * verify_chunk(header * chunk) {
	if (get_object_state(chunk) != FENCEPOST) {
		fprintf(stderr, "Invalid fencepost\n");
		print_object(chunk);
		return chunk;
	}
	
	for (; get_object_state(chunk) != FENCEPOST; chunk = get_right_header(chunk)) {
		if (get_object_size(chunk)  != get_right_header(chunk)->object_left_size) {
			fprintf(stderr, "Invalid sizes\n");
			print_object(chunk);
			return chunk;
		}
	}
	
	return NULL;
}

/**
 * @brief For each chunk allocated by the OS verify that the boundary tags
 *        are consistent
 *
 * @return true if the boundary tags are valid
 */
static inline bool verify_tags() {
  for (size_t i = 0; i < numOsChunks; i++) {
    header * invalid = verify_chunk(osChunkList[i]);
    if (invalid != NULL) {
      return invalid;
    }
  }

  return NULL;
}

/**
 * @brief Initialize mutex lock and prepare an initial chunk of memory for allocation
 */
static void init() {
  // Initialize mutex for thread safety
  pthread_mutex_init(&mutex, NULL);

#ifdef DEBUG
  // Manually set printf buffer so it won't call malloc when debugging the allocator
  setvbuf(stdout, NULL, _IONBF, 0);
#endif // DEBUG

  // Allocate the first chunk from the OS
  header * block = allocate_chunk(ARENA_SIZE);

  header * prevFencePost = get_header_from_offset(block, -ALLOC_HEADER_SIZE);
  insert_os_chunk(prevFencePost);

  lastFencePost = get_header_from_offset(block, get_object_size(block));

  // Set the base pointer to the beginning of the first fencepost in the first
  // chunk from the OS
  base = ((char *) block) - ALLOC_HEADER_SIZE; //sizeof(header);

  // Initialize freelist sentinels
  for (int i = 0; i < N_LISTS; i++) {
    header * freelist = &freelistSentinels[i];
    freelist->next = freelist;
    freelist->prev = freelist;
  }

  // Insert first chunk into the free list
  header * freelist = &freelistSentinels[N_LISTS - 1];
  freelist->next = block;
  freelist->prev = block;
  block->next = freelist;
  block->prev = freelist;
}

/* 
 * External interface
 */
void * my_malloc(size_t size) {
  pthread_mutex_lock(&mutex);
  header * hdr = allocate_object(size); 
  pthread_mutex_unlock(&mutex);
  return hdr;
}

void * my_calloc(size_t nmemb, size_t size) {
  return memset(my_malloc(size * nmemb), 0, size * nmemb);
}

void * my_realloc(void * ptr, size_t size) {
  void * mem = my_malloc(size);
  memcpy(mem, ptr, size);
  my_free(ptr);
  return mem; 
}

void my_free(void * p) {
  pthread_mutex_lock(&mutex);
  deallocate_object(p);
  pthread_mutex_unlock(&mutex);
}

bool verify() {
  return verify_freelist() && verify_tags();
}

