/* Author: Corey ortiz   
   Date: 4/9/24
    */
    
    
    
/***
 *      ______ .___  ___. .______     _______.  ______              ____    __   __  
 *     /      ||   \/   | |   _  \   /       | /      |            |___ \  /_ | /_ | 
 *    |  ,----'|  \  /  | |  |_)  | |   (----`|  ,----'              __) |  | |  | | 
 *    |  |     |  |\/|  | |   ___/   \   \    |  |                  |__ <   | |  | | 
 *    |  `----.|  |  |  | |  |   .----)   |   |  `----.             ___) |  | |  | | 
 *     \______||__|  |__| | _|   |_______/     \______|            |____/   |_|  |_| 
 *                                                                                   
 */




#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "cache.h"
#include "mdadm.h"
#include "util.h"
#include "jbod.h"
#include "net.h"

int is_mounted = 0;  // variable in order to keep track ,throughout unitl the program terminates,if the mdam is mounted or not, in order to avoid mounting twice without having an unmount called before hand, and vice versa.
                    // Mounted = 1, Unmounted = 0           

int mdadm_mount(void) 
{
  
   // Checking if JBOD is mounted or not and that the operation is successfull
                                        
  if( (is_mounted == 0) && (jbod_client_operation( JBOD_MOUNT << 14, NULL) == 0))  // As per instructions were allowed to pass NULL for the parameter block 
  {                                  
      is_mounted = 1;
      return 1;
  }

  else
   {
    
      return -1; // if the operation fails or that the mdam is mounted, we return -1;
   }


}

int mdadm_unmount(void) 
{                                                   
  if( (is_mounted == 1) && (jbod_client_operation( JBOD_UNMOUNT << 14, NULL) == 0))   // Similar functionality as in the mount() and can have NULL for the block parameter
  {                                 
     // To inform that now the JDOB is unmounted and ready to be mounted again before any other operation.
    is_mounted = 0;
    return 1;
  }

  else
  {
    
    return -1;  // If operation of unmounting fails or that the JDOB was already unmounted. 
  }
}


int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) 
{

  if( (is_mounted == 0) || (len > 1024) || (addr > ( JBOD_DISK_SIZE * JBOD_NUM_DISKS ) ) || ( len != 0 && buf == NULL) || (len + addr) > ( JBOD_DISK_SIZE * JBOD_NUM_DISKS) )
  {
    return -1;  // testing the invalid parameters, simiilar to the mdadm_read() 
  }

// obtain the respective addresses
  uint32_t curr_address = addr;
  
  uint32_t disk_address =  addr / JBOD_DISK_SIZE;   // Locate which disk are we starting on.
  uint32_t block_address = (addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;    // Locate which block we are on. 


  uint8_t *temp_buff = malloc(JBOD_BLOCK_SIZE * sizeof(uint8_t));   // creating the temporary buffer to hold a block size worth of memory
  uint32_t read_so_far = 0; 
  uint32_t info_to_read;
  uint32_t offset;        // Indicating the position we are within the block.
  uint32_t remaining_length;  // Keeping track of how much reading we need to do
  uint32_t remainingSpace_currBlock;  // Keeping track of how much space can the current block hold

  

  while(read_so_far < len)  
  { 
    disk_address =  curr_address / JBOD_DISK_SIZE;       // As we are iterating through while loop we need to find the current disk ID and current block ID with the current address
    block_address = (curr_address % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
    offset = curr_address % JBOD_BLOCK_SIZE; 
    remaining_length  = len - read_so_far;  
    remainingSpace_currBlock = JBOD_BLOCK_SIZE - offset;

    if(remaining_length <= remainingSpace_currBlock) // checking if the remaining length can be able to read within the current block 
      {
        info_to_read = remaining_length;
      } 
    else
      {
        info_to_read = remainingSpace_currBlock;  
      }

    // Checking if it is a cache hit when cache is enabled
    if(cache_enabled() == true && cache_lookup(disk_address,block_address,buf) == 1)
    {
      read_so_far += info_to_read; // we do the next loop 
      curr_address += info_to_read;
    }

    else 
    {
      jbod_client_operation( disk_address << 28 | JBOD_SEEK_TO_DISK << 14, NULL);  
      jbod_client_operation( block_address << 20 | JBOD_SEEK_TO_BLOCK << 14, NULL);  // We need to make sure where we are before read 

      if( jbod_client_operation(JBOD_READ_BLOCK << 14, temp_buff) == -1 )  // Reading the block with the temp buffer.
      {
        free(temp_buff);  // Make sure of no memory leaks
        return -1; // Read Failed
      }
    
      memcpy( buf + read_so_far, temp_buff + offset, info_to_read); // Making sure what we read is stored into our main buffer 
       
      if(cache_enabled() == true)  // this is if we are using the cache and it was cache miss we insert into the cache.
      {
        cache_insert(disk_address,block_address,temp_buff);
      }
      
      read_so_far += info_to_read;
      curr_address += info_to_read;
    }


  }
  
  free(temp_buff);  // Prevent memory leaks

 
  return len;

}





int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf)   
{

  if( (is_mounted == 0) || (len > 1024) || (addr > ( JBOD_DISK_SIZE * JBOD_NUM_DISKS ) ) || ( len != 0 && buf == NULL) || (len + addr) > ( JBOD_DISK_SIZE * JBOD_NUM_DISKS) )
  {
    return -1;  // testing the invalid parameters, simiilar to the mdadm_read() 
  }

  uint32_t curr_addr = addr;
  uint32_t curr_diskID = addr / JBOD_DISK_SIZE;
  uint32_t curr_blockID = (addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
  uint8_t *temp_buf = malloc(JBOD_BLOCK_SIZE * sizeof(uint8_t));
  uint32_t written_so_far = 0;
  uint32_t info_to_write; 
  uint32_t offset;
  uint32_t remaining_length_to_write;
  uint32_t remainingSpace_of_currBlock;


  while(written_so_far < len)
  {
    
    curr_diskID = curr_addr / JBOD_DISK_SIZE;   //  Updating the current blockid and current diskid
    curr_blockID = (curr_addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
    offset = curr_addr % JBOD_BLOCK_SIZE;   // This is to ensure with current address we are on and how far we are in the current block we want to make sure we are not ignoring that content.
    remaining_length_to_write = len - written_so_far;
    remainingSpace_of_currBlock = JBOD_BLOCK_SIZE - offset;

    if(remaining_length_to_write <= remainingSpace_of_currBlock)  
      {
        info_to_write = remaining_length_to_write;
      }                                                            // This dicate how much can we write within the current block space we are in.
    else
      {
        info_to_write = remainingSpace_of_currBlock;
      }

   
    // Dealing with a cache hit  , if it does we need to seek to ensure we are going to write the new content, buf, and then update that in the cache
    if(cache_enabled() == true && cache_lookup(curr_diskID,curr_blockID,temp_buf) == 1)
    {
      jbod_client_operation( curr_diskID << 28 | JBOD_SEEK_TO_DISK << 14, NULL);
      jbod_client_operation( curr_blockID << 20 | JBOD_SEEK_TO_BLOCK << 14,   NULL);
      
      memcpy(temp_buf + offset, buf + written_so_far, info_to_write);  // Since buf is constant we need to copy the whole buf and what is already written into the temp buf therefore now we can write the correct content

      if(jbod_client_operation(JBOD_WRITE_BLOCK << 14, temp_buf) == -1)  // Doing the writing operation and making sure its successful
      {
        free(temp_buf);
        return -1;
      }

      cache_update(curr_diskID,curr_blockID, (const uint8_t*) temp_buf); // Update the cache with the new content of the specific disk and block 
      
    }

    else // Dealing with a cache miss if cache is being enabled
    {
      // We do the regular Mdadm Write Implementation as in Lab3 but now with the extra content of cache 
      jbod_client_operation( curr_diskID << 28 | JBOD_SEEK_TO_DISK << 14, NULL);
      jbod_client_operation( curr_blockID << 20 | JBOD_SEEK_TO_BLOCK << 14, NULL);

      if(jbod_client_operation(JBOD_READ_BLOCK << 14, temp_buf) == -1)  // This is reassure that we are not over writing content that we are not trying to write on
      {
        free(temp_buf);  // If reading fails we need to return -1 and make sure we don't have memory leaks
        return -1;   
      }

      if(cache_enabled() == true)  // We need to insert the content we read to the cache before we can do any write operation
      {
       cache_insert(curr_diskID,curr_blockID,temp_buf);  // Insert into the cache
      }

      // We need to seek before we write 
      jbod_client_operation( curr_diskID << 28 | JBOD_SEEK_TO_DISK << 14, NULL);
      jbod_client_operation( curr_blockID << 20 | JBOD_SEEK_TO_BLOCK << 14,   NULL);

      memcpy(temp_buf + offset, buf + written_so_far, info_to_write);  // Since buf is constant we need to copy the whole buf and what is already written into the temp buf therefore now we can write the correct content

      if(jbod_client_operation(JBOD_WRITE_BLOCK << 14, temp_buf) == -1)  // Doing the writing operation and making sure its successful
      {
        free(temp_buf);
        return -1;
      }

      if(cache_enabled() == true)
      {
        cache_update(curr_diskID,curr_blockID, (const uint8_t*)temp_buf); // Update the cache with the new content of the specific disk and block 
      }

  
    }

    written_so_far += info_to_write;
    curr_addr += info_to_write;



  }

free(temp_buf);
return len; 

}