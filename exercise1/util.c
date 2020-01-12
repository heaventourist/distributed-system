#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sendto_dbg.h"
#include "util.h"


extern unsigned long fileSize;
extern unsigned long storageSize;
extern unsigned long numSlotsToFill;

void *get_in_addr(struct sockaddr *sa){
    if(sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int writeFile(char* filename, char** storage){

    if(remove(filename) == 0){
      printf("The origin file %s is deleted\n", filename);
    }

    FILE *fp = NULL;
    if((fp = fopen(filename, "a+")) == NULL){
        perror("ncp: failed to write to the file");
        return 1;
    }
    int i=0, rv=0;
    while(fileSize>0){
      rv = fwrite(storage[i++], sizeof(char), fileSize>SLOT_SIZE?SLOT_SIZE:fileSize, fp);
      fileSize -= rv;
    }
    fclose(fp);
    return 0;
}

char** readFile(char *filename){
    FILE *fp = NULL;
    char** storage = (char**)calloc(storageSize, sizeof(char*));
    char buff[SLOT_SIZE] = "";
    int i;
    for(i=0; i<storageSize; i++){
        storage[i] = (char*)calloc(SLOT_SIZE, sizeof(char));
    }
    
    if((fp = fopen(filename, "r")) == NULL){
        perror("ncp: failed to read the file");
        freeStorage(storage);
        return NULL;
    }
    i=0;
    int rv=0;
    while((rv=fread(buff, sizeof(char), SLOT_SIZE, fp)) > 0){
        if(i==storageSize){
            storage = resize(storage);
        }
        fileSize += rv;
        memcpy(storage[i++], buff, rv);
    }
    
    fclose(fp);
    return storage;
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

enum WindowStatus checkWindow(int window[WINDOW_SIZE]){
  enum WindowStatus res;
  int cnt = 0;
  int size = WINDOW_SIZE<numSlotsToFill?WINDOW_SIZE:numSlotsToFill;
  for(int i=0; i<size; i++){
    if(window[i]!=0){
      cnt++;
    }
  }
  res = cnt==0?EMPTY:(cnt<size?NFULL:FULL);
  return res;
}

int sendPacket(enum Status status, unsigned short dataSize, unsigned long seqNum, unsigned long numPacket, char *dest_file, struct sockaddr* addr, socklen_t addr_len, int sockfd, char **storage){

    int packetSize;
    int ret;
    int nbytes;
    struct Packet* packet = (struct Packet*)calloc(1, sizeof(struct Packet));
    packet->numPacket = numPacket;
    packet->dataSize = dataSize;
    packet->status = status;
    packet->seqNum = seqNum;
    if(dataSize>0){
        memcpy(packet->data, *(storage+seqNum), dataSize);
    }
    if(dest_file!=NULL){
        memset(packet->filename, 0, sizeof packet->filename);
        memcpy(packet->filename, dest_file, strlen(dest_file));
    }
    
    ret = packetSize = sizeof(struct Packet);
    struct Packet* ptr = packet;
    while(packetSize > 0){
        nbytes = sendto_dbg(sockfd, (char*)packet, packetSize, 0, addr, addr_len);
        if(nbytes == -1){
            perror("send to error");
            return 0;
        }
        packetSize -= nbytes;
        ptr += nbytes;
    }
    free(packet);
    return ret;
}
