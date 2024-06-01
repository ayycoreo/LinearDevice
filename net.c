#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {

  int content_to_read = 0;
  int read_so_far = 0;

  while(read_so_far < len)
  {
    int size_to_read = len - read_so_far;  // Knowing how how much needs to be read per loop 
    content_to_read = read(fd, buf + read_so_far, size_to_read);
    if(content_to_read < 0)
    {
      return false; // there was a failure in reading 
    }
    read_so_far += content_to_read; // We increment based on how much was actually read
  }

  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {    // Function is similar to nread 

  int content_to_write;
  int written_so_far = 0;
  while(written_so_far < len)
  {
    int size_to_write = len - written_so_far;
    content_to_write = write(fd, buf + written_so_far, size_to_write);
    if(content_to_write < 0)
    {
      return false; // If calling the system call "write" fails 
    }
    written_so_far += content_to_write; // We increment based on how much was actually written
  }
  
  return true;

}

/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the return value of the server side calling the corresponding jbod_operation function.
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block) {

  uint16_t length;

  // We read the packet header first
  uint8_t header[HEADER_LEN];

  if( nread(sd, HEADER_LEN, header) == false)
  {
    return false; // if reading the packet header fails we return false
  }

  memcpy(&length, header, sizeof(uint16_t));
  length = ntohs(length);  // We want the network back to ints

  // We want to get the op and return from header
  memcpy(op, header + 2, sizeof(uint32_t)); // bytes 2-5 opcode, JBOD protool packet format                                        
  memcpy(ret, header + 6, sizeof(uint16_t)); /// bytes 6-7 return , JBOD protocol packet format
  
  *op = ntohl(*op); // We want to make op value back to its int value from the network 
  *ret = ntohs(*ret); // We want to make ret value back it its int value from the network  

                                                                                                      
  
  
  // Testing if there is actually needed to read a block size worth of data and make sure we have an existing block
  if(length == HEADER_LEN + JBOD_BLOCK_SIZE)
  {
    if(nread(sd, JBOD_BLOCK_SIZE, block) == false)
    {
      return false;
    }
  }
  

  return true;

}




/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {


  uint16_t length = HEADER_LEN;   // 8 


  uint8_t cmd = (op >> 14) & 0x3f;  // I need to grab the cmd bits by shifting by 14 and "AND" with a hex representation of 63.  

  
  if( cmd == JBOD_WRITE_BLOCK) // If the command is a write then we need to add the length by a whole 256 (Block Size), if we have a read we dont need to add anything.
  {
    length += JBOD_BLOCK_SIZE;  // 264
  }
  
  // creating the packet buffer 
  uint8_t packet[length]; // length- 8 bytes or the 264 if we need to write   

  uint16_t network_length = htons(length);
  memcpy(packet, &network_length, sizeof(uint16_t));    

  uint32_t op_network = htonl(op);
  memcpy( packet + 2, &op_network, sizeof(uint32_t));

  if( cmd == JBOD_WRITE_BLOCK ) // Copy the block if it exists into the packet to be able to prep the nwrite system call
  {
    memcpy(packet + HEADER_LEN, block, JBOD_BLOCK_SIZE);
  } 

  
  if( nwrite(sd, length, packet) == false) // Send the entire packet 
  {
    return false;
  }
  

  return true;
}



/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {

  cli_sd = socket(AF_INET, SOCK_STREAM,0); // Creating the socket 

  if(cli_sd == -1)
  {
    return false; // This is if creating the socket fails.

  }
 
  struct sockaddr_in server_addr;  // Setting up the server address
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);

  if( inet_pton(AF_INET, ip, &server_addr.sin_addr) == 0) // Getting the IPv6 address in binary 
  {
    return false;
  }

  if(connect(cli_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) // Here is when we actually try to connect to the server 
  {
    return false; // if connecting to the server fails
  }

  return true; // A successful jbod connect

}




/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {

  if(cli_sd != -1)
  {
    close(cli_sd);
    cli_sd = -1;
  }
}



/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {

  if( cli_sd == -1)
  {
    return -1; // make sure we are connected to the server
  }

  // Sends the JBOD operation to the server
  if( send_packet(cli_sd, op, block) == false)
  {
    return -1;
  }

  // receive and process the response from the server
  uint16_t response_return;
  uint32_t response_op;

  if( recv_packet(cli_sd, &response_op, &response_return, block) == false)
  {
    return -1;
  }
  if (response_op != op || response_return == -1)
  {
    return -1;
  }

  return 0;
}
