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
// #include "sendto_dbg.h"

#define SERVERPORT "10250"
#define SLOT_SIZE 1000
#define BUF_SIZE 20000
#define BACK_BUF_SIZE 2000
#define INIT_STORAGE_SIZE 10
#define MBITS 1024*1024/8

struct Packet{
    unsigned long numPacket;
    unsigned short dataSize;
    char data[SLOT_SIZE];
    char filename[20];
};

unsigned long fileSize = 0;
unsigned long storageSize = INIT_STORAGE_SIZE;

void *get_in_addr(struct sockaddr *sa){
    if(sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void writeFile(char* filename, char** storage){

    if(remove(filename) == 0){
      printf("The origin file %s is deleted\n", filename);
    }

    FILE *fp = NULL;
    if((fp = fopen(filename, "a+")) == NULL){
        perror("ncp: failed to write to the file");
        exit(1);
    }
    int i=0, rv=0;
    while(fileSize>0){
      rv = fwrite(storage[i++], sizeof(char), fileSize>SLOT_SIZE?SLOT_SIZE:fileSize, fp);
      fileSize -= rv;
    }
    fclose(fp);
}

char ** resize(char ** storage){
    char **newStorage = (char**)calloc(2*storageSize, sizeof(char*));
    int i;
    for(i=0; i<storageSize; i++){
        newStorage[i] = storage[i];
    }
    for(; i<2*storageSize; i++){
        newStorage[i] = (char*)calloc(SLOT_SIZE, sizeof(char));
    }
    storageSize *= 2;
    free(storage);
    return newStorage;
}

char** freeStorage(char **storage){
    for(int i=0; i<storageSize; i++){
        free(storage[i]);
    }
    free(storage);
    return NULL;
}

int main(int argc, char **argv)
{
  int rate;
  char dest_file[20];
  fd_set master;
  fd_set read_fds;
  int fdmax;
  struct sockaddr_storage their_addr;
  socklen_t addr_len;
  char s[INET6_ADDRSTRLEN] = {'\0'};

  int sockfd, newfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  int nbytes;
  char buf[BUF_SIZE];
  char backBuf[BACK_BUF_SIZE];
  int backSize = 0;
  char *bufptr = buf;

  int numPacket;
  char** storage = NULL;
  int idxSize = 100;
  unsigned long *idx = (unsigned long *)calloc(idxSize, sizeof(unsigned long));
  int i;

  clock_t start; 

  if(argc != 2) {
      fprintf(stderr, "Usage: ./ncp <loss_rate_percent>\n");
      exit(0);
  }

  rate = atoi(argv[1]);

  printf("rate=%d\n", rate);

  /* Call this once to initialize the coat routine */
  // sendto_dbg_init(rate);
  FD_ZERO(&master);
  FD_ZERO(&read_fds);

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
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

  if(listen(sockfd, 10) == -1){
      perror("listen");
      exit(1);
  }

  printf("server: waiting for connection...\n");

  // readfile and process
  FD_SET(sockfd, &master);
  fdmax = sockfd;
  for(;;){
    read_fds = master;
    if(select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1){
        perror("select error");
        free(idx);
        exit(1);
    }

    for(i=0; i<=fdmax; i++){
        if(FD_ISSET(i, &read_fds)){
            if(i == sockfd){
                addr_len = sizeof their_addr;
                newfd = accept(sockfd, (struct sockaddr*)&their_addr, &addr_len);

                if(newfd == -1){
                    perror("accept");
                }else{
                    FD_SET(newfd, &master);
                    if(newfd > fdmax){
                        fdmax = newfd;
                    }
                    if(newfd>=idxSize){
                        idxSize = newfd+1;
                        idx = (unsigned long *)realloc(idx, idxSize);
                    }
                    idx[newfd] = 0;
                    printf("t_rcv: new connection from %s on socket %d\n", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr*)&their_addr), s, sizeof s), newfd);
                }
            }else{
                if((nbytes = recv(i, buf, sizeof buf, 0)) <= 0){
                    if(nbytes == 0){
                        printf("t_rcv: socket %d hung up\n", i);
                    }else{
                        perror("recv\n");
                    }
                    close(i);
                    FD_CLR(i, &master);         
                }else{
                    int size = sizeof(struct Packet);
                    struct Packet* recvPacket = (struct Packet*)calloc(1, size);

                    if(backSize>0 && nbytes+backSize>=size){
                        memcpy(recvPacket, backBuf, backSize);
                        memcpy((char*)recvPacket+backSize, bufptr, size-backSize);
                        
                        if(SLOT_SIZE != recvPacket->dataSize){
                            return 1;
                        }
                        if(storage == NULL){
                            start = clock();
                            storageSize = numPacket = recvPacket->numPacket;
                            storage = (char **)calloc(storageSize, sizeof(char*));
                            for(unsigned long j=0; j<storageSize; j++){
                                storage[j] = (char*)calloc(SLOT_SIZE, sizeof(char));
                            }
                            memcpy(dest_file, recvPacket->filename, strlen(recvPacket->filename));
                        }
                        
                        memcpy(storage[idx[i]++], recvPacket->data, recvPacket->dataSize);
                        fileSize += recvPacket->dataSize;
                        if(idx[i] == numPacket){
                            double time_cost = (double)(clock()-start)/CLOCKS_PER_SEC;
                            printf("time_cost: %.2f, filesize: %lu, tranRate: %.2f\n", time_cost, fileSize, (double)fileSize/(time_cost*MBITS));
                            writeFile(dest_file, storage);
                            storage = freeStorage(storage);
                            storageSize = 0;
                            printf("file written successfully\n");
                            close(i);
                            FD_CLR(i, &master);
                        }

                        nbytes -= size-backSize;
                        bufptr += size-backSize;
                        backSize = 0;
                    }
                        

                    while(nbytes>=size){
                        memcpy(recvPacket, bufptr, size);

                        if(storage == NULL){
                            start = clock();
                            storageSize = numPacket = recvPacket->numPacket;
                            storage = (char **)calloc(storageSize, sizeof(char*));
                            for(unsigned long j=0; j<storageSize; j++){
                                storage[j] = (char*)calloc(SLOT_SIZE, sizeof(char));
                            }
                            memcpy(dest_file, recvPacket->filename, strlen(recvPacket->filename));
                        }
                        
                        memcpy(storage[idx[i]++], recvPacket->data, recvPacket->dataSize);
                        fileSize += recvPacket->dataSize;
                        if(idx[i] == numPacket){
                            double time_cost = (double)(clock()-start)/CLOCKS_PER_SEC;
                            printf("time_cost: %.2f seconds, filesize: %lu bytes, tranRate: %.2f Mbits\n", time_cost, fileSize, (double)fileSize/(time_cost*MBITS));
                            printf("%s\n", dest_file);
                            writeFile(dest_file, storage);
                            storage = freeStorage(storage);
                            storageSize = 0;
                            printf("file written successfully\n");
                            close(i);
                            FD_CLR(i, &master);
                        }

                        nbytes -= size;
                        bufptr += size;
                    }
                    if(nbytes>0 && backSize+nbytes<size){
                        memcpy(backBuf+backSize, bufptr, nbytes);
                        backSize += nbytes;
                    }
                    bufptr = buf;
                    free(recvPacket);
                }
            }
        }
    }
  }
  return 0;
}
