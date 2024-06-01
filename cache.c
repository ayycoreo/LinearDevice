#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "cache.h"
#include "jbod.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;


// Create and Destroy is similar as unmount and mount in mdadm.c. 
int cache_create(int num_entries) {
  if (num_entries < 2 || num_entries > 4096 || cache != NULL)
  {
    return -1; // Making Sure for improper parameters and make sure that there isn't two cache creates in a row.
  }

  cache = malloc(num_entries * sizeof(cache_entry_t)); // dynamically allocate space for num_entries cache entries
  cache_size = num_entries; // Cache size is fixed. 

  for(int i = 0; i < cache_size; i++)
  {
    cache[i].valid = false;
  }

  return 1; // Successful Cache create
}

int cache_destroy(void) {
  if (cache == NULL)
  {
    return -1; // Can't destory cache that doesn't exist.
  }
  free(cache);
  cache = NULL;
  cache_size = 0;

  return 1; // Successful Cache Destory
}




// We need to check if the data we are seeking is already in the cache and no need to got the main memory
int cache_lookup(int disk_num, int block_num, uint8_t *buf) {

  if( cache == NULL || buf == NULL || cache[0].valid == false)
  {
    return -1; // Making sure we are having an existing cache, a non-NULL buf, and not an empty cache
  }

  num_queries++; // We must increment every time we call a lookup
  for(int i = 0; i < cache_size; i++)
  {
    // Checking if we have the disk and block that we want in the cache already
    if(cache[i].valid == true && cache[i].disk_num == disk_num && cache[i].block_num == block_num)
    {
      num_hits++; // We found it in the cache so it is a HIT
      clock++;
      cache[i].access_time = clock;
      memcpy(buf,cache[i].block, JBOD_BLOCK_SIZE);
      return 1; // Successful lookup
    }
  }
  return -1; // Did not find it in the cache

  

}

// Updates the blocks content with the new data in buf
void cache_update(int disk_num, int block_num, const uint8_t *buf) {

  for(int i = 0; i < cache_size; i++)
  {
    if(cache[i].disk_num == disk_num && cache[i].block_num == block_num)
    {
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);  // Finding the disk and block wihin the cache and updating it with the new buf
      cache[i].valid = true;
      clock++;
      cache[i].access_time = clock;
    } 
  }
  
}

// If the cache doesn't have the memory we are looking for we add it to the cache
int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  if (cache_size == 0 || buf == NULL || disk_num < 0 || disk_num > 16 || block_num < 0 || block_num > 256)
  {
    return -1; // Ensuring that we have an existing cache and that the buff is not the NULL and the disk & block num are valid
  }

  for(int i = 0; i < cache_size; i++) // If disk_num and block_nim exists already in the cache we want to update it with the new content
  {
    if(cache[i].valid != false && cache[i].disk_num == disk_num && cache[i].block_num == block_num)
    {
      cache_update(disk_num,block_num,buf);
      return -1;
    }
  }

  // If it doesn't exist we add it at the at the end if there is still space
  if(cache[cache_size-1].valid == false)
  {
    for(int i = 0; i < cache_size; i++)
    {
      if(cache[i].valid == false)
      {
        clock++;
        cache[i].disk_num = disk_num;
        cache[i].block_num = block_num;
        cache[i].valid = true;
        cache[i].access_time = clock;
        memcpy(cache[i].block,buf, JBOD_BLOCK_SIZE);
        return 1;
      }
    }
  
  }

  // If there is no space left in the cache we apply the LRU policy  (similar of finding the min number in a list functionality )

  int LRU = cache[0].access_time; // We need to find the least recently used cache line
  int LRU_index = 0; // we need to do where this LRU cache line is in the cache
  
  for (int i = 0; i < cache_size; i++)
  {
    if(cache[i].access_time < LRU)
    {
      LRU = cache[i].access_time;
      LRU_index = i;
    }
  }
  // Once we located the LRU we will overwrite the data with the new data we want
  cache[LRU_index].disk_num = disk_num;
  cache[LRU_index].block_num = block_num;
  cache[LRU_index].valid = true;
  memcpy(cache[LRU_index].block, buf, JBOD_BLOCK_SIZE);
  clock++;
  cache[LRU_index].access_time = clock;

  return 1;
}
// Returns true if the cache is proper and able to be used
bool cache_enabled(void) {
  return cache_size > 2 && cache != NULL;
}

void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}