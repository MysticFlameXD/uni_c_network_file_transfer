#ifndef MY_COMMONS
#define MY_COMMONS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

struct header{
  unsigned char flag;
  /*
  flag==0x01 -> connect req
  flag==0x02 -> connect term
  flag==0x04 -> data packet
  flag==0x08 -> ack packet
  flag==0x10 -> connect accept
  flag==0x20 -> connect refuse
  */
  unsigned char pktseq;
  unsigned char ackseq;
  unsigned char unassigned;
  int senderid;
  int recvid;
  int metadata;
  char payload[];
  /*
  if flag==0x04, metadata = length of packet including payload
  if flag==0x20, metadata = int indicating error
    -2 = ID ALREADY TAKEN
    -3 = TOO MANY CONNECTIONS
  */
};

struct connection_info{
  int id;
  int current_packet;
  struct sockaddr_in addr;
};

void check_error(int res, char *msg);

int rdp_connect(int fd, struct sockaddr_in* addr);

struct connection_info* rdp_accept();

int rdp_close(int fd, int id, struct sockaddr_in* addr);

int rdp_read(struct header* packet, struct sockaddr_in dest_addr, FILE* fil, int current_packet, int id, int fd);

int rdp_write(struct connection_info ** connection_array, struct header* packet, FILE* fil, int fd, int n, int sz);

#endif
