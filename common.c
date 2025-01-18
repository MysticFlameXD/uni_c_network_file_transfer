#include "common.h"
#include "send_packet.h"

int rdp_connect(int fd, struct sockaddr_in* dest_addr){
  int wc, rc, id;
  fd_set fds;
  struct timeval tv;

  srand(time(NULL));
  id = (rand() % 30000); //ids 0-999

  struct header packet;
  packet.flag = 0x01;
  packet.pktseq = 0;
  packet.ackseq = 0;
  packet.unassigned = 0;
  packet.senderid = htons(id);
  packet.recvid = 0;
  packet.metadata = 0;

  wc = sendto(fd, (char*)&packet, sizeof(struct header), 0, (struct sockaddr*)dest_addr, sizeof(struct sockaddr_in));
  check_error(wc, "sendto rdp_connect");

  FD_ZERO(&fds);
  FD_SET(fd, &fds);

  tv.tv_sec = 1;
  tv.tv_usec = 0;
  rc = select(FD_SETSIZE, &fds, NULL, NULL, &tv);
  if(rc == 0){
    fprintf(stderr, "Error: Connection timeout\n");
    return -1;
  }

  if(FD_ISSET(fd, &fds)){
    rc = read(fd, (struct header*)&packet, sizeof(struct header));
    packet.metadata = ntohs(packet.metadata);
    packet.recvid = ntohs(packet.recvid);
    if(packet.flag == 0x20){
      // printf("flag = 0x20\n");
      if(packet.metadata == 2){
        fprintf(stderr, "Error: ID already taken\n");
        return -2;
      }
      if(packet.metadata == 3){
        fprintf(stderr, "Error: Too many connections\n");
        return -3;
      }
    }
    // printf("0x%x\n", packet.flag);
    printf("server-id: %d\n", packet.senderid);
    printf("client-id: %d\n\n", packet.recvid);
  }

  return packet.recvid;
}

struct connection_info *rdp_accept(int fd, struct sockaddr_in* addr, struct header* in_packet, struct connection_info ** connection_array, int curr_conns, int n){
  int wc;
  int id = in_packet->senderid;

  struct header out_packet;
  out_packet.flag = 0;
  out_packet.pktseq = 0;
  out_packet.ackseq = 0;
  out_packet.unassigned = 0;
  out_packet.senderid = 0;
  out_packet.recvid = htons(id);
  out_packet.metadata = 0;

  if(curr_conns >= n){
    //reject, too many conns
    out_packet.flag = 0x20;
    out_packet.metadata = htons(3);

    wc = sendto(fd, (char*)&out_packet, sizeof(struct header), 0, (struct sockaddr*)addr, sizeof(struct sockaddr_in));
    check_error(wc, "sendto rej -3");
    printf("NOT CONNECTED %d %d\n", id, 0);
    return NULL;
  }

  for(int i = 0; i < curr_conns; i++){
    if(connection_array[i]){
      if(id == connection_array[i]->id){
        //reject, id already taken
        out_packet.flag = 0x20;
        out_packet.metadata = htons(2);

        wc = sendto(fd, (char*)&out_packet, sizeof(struct header), 0, (struct sockaddr*)addr, sizeof(struct sockaddr_in));
        check_error(wc, "sendto rej -2");
        // printf("packet deny\n");
        return NULL;
      }
    }
  }
  //accept
  // printf("packet accept\n");
  out_packet.flag = 0x10;

  wc = sendto(fd, (char*)&out_packet, sizeof(struct header), 0, (struct sockaddr*)addr, sizeof(struct sockaddr_in));
  check_error(wc, "sendto acc");

  struct connection_info* conn;
  conn = malloc(sizeof(struct connection_info));
  conn->id = id;
  conn->current_packet = 0;
  conn->addr = *addr;

  return conn;
}

int rdp_close(int fd, int id, struct sockaddr_in* dest_addr){
  int wc;

  struct header packet;
  packet.flag = 0x02;
  packet.pktseq = 0;
  packet.ackseq = 0;
  packet.unassigned = 0;
  packet.senderid = htons(id);
  packet.recvid = 0;
  packet.metadata = 0;

  //if failed return -1
  wc = sendto(fd, (char*)&packet, sizeof(struct header), 0, (struct sockaddr*)dest_addr, sizeof(struct sockaddr_in));
  return wc;
}

int rdp_read(struct header* packet, struct sockaddr_in dest_addr, FILE* fil, int current_packet, int id, int fd){
  /* order of operations:

  check if packet has been recieved before (idempotency)->re-send ACK packet
  OR
  write length of payload into file
  send ACK packet

  return current_packet
  */
  struct header ack_packet;
  // printf("in rdp_read\n");
  if(current_packet % 256 == packet->pktseq){ //ack has not been recieved
    // printf("resending in rdp_read ack\n"); //DEBUG
    ack_packet.flag = 0x08;
    ack_packet.pktseq = packet->pktseq;
    ack_packet.ackseq = packet->pktseq;
    ack_packet.unassigned = 0;
    ack_packet.senderid = htons(id);
    ack_packet.recvid = 0;
    ack_packet.metadata = 0;

    send_packet(fd, (char*)&ack_packet, sizeof(struct header), 0, (struct sockaddr*)&dest_addr, sizeof(struct sockaddr_in));
    return current_packet;
  }
  // new packet
  // printf("sending new ack\n");
  int wc, new_packet;
  int size = packet->metadata - sizeof(struct header);
  int wsize = size*sizeof(char);
  wc = fwrite(packet->payload, sizeof(char), size, fil);
  if(wc != wsize){
    fprintf(stderr, "Error: short write count\n");
  }

  ack_packet.flag = 0x08;
  ack_packet.pktseq = packet->pktseq;
  ack_packet.ackseq = packet->pktseq;
  ack_packet.unassigned = 0;
  ack_packet.senderid = htons(id);
  ack_packet.recvid = 0;
  ack_packet.metadata = 0;

  send_packet(fd, (char*)&ack_packet, sizeof(struct header), 0, (struct sockaddr*)&dest_addr, sizeof(struct sockaddr_in));

  new_packet = current_packet+1;
  // printf("%d\n", new_packet);
  return new_packet;
}

int rdp_write(struct connection_info ** connection_array, struct header* packet, FILE* fil, int fd, int n, int sz){
  /* order of operations:
  where did ack come from
  find connection_info from id

  has final packet already been sent
  sent empty packet
  OR
  send (up to) 1k bytes * current_packet to id
  */
  int id = packet->senderid;
  int len, packet_size;
  int *current_packet;
  struct sockaddr_in addr;
  struct header* out_packet;
  char *pl;

  // printf("in rdp_write\n"); //DEBUG
  for(int i = 0; i < n; i++){
    if(connection_array[i]){ //not null
      if(connection_array[i]->id == id){
        // printf("found id: %d\n", id); //DEBUG
        current_packet = &(connection_array[i]->current_packet);
        addr = connection_array[i]->addr;
        break;
      }
    }
  }

  if(((*current_packet) * 1000) > sz){ //done with file
    out_packet = malloc(sizeof(struct header));
    out_packet->flag = 0x04;
    out_packet->pktseq = 0;
    out_packet->ackseq = 0;
    out_packet->unassigned = 0;
    out_packet->senderid = 0;
    out_packet->recvid = htons(id);
    out_packet->metadata = htons(sizeof(struct header));

    send_packet(fd, (char*)out_packet, sizeof(struct header), 0, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
    free(out_packet);
    return 0;
  }

  fseek(fil, ((*current_packet)*1000L), SEEK_SET);

  if((((*current_packet) + 1) * 1000) > sz){
    len = sz - ((*current_packet) * 1000);
    pl = malloc(len * sizeof(char));
    fread(pl, sizeof(char), len, fil);
  } else{
    len = 1000;
    pl = malloc(len * sizeof(char));
    fread(pl, sizeof(char), len, fil);
  }

  (*current_packet)++;
  packet_size = (sizeof(struct header) + (len * (sizeof(char))) );

  out_packet = malloc(packet_size);
  out_packet->flag = 0x04;
  out_packet->pktseq = *current_packet;
  out_packet->ackseq = 0;
  out_packet->unassigned = 0;
  out_packet->senderid = 0;
  out_packet->recvid = htons(id);
  out_packet->metadata = htons(packet_size);
  memcpy((&out_packet->payload), pl, len);

  send_packet(fd, (char*)out_packet, packet_size, 0, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));
  free(pl);
  free(out_packet);
  return 0;
}

void check_error(int res, char *msg){
  if(res == -1){
    perror(msg);
    exit(EXIT_FAILURE);
  }
}
