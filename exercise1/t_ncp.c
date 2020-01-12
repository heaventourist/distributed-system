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
unsigned long numPacket = 0;

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
        exit(1);
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

char** freeStorage(char **storage){
    for(int i=0; i<storageSize; i++){
        free(storage[i]);
    }
    free(storage);
    storageSize = INIT_STORAGE_SIZE;
    return NULL;
}

void *get_in_addr(struct sockaddr *sa){
    if(sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char **argv)
{
    int rate;
    char * src_file;
    char * dest_file;
    char * remote_host;
    char s[INET6_ADDRSTRLEN];

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int nbytes;
    char** storage;

    clock_t start;


    if(argc != 4) {
        fprintf(stderr, "Usage: ./ncp <loss_rate_percent> <source_file_name> <dest_file_name>@<comp_name>\n");
        exit(0);
    }

    rate = atoi(argv[1]);
    src_file = (char *)argv[2];
    dest_file = strtok(argv[3], "@");
    remote_host = strtok(NULL, "@");

    printf("rate=%d , src=%s, dest=%s, remote=%s \n", rate, src_file, dest_file, remote_host);

    /* Call this once to initialize the coat routine */
    // sendto_dbg_init(rate);
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if((rv=getaddrinfo(remote_host, SERVERPORT, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and make a socket
    for(p=servinfo; p!=NULL; p=p->ai_next){
        struct sockaddr_in* saddr = (struct sockaddr_in*)p->ai_addr;
        printf("hostname: %s\n", inet_ntoa(saddr->sin_addr));
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            perror("ncp: socket");
            continue;
        }

        if(connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) == -1){
            perror("ncp: connect");
            continue;
        }
        break;
    }

    if(p==NULL){
        fprintf(stderr, "ncp: failed to create socket\n");
        return 2;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr*)p->ai_addr), s, sizeof s);
    printf("client: connecting to %s\n", s);
    freeaddrinfo(servinfo);


    // readfile and process
    storage = readFile(src_file);
    numPacket = fileSize/SLOT_SIZE + (fileSize%SLOT_SIZE>0?1:0);

    start = clock();
    unsigned long originSize = fileSize;
    for(unsigned long i = 0; i<numPacket; i++){
        int dataSize = fileSize<SLOT_SIZE?fileSize:SLOT_SIZE;
        int packetSize = sizeof(struct Packet);

        struct Packet* packet = (struct Packet*)calloc(1, packetSize);
        packet->numPacket = numPacket;
        packet->dataSize = dataSize;
        memset(packet->filename, 0, sizeof packet->filename);
        memcpy(packet->filename, dest_file, strlen(dest_file));
        memcpy(packet->data, storage[i], dataSize);
        struct Packet* ptr = packet;
        while(packetSize > 0){
            nbytes = send(sockfd, packet, packetSize, 0);
            if(nbytes == -1){
                perror("ncp: send to error");
                exit(1);
            }
            packetSize -= nbytes;
            ptr += nbytes;
        }
        free(packet);
        fileSize -= dataSize;
    } 
    double time_cost = (double)(clock()-start)/CLOCKS_PER_SEC;
    printf("time_cost: %.2f seconds, filesize: %lu bytes, tranRate: %.2f Mbits/s\n", time_cost, originSize, (double)originSize/(time_cost*MBITS));
    free(storage);
    return 0;
}
