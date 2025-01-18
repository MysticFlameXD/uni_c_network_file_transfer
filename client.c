#include "common.h"
#include "send_packet.h"

int main(int argc, char *argv[]){
  if (argc < 4){
    printf("usage: %s <server IP> <server port> <loss-probability>\n", argv[0]);
    return EXIT_FAILURE;
  }

  set_loss_probability(atof(argv[3]));

  int fd, wc, rc, id, bufsize;
  unsigned int current_packet;
  struct sockaddr_in dest_addr;
  struct in_addr ip_addr;
  socklen_t addr_len;
  char *char_packet;
  char *filename;
  FILE *fil;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  check_error(fd, "socket");

  wc = inet_pton(AF_INET, argv[1], &ip_addr.s_addr);
  check_error(wc, "inet_pton");
  if(!wc){
    fprintf(stderr, "Invalid IP address: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(atoi(argv[2]));
  dest_addr.sin_addr = ip_addr;

  id = rdp_connect(fd, &dest_addr);
  if(id < 0){
    return EXIT_FAILURE;
  }
  printf("id: %d\n", id); //DEBUG

  filename = malloc((sizeof(char)*20));
  sprintf(filename, "kernel-file-%d", id);
  fil = fopen(filename, "r");
  if(fil){
    fprintf(stderr, "Warning: File %s already exists\n", filename);
    fprintf(stderr, "Overwriting...\n");
    fclose(fil);
  }
  fil = fopen(filename, "w");

  addr_len = sizeof(struct sockaddr_in);
  bufsize = sizeof(struct header) + (1000 * sizeof(char));
  char_packet = malloc(bufsize);
  current_packet = 0;

  rc = recvfrom(fd, char_packet, bufsize, 0, (struct sockaddr*)&dest_addr, &addr_len);
  check_error(rc, "recvfrom");

  struct header* packet = (struct header*) char_packet;
  packet->recvid = ntohs(packet->recvid);
  packet->metadata = ntohs(packet->metadata);

  while(packet->metadata != sizeof(struct header)){ //payload != 0
    // printf("recieved packet\n");  //DEBUG
    // printf("packet n: %d\n", current_packet); //DEBUG
    // if(current_packet % 256 == packet->pktseq){ //DEBUG
      // printf("already got this packet\n"); //DEBUG
      // printf("packet %d should be the same after ack packet\n", current_packet); //DEBUG
    // } //DEBUG
    current_packet = rdp_read(packet, dest_addr, fil, current_packet, id, fd);
    // printf("ack_packet sent\n"); //DEBUG
    // printf("new packet n: %d\n\n", current_packet); //DEBUG

    rc = recvfrom(fd, char_packet, bufsize, 0, (struct sockaddr*)&dest_addr, &addr_len);
    check_error(rc, "recvfrom");
    packet = (struct header*) char_packet;
    packet->recvid = ntohs(packet->recvid);
    packet->metadata = ntohs(packet->metadata);
  }

  wc = rdp_close(fd, id, &dest_addr);
  check_error(wc, "rdp_close");
  printf("Transfer complete: %s\n", filename);

  free(char_packet);
  free(filename);
  fclose(fil);
  close(fd);
  return EXIT_SUCCESS;
}
