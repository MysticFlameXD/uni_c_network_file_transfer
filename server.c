#include "common.h"
#include "send_packet.h"

int main(int argc, char *argv[]){
  if (argc < 5){
    printf("usage: %s <port-number> <file name> <n amount of files> <loss-probability>\n", argv[0]);
    return EXIT_FAILURE;
  }

  set_loss_probability(atof(argv[4]));

  int fd, rc, n, curr_conns, finished_conns;
  long sz;
  FILE *fil;
  fd_set fds;
  struct sockaddr_in my_addr, src_addr;
  socklen_t addr_len;
  char *packet;
  struct connection_info ** connection_array;
  struct timeval tv;

  curr_conns = 0;
  finished_conns = 0;
  n = atoi(argv[3]);
  connection_array = malloc(n * sizeof(struct connection_info*));
  packet = malloc(sizeof(struct header));

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  check_error(fd, "socket");

  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(atoi(argv[1]));
  my_addr.sin_addr.s_addr = INADDR_ANY;

  rc = bind(fd, (struct sockaddr*)&my_addr, sizeof(struct sockaddr_in));
  check_error(rc, "bind");

  fil = fopen(argv[2], "r");
  if(fil == NULL){
    perror("Error: File does not exist");
    fclose(fil);
    free(connection_array);
    free(packet);
    close(fd);
    return EXIT_FAILURE;
  }

  fseek(fil, 0L, SEEK_END);
  sz = ftell(fil);
  fseek(fil, 0L, SEEK_SET);

  /*
  1. sjekker om en ny RDP-forbindelse forespørsel er mottatt
  2. prøver å levere neste pakke (eller siste tomme) til hver tilkoblede RDP-klient
  3. sjekker om en RDP-forbindelse er stengt
  4. hvis ingen av de 3 over har skjedd, vent og prøv igjen
  */
  while(finished_conns < n){
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    rc = select(FD_SETSIZE, &fds, NULL, NULL, &tv);
    check_error(rc, "select");

    if(rc == 0){ //timeout triggered, no packet
      // printf("timeout triggered\n"); //DEBUG
      for(int i = 0; i < curr_conns; i++){
        if(connection_array[i]){ //dont progress current_packet, instead send previous one
          connection_array[i]->current_packet--;
          struct header* tmp_packet;
          tmp_packet = malloc(sizeof(struct header));
          tmp_packet->senderid = connection_array[i]->id;
          rdp_write(connection_array, tmp_packet, fil, fd, n, sz);
          free(tmp_packet);
        }
      }
      continue;
    }

    addr_len = sizeof(struct sockaddr_in);
    rc = recvfrom(fd, packet, sizeof(struct header), 0, (struct sockaddr*)&src_addr, &addr_len);
    check_error(rc, "recvfrom");
    // printf("recieved packet\n"); //DEBUG

    struct header *read_packet = (struct header*) packet;
    read_packet->senderid = ntohs(read_packet->senderid);
    //rdp_accept()
    // check:
    // is there a connection with that ID
    // is the connection list full
    if(read_packet->flag == 0x01){ // new connection req
      // printf("new connection\n"); //DEBUG
      struct connection_info* conn;
      conn = rdp_accept(fd, &src_addr, read_packet, connection_array, curr_conns, n);
      if(conn){
        connection_array[curr_conns] = conn;
        curr_conns++;
        printf("CONNECTED %d %d\n\n", conn->id, 0);
        rdp_write(connection_array, read_packet, fil, fd, n, sz);
      }
    }

    if(read_packet->flag == 0x08){ // ack packet
      // printf("ack recieved: %d\n", read_packet->ackseq); //DEBUG
      rdp_write(connection_array, read_packet, fil, fd, n, sz);
    }

    if(read_packet->flag == 0x02){ // connection term
      finished_conns++;
      for(int i = 0; i < curr_conns; i++){
        if(connection_array[i]){
          if(read_packet->senderid == connection_array[i]->id){
            free(connection_array[i]);
            connection_array[i] = NULL;
          }
        }
      }
      printf("DISCONNECTED %d %d\n\n", read_packet->senderid, 0);
    }

  } // n files delivered

  // for(int i = 0; i < n; i++){ //free connection_info
  //   free(connection_array[i]);
  // }
  free(connection_array); //free connection_array
  fclose(fil);
  free(packet);
  close(fd);
  return EXIT_SUCCESS;
}
