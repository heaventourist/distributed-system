#ifndef _UTIL_H_
#define _UTIL_H_

#define SERVERPORT "10250"
#define BUF_SIZE 2000
#define INIT_STORAGE_SIZE 10
#define SLOT_SIZE 1000
#define WINDOW_SIZE 100
#define INTERVAL 100*1024*1024 // 100MB
#define MBITS 1024*1024/8

enum Status{
    SYN, SYNACK, SEND, ACK, NAK, WAIT
};

enum WindowStatus{
  EMPTY, NFULL, FULL
};

struct Packet{
    unsigned long numPacket;
    unsigned short dataSize;
    enum Status status;
    unsigned long seqNum;
    char data[SLOT_SIZE];
    char filename[20];
};

unsigned long fileSize;
unsigned long storageSize;
unsigned long numPacket;
unsigned long numSlotsToFill;

void *get_in_addr(struct sockaddr *sa);
int writeFile(char* filename, char** storage);
char** readFile(char *filename);
char ** resize(char ** storage);
char** freeStorage(char **storage);
enum WindowStatus checkWindow(int window[WINDOW_SIZE]);
int sendPacket(enum Status status, unsigned short dataSize, unsigned long seqNum, unsigned long numPacket, char *dest_file, struct sockaddr* addr, socklen_t addr_len, int sockfd, char **storage);

#endif
