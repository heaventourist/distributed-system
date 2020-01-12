#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include "sendto_dbg.h"
#include "util.h"


unsigned long fileSize = 0;
unsigned long storageSize = 0;
unsigned long numSlotsToFill = 0;

int main(int argc, char **argv)
{
  int rate;
  char dest_file[20];
  fd_set master;
  fd_set read_fds;
  int fdmax;
  int window[WINDOW_SIZE] = {0};
  struct sockaddr_storage their_addr;
  socklen_t addr_len;
  char s[INET6_ADDRSTRLEN] = {'\0'};
  char curPeer[INET6_ADDRSTRLEN] = {'\0'};

  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  int nbytes;
  unsigned long numDataTrans = 0;
  char buf[BUF_SIZE];
  unsigned long seqNum = 0;
  char** storage = NULL;

  int numPacket;
  struct timeval tv;

  tv.tv_sec = 2;
  tv.tv_usec = 0;

  FILE* log;
  unsigned long logInterval = INTERVAL;

  clock_t start;
  clock_t checkpoint_time;
  unsigned long checkpoint_data=0, checkpoint_file=0;

  if(argc != 2) {
      fprintf(stderr, "Usage: ./ncp <loss_rate_percent>\n");
      exit(0);
  }

  rate = atoi(argv[1]);

  printf("rate=%d\n", rate);

  /* Call this once to initialize the coat routine */
  sendto_dbg_init(rate);
  FD_ZERO(&master);
  FD_ZERO(&read_fds);

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;

  if((rv=getaddrinfo(NULL, SERVERPORT, &hints, &servinfo)) != 0){
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
      return 1;
  }

  // loop through all the results and make a socket
  for(p=servinfo; p!=NULL; p=p->ai_next){
      if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
          perror("ncp: socket");
          continue;
      }
      if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
        close(sockfd);
        perror("listenner: bind");
        continue;
      }
      break;
  }

  freeaddrinfo(servinfo);

  if(p==NULL){
      fprintf(stderr, "rcv: failed to bind\n");
      exit(1);
  }

  printf("server: waiting for connection...\n");

  // readfile and process
  FD_SET(sockfd, &master);
  fdmax = sockfd;
  for(;;){
    read_fds = master;
    if(select(fdmax+1, &read_fds, NULL, NULL, &tv) == -1){
        perror("select error");
        exit(1);
    }
    if(FD_ISSET(sockfd, &read_fds)){
      addr_len = sizeof their_addr;
      if((nbytes = recvfrom(sockfd, buf, sizeof buf, 0, (struct sockaddr*)&their_addr, &addr_len)) < 0){
        perror("recvfrom\n");
        exit(1);         
      }

      numDataTrans += nbytes;
      if(numDataTrans>0 && numDataTrans>logInterval){
        clock_t cur_time = clock();
        double duration = (double)(cur_time - checkpoint_time)/CLOCKS_PER_SEC;
        checkpoint_time = cur_time;
        unsigned long trans_data = numDataTrans - checkpoint_data;
        checkpoint_data = numDataTrans;
        unsigned long trans_file = fileSize - checkpoint_file;
        checkpoint_file = fileSize;
        logInterval += INTERVAL;
        fprintf(log, "numDataTrans: %lu bytes, filesize_recvd: %lu bytes, transfer_rate: %.2f Mbits/s, file_transferrate: %.2f Mbits/s\n", numDataTrans, fileSize, (double)trans_data/(duration*MBITS), (double)trans_file/(duration*MBITS));
      }

      const char * peer = inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr*)&their_addr), s, sizeof s);

      if(storage == NULL){
        strcpy(curPeer, peer);
      }
      // printf("curpeer: %s, remotePeer: %s\n", curPeer, peer);
      if(strcmp(peer, curPeer)!=0){
        struct Packet* packet = (struct Packet*)calloc(1, sizeof(struct Packet));
        packet->status = WAIT;
        int packetSize = sizeof(struct Packet);
        struct Packet* ptr = packet;
        while(packetSize > 0){
            nbytes = sendto_dbg(sockfd, (char*)packet, packetSize, 0, (struct sockaddr*)&their_addr, addr_len);
            if(nbytes == -1){
                perror("ncp: send to error");
                exit(1);
            }
            packetSize -= nbytes;
            ptr += nbytes;
        }
        free(packet);
        // printf("listener: got packet from %s, but they have to wait\n", peer);
        continue;
      }
      // printf("listener: got packet from %s\n", peer);
      struct Packet* recvPacket = (struct Packet*)calloc(1, sizeof(struct Packet));
      memcpy(recvPacket, buf, sizeof(struct Packet));
      // printf("%lu\n", recvPacket->seqNum);
      if(recvPacket->status == SYN && seqNum == 0){
        start = clock();
        checkpoint_time = start;

        if((log = fopen("rcv.log", "a+")) == NULL){
          perror("rcv: failed to read the file");
          exit(1);
        }

        if((nbytes = sendPacket(SYNACK, 0, 0, 0, NULL, (struct sockaddr*)&their_addr, addr_len, sockfd, storage))==0){
            fprintf(stderr, "Packet SYN sending error.\n");
            return 1;
        }
      }else if(recvPacket->status == SEND){
        if(storage == NULL && recvPacket->seqNum == 0){
          numSlotsToFill = storageSize = numPacket = recvPacket->numPacket;
          storage = (char **)calloc(storageSize, sizeof(char*));
          for(int i=0; i<storageSize; i++){
            storage[i] = (char*)calloc(SLOT_SIZE, sizeof(char));
          }
          memcpy(dest_file, recvPacket->filename, strlen(recvPacket->filename));
        }else if(storage == NULL && recvPacket->seqNum > 0){
          continue;
        }
        
        if(window[recvPacket->seqNum-seqNum]==1){
          continue;
        }

        if(recvPacket->seqNum<seqNum){

          if((nbytes = sendPacket(ACK, 0, seqNum, numPacket, NULL, (struct sockaddr*)&their_addr, addr_len, sockfd, storage))==0){
              fprintf(stderr, "Packet SEND sending error.\n");
              return 1;
          }
          continue;
        }
        memcpy(storage[recvPacket->seqNum], recvPacket->data, recvPacket->dataSize);
        fileSize += recvPacket->dataSize;
        window[recvPacket->seqNum-seqNum] = 1;

        
        if(checkWindow(window) == FULL){
          for(int i=0; i<WINDOW_SIZE; i++){
            window[i] = 0;
          }
          
          seqNum+=(WINDOW_SIZE<numSlotsToFill?WINDOW_SIZE:numSlotsToFill);
          numSlotsToFill -= WINDOW_SIZE<numSlotsToFill?WINDOW_SIZE:numSlotsToFill;

          if((nbytes = sendPacket(ACK, 0, seqNum, numPacket, NULL, (struct sockaddr*)&their_addr, addr_len, sockfd, storage))==0){
              fprintf(stderr, "Packet ACK sending error.\n");
              return 1;
          }

          if(numSlotsToFill == 0 && seqNum!=0){
            clock_t cur_time = clock();
            double time_cost = (double)(cur_time - start)/CLOCKS_PER_SEC;
            checkpoint_data = 0;
            checkpoint_file = 0;
            checkpoint_time = 0;
            logInterval = INTERVAL;
            fprintf(log, "transmission time cost: %.2f seconds, dataTrans: %lu bytes, fileTrans: %lu bytes, dataTransRate: %.2f Mbits/s, fileTransRate: %.2f Mbits/s\n", time_cost, numDataTrans, fileSize, (double)numDataTrans/(time_cost*MBITS), (double)fileSize/(time_cost*MBITS));
            fclose(log);

            seqNum = 0;
            memset(curPeer, '\0', sizeof curPeer);
            if(writeFile(dest_file, storage)!=0){
              fprintf(stderr, "File written failed\n");
              return 1;
            }
            storage = freeStorage(storage);
            storageSize = 0;
            numDataTrans = 0;
            printf("file written successfully\n");
          }

        }else if(recvPacket->seqNum>seqNum){
          for(int i=0; i<recvPacket->seqNum-seqNum; i++){
            if(window[i] == 0){
              if((nbytes = sendPacket(NAK, 0, seqNum+i, numPacket, NULL, (struct sockaddr*)&their_addr, addr_len, sockfd, storage))==0){
                fprintf(stderr, "Packet NAK sending error.\n");
                return 1;
              }
            }
          }
        }
      }
      free(recvPacket);
    }else if(storage != NULL){
      //printf("timeout\n");
      int size = WINDOW_SIZE<numSlotsToFill?WINDOW_SIZE:numSlotsToFill;
      for(int i=0; i<size; i++){
        if(window[i] == 0){

          if((nbytes = sendPacket(NAK, 0, seqNum+i, numPacket, NULL, (struct sockaddr*)&their_addr, addr_len, sockfd, storage))==0){
              fprintf(stderr, "Packet NAK sending error.\n");
              return 1;
          }
        }
      }
    }
  }
  return 0;
}
