#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"

// Helper function created using example from lecture slide to make passing arguments and commands to op variable easier. This function returns an unsigned 32-bit integer variable op with four fields of different widths relating to the disk ID, block number and commands that we use when calling jbod(). Each field is also unsigned 32-bit integer type. Function doesn't require all 4 arguments to be passed and zero can be used as input to clear out certain fields.
uint32_t op(uint32_t reserved, uint32_t command, uint32_t blockID, uint32_t diskID)
{
  
  // Unsigned integer type to hold disk ID, block ID and relevant command for jbod interface
  uint32_t fullOp = 0x0, tempRes, tempComm, tempBlock, tempDisk;

  tempRes = reserved;
  // Bit shift rest of the fields to the left using the described field widths in pdf table
  tempComm = command << 14;
  tempBlock = blockID << 20;
  tempDisk = diskID << 28;
  
  // All 4 separate fields/variables ORed together to form one single unsigned integer with any relevant commands or disk or block numbers.
  fullOp = tempRes | tempComm | tempBlock | tempDisk;

  // Helper function is type unsigned integer so variable of the same type must be returned at the end
  return fullOp;

}



// Flag variable to track status of whether linear device is currenty mounted on system or not. If 1, linear device mounted and if 0 then no device mounted.
int mountStatus = 0;

// Function to mount  the  linear  device when called. Must mount device before any other operations like SEEK or READ can be performed. Function  should  return  1  on  success  and -1  on failure. Calling this function the second time without calling mdadm_unmount() in between, should fail.
int mdadm_mount(void) {
  
  // if block checks device status to see if device already mounted, and if not, flag variable set to 1 (True) and jbod() called with the single function op() passed with only one argument i.e. the mount command.
  if (mountStatus == 0) 
  {

    mountStatus = 1;
    jbod_operation(op(0,JBOD_MOUNT,0,0), NULL);

    // Returns 1 upon successful mounting
    return 1;

  }
  
  return -1;

}


// Function to unmount  the  linear  device when called. Passing commands or operations like SEEK and READ will fail. Function  should  return  1  on  success  and -1  on failure. Calling this function the second time without calling mdadm_mount() in between, should fail.
int mdadm_unmount(void) {
  
  // if block checks device status to see if device already mounted, and if so, flag variable set to 0 (False) and jbod() called with the single function op() passed with only one argument i.e. the unmount command.
  if (mountStatus == 1)
  {
    
    mountStatus = 0;
    jbod_operation(op(0,JBOD_UNMOUNT,0,0), NULL);
    
    // Returns 1 upon successful un-mounting
    return 1;
  }

  return -1;

}


// Function reads read_len bytes  into  read_buf starting  at  start_addr.  Reading  from  an out-of-bound  linear  address  should  fail.  A  read length  larger  than  1,024  bytes  should  fail;  in  other  words, read_len can be 1,024 at most. Function accepts 3 arguments, unsigned 32-bit integer as starting memory address, the read length and an array to copy into what the function in current I/O is reading.
int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf)  {
  	
  // Checks if device mounted, if not, read fails and returns -1 since device needed to perform read operation
  if (mountStatus == 0)
  {

    return -1;

  }
  // Second set of conditions check for edge cases of read function i.e. the max. read length at any point that mdadm_read() is called 1024 bytes (At most 1024 bytes can be read each time function's called). If start address is less than zero or greater than 1MB then such a memory address is out of bounds and function fails.
  else if (read_len > 1024 || (start_addr + read_len) > 1048576 || (read_buf == NULL && read_len != 0) || start_addr < 0 || start_addr > 1048576)
  {

    return -1;

  }
  // Last else-if block takes care of situations where a device is mounted and read function can execute.
  else if (mountStatus == 1)
  {
   
    // x is a counter variable that tracks current byte position inn linear device until the while loop finishes executing (when read function copies all bytes from start_addr to (start_addr + read_length). Used for comparison in while loop condition.
    uint32_t x = start_addr;

    // y is a comparison variable that stores the final address position at which function copies bytes from the device.
    uint32_t y = start_addr + read_len;

    // Counter variable to keep track of how many bytes are still left to read. Defined and initialised to zero.
    int remainderBits = 0;


    // While loop continues to execute while the start address tracker variable is less than the sum of the start address and read length.
    while (x < y)
    {

      // Store is a temporary array to which jbod() copies the contents of the address it reads at a given disk and block. The amount jbod() copies to store at each call is of block size (256 bytes).
      uint8_t store[JBOD_BLOCK_SIZE];
      // disk ID holds the value of the desired disk that jbod() needs to read. Continously updates current address that's calculated in scenarios where a large read length requires the function to read across multiple disks and thus disk ID changes.
      int diskNum = x / JBOD_DISK_SIZE;
      // blockNum holds the continously updated block ID to which jbod() calls the read function on.
      int blockNum = (x % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
      // Because the start address can possibly start at any of the 255 byte positions in a block apart from the 0th position, the offset variable helps readjust the real start point for the memcpy() function below.
      int addrOffset = (x % (JBOD_DISK_SIZE)) % (JBOD_BLOCK_SIZE);

      // jbod() is called three times, with the first command finding the right disk using the values computed above, the second command finding the right block and pointing to its 0th byte position and the last command to read through that block and copy its contents to the specified array. When using the seek commands no argument is needed for the second parameter.
      jbod_operation(op(0,JBOD_SEEK_TO_DISK,0,diskNum), NULL);
      jbod_operation(op(0,JBOD_SEEK_TO_BLOCK,blockNum,0), NULL);
      jbod_operation(op(0,JBOD_READ_BLOCK,0,0), store);


      // Using both the offset calculated for every time that the block and disk position change in the while loop, this variable stores the integer value that can be added to the start address at the 0th byte of the block to start reading at the actual correct position in I/O.
      int currentBlockLeft = JBOD_BLOCK_SIZE - addrOffset;


      // If greater than the bounds of a single block, then offset used to calculate new disk and block ID for next while loop iteration.
      if (((x + JBOD_BLOCK_SIZE) <= y)|| x == start_addr)
      {

        // If read length is lesser than the number of bytes before the next block starts and the offset is not zero (address starts somewhere in the block) then contents of the block can be copied since after the first call all the required contents will be scanned.
        if (read_len <= currentBlockLeft)
        {

          memcpy(read_buf, store, read_len);
          break;

        }

        memcpy(read_buf + remainderBits, store + addrOffset, currentBlockLeft);

        x += currentBlockLeft;

        remainderBits += currentBlockLeft;

      }
      // If the sum of the start address and read length is less than or equal to the sum of the start address and block size, then memcpy() shifts the readbuf but however much is remaining to be read and copies this remaining amount from the temp array store.
      else if ((x + JBOD_BLOCK_SIZE) >= y)
      {

        memcpy(read_buf + remainderBits, store, read_len - remainderBits);
        break;

      }  

    }

  }  
    
  return read_len;

}



// Function reads write_len bytes  from the constant buffer  write_buf and writes these write_len bytes into the storage system starting at start_addr.  Writing  to  an out-of-bound  linear  address  should  fail.  A  write length  larger  than  1,024  bytes  should  fail;  in  other  words, write_len can be 1,024 at most. Function accepts 3 arguments, unsigned 32-bit integer as starting memory address, the write length and a constant user-supplied buffer from which to copy and write into the current I/O position.
int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf) {
  
  // Checks if device mounted, if not, write fails and returns -1 since device needed to perform write operation
  if (mountStatus == 0)
  {

    return -1;

  }
  // Second set of conditions check for edge cases of write function i.e. the max. write length at any point that mdadm_write() is called is 1024 bytes (At most 1024 bytes can be written into memory each time function's called). If start address is less than zero or greater than 1MB then such a memory address is out of bounds and function fails. Also edge cases like Null pointers are accounted for.
  else if (write_len > 1024 || (start_addr + write_len) > 1048576 || (write_buf == NULL && write_len != 0) || start_addr < 0 || start_addr > 1048576)
  {

    return -1;

  }
  // Last else-if block takes care of situations where a device is mounted and write function can execute.
  else if (mountStatus == 1)
  {

    // x is a counter variable that tracks current byte position in linear device until the while loop finishes executing (when write function overwrites all bytes from start_addr to (start_addr + write_length). Used for comparison in while loop condition.
    uint32_t x = start_addr;

    // y is a comparison variable that stores the final address position at which function writes bytes into the device.
    uint32_t y = start_addr + write_len;

    // Counter variable for how many bits of write_len have already been read/written and used to calculated how much left to read/write.
    uint32_t remainderBits = 0;

    // While loop continues to execute while the start address tracker variable is less than the sum of the start address and write length. The increment process isn't implemented because I am still dealing with the write_across_blocks test that keeps failing. Another reason I havn't incremented is that doing so will cause an error from the pointers memcpy() uses below.
    while (x < y)
    {
      // Temp array variable of block size to hold contents of current block in I/O position during read/write process.
      uint8_t store[JBOD_BLOCK_SIZE] = {0};

      // disk ID holds the value of the desired disk that jbod() needs to write to. Continously updates current address that's calculated in scenarios where a large write length requires the function to write across multiple disks and thus disk ID changes.
      int diskNum = x / JBOD_DISK_SIZE;

      // blockNum holds the continously updated block ID to which jbod() calls the write function on.
      int blockNum = (x % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;

      // Because the start address can possibly start at any of the 255 byte positions in a block apart from the 0th position, this variable stores the integer value that can be added to the start address at the 0th byte of the block to start writing at the actual correct position in I/O.
      uint32_t addrOffset = (x % (JBOD_DISK_SIZE)) % (JBOD_BLOCK_SIZE);

      // This variable keeps live count of how many byte are left before the end of the block and the diskID and blockID will change. Calculating this helps with setting the condition for writing within only one block.
      uint32_t currentBlockLeft = (uint32_t) (JBOD_BLOCK_SIZE - addrOffset);

      // If greater than the bounds of a single block, then offset used to calculate new disk and block ID for next while loop iteration or if already at start_addr then inner condition executes.
      if (((x + (uint32_t) JBOD_BLOCK_SIZE) <= y)|| x == start_addr)
      {
        // If write length is less than the number of bytes before the next block starts then it means that the amount to write is small and no block or disk traversal is necessary. In this case, the whole of write_buf can be written into memory using the jbod command and memory since after the first call of the function, all the required contents will be scanned and written.
        if (write_len <= currentBlockLeft)
        {
          // jbod() is called two times, with the first command finding the right disk using the values computed above and the second command finding the right block and pointing to its 0th byte position. When using the seek commands no argument is needed for the second parameter.
          jbod_operation(op(0,JBOD_SEEK_TO_DISK,0,diskNum), NULL);
          jbod_operation(op(0,JBOD_SEEK_TO_BLOCK,blockNum,0), NULL);
          jbod_operation(op(0, JBOD_READ_BLOCK, 0,0), store);
          memcpy(store + addrOffset, write_buf + remainderBits, write_len);
          jbod_operation(op(0,JBOD_SEEK_TO_DISK,0,diskNum), NULL);
          jbod_operation(op(0,JBOD_SEEK_TO_BLOCK,blockNum,0), NULL);
          jbod_operation(op(0,JBOD_WRITE_BLOCK,0,0), store);
          break;
          // The last jbod command is to write through that current block and overwrite its contents from write_buf. When using the seek commands no argument is needed for the second parameter.

        }

        jbod_operation(op(0,JBOD_SEEK_TO_DISK,0,diskNum), NULL);
        jbod_operation(op(0,JBOD_SEEK_TO_BLOCK,blockNum,0), NULL);
        jbod_operation(op(0, JBOD_READ_BLOCK, 0,0), store);
        memcpy(store + addrOffset, write_buf + remainderBits, currentBlockLeft);
        jbod_operation(op(0,JBOD_SEEK_TO_DISK,0,diskNum), NULL);
        jbod_operation(op(0,JBOD_SEEK_TO_BLOCK,blockNum,0), NULL);
        jbod_operation(op(0,JBOD_WRITE_BLOCK,0,0), store);

        remainderBits += currentBlockLeft;
        
        x += currentBlockLeft;


      }
      else if ((x + (JBOD_BLOCK_SIZE)) >= y)
      {

        jbod_operation(op(0,JBOD_SEEK_TO_DISK,0,diskNum), NULL);
        jbod_operation(op(0,JBOD_SEEK_TO_BLOCK,blockNum,0), NULL);
        jbod_operation(op(0, JBOD_READ_BLOCK, 0,0), store);
        memcpy(store, write_buf + remainderBits, write_len - remainderBits);
        jbod_operation(op(0,JBOD_SEEK_TO_DISK,0,diskNum), NULL);
        jbod_operation(op(0,JBOD_SEEK_TO_BLOCK,blockNum,0), NULL);
        jbod_operation(op(0,JBOD_WRITE_BLOCK,0,0), store);
        break;

      }  

    }

  } 

  // Function has int return type and returns the amount of bytes written
  return write_len;

}
