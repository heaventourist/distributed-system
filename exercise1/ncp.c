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
unsigned long storageSize = INIT_STORAGE_SIZE;
unsigned long numPacket = 0;

int main(int argc, char **argv)
{
    int rate;
    char * src_file;
    char *dest_file;
    char * remote_host;
    fd_set master;
    fd_set read_fds;
    int fdmax;

    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int nbytes;
    char buf[BUF_SIZE] = {0};
    unsigned long seqNum = 0;
    int cnt;
    unsigned long numDataTrans = 0;
    char** storage = NULL;

    enum Status curStatus = WAIT;
    FILE* log;
    unsigned long logInterval = INTERVAL; // 100MB

    struct timeval tv;

    tv.tv_sec = 2;
    tv.tv_usec = 0;

    clock_t start;
    clock_t checkpoint_time;
    unsigned long checkpoint_data=0, checkpoint_file=0;


    if(argc != 4) {
        fprintf(stderr, "Usage: ./ncp <loss_rate_percent> <source_file_name> <dest_file_name>@<comp_name>\n");
        exit(0);
    }

    if((log = fopen("ncp.log", "a+")) == NULL){
        perror("ncp: failed to read the file");
        exit(1);
    }

    rate = atoi(argv[1]);
    src_file = (char *)argv[2];
    dest_file = strtok(argv[3], "@");
    remote_host = strtok(NULL, "@");

    printf("rate=%d, src=%s, dest=%s, remote=%s \n", rate, src_file, dest_file, remote_host);

    /* Call this once to initialize the coat routine */
    sendto_dbg_init(rate);
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if((rv=getaddrinfo(remote_host, SERVERPORT, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and make a socket
    for(p=servinfo; p!=NULL; p=p->ai_next){
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            perror("ncp: socket");
            continue;
        }
        break;
    }

    if(p==NULL){
        fprintf(stderr, "ncp: failed to create socket\n");
        return 2;
    }

    // readfile and process
    if((storage = readFile(src_file)) == NULL){
        // fprintf(stderr, "File reading failed\n");
        return 1;
    }
    FD_SET(sockfd, &master);
    fdmax = sockfd;
    numPacket = fileSize/SLOT_SIZE + (fileSize%SLOT_SIZE>0?1:0);

    if((nbytes = sendPacket(SYN, 0, 0, numPacket, dest_file, p->ai_addr, p->ai_addrlen, sockfd, storage))==0){
        fprintf(stderr, "Packet SEND1 sending error.\n");
        return 1;
    }
    
    numDataTrans+=nbytes;
    checkpoint_time = start = clock();
    

    for(;;){
        read_fds = master;
        if(select(fdmax+1, &read_fds, NULL, NULL, &tv) == -1){
            perror("select error");
            exit(1);
        }
        // send packet
        if(FD_ISSET(sockfd, &read_fds)){           
            if((nbytes = recv(sockfd, buf, sizeof buf, 0)) < 0){  
                perror("recv");
                close(sockfd);
                FD_CLR(sockfd, &master);
                exit(1);
            }

            if(numDataTrans>0 && numDataTrans>logInterval){
                clock_t cur_time = clock();
                double duration = (double)(cur_time - checkpoint_time)/CLOCKS_PER_SEC;
                checkpoint_time = cur_time;
                unsigned long trans_data = numDataTrans - checkpoint_data;
                checkpoint_data = numDataTrans;
                unsigned long trans_file = seqNum*SLOT_SIZE - checkpoint_file;
                checkpoint_file = seqNum*SLOT_SIZE;
                logInterval += INTERVAL;
                fprintf(log, "numDataTrans: %lu bytes, filesize_sent: %lu bytes, transfer_rate: %.2f Mbits/s, file_transferrate: %.2f Mbits/s\n", numDataTrans, seqNum*SLOT_SIZE, (double)trans_data/(duration*MBITS), (double)trans_file/(duration*MBITS));
            }
            struct Packet recvPacket;
            memcpy(&recvPacket, buf, sizeof recvPacket);
            
            if(recvPacket.status == WAIT){
                printf("server is busy\n");
                tv.tv_sec *= 2;
            }else if(recvPacket.status == SYNACK && curStatus==WAIT){
                // printf("SYNACK\n");
                cnt = 0;
                curStatus = SYNACK;
                tv.tv_sec = 2;
                unsigned long tmpSeq = seqNum;
                while(cnt < WINDOW_SIZE && tmpSeq < numPacket){
                    unsigned short dataSize = tmpSeq==numPacket-1 && fileSize%SLOT_SIZE>0?fileSize%SLOT_SIZE:SLOT_SIZE;

                    if((nbytes = sendPacket(SEND, dataSize, tmpSeq, numPacket, dest_file, p->ai_addr, p->ai_addrlen, sockfd, storage))==0){
                        fprintf(stderr, "Packet SEND1 sending error.\n");
                        return 1;
                    }
                    numDataTrans+=nbytes;

                    cnt++;
                    tmpSeq++;
                }
            }else if(recvPacket.status == ACK){
                // printf("ACK seq:%lu", seqNum);
                curStatus = ACK;
                if(seqNum >= recvPacket.seqNum){
                    continue;
                }
                seqNum = recvPacket.seqNum;
                // printf("seqNum: %lu, numPacket: %lu\n", seqNum, numPacket);
                if(seqNum == numPacket){
                    printf("transmission complete\n");
                    clock_t cur_time = clock();
                    double time_cost = (double)(cur_time - start)/CLOCKS_PER_SEC;
                    unsigned long final_size = seqNum*SLOT_SIZE>fileSize?fileSize:seqNum*SLOT_SIZE;
                    checkpoint_time = 0;
                    checkpoint_data = 0;
                    checkpoint_file = 0;
                    fprintf(log, "transmission time cost: %.2f seconds, dataTrans: %lu bytes, fileTrans: %lu bytes, dataTransRate: %.2f Mbits/s, fileTransRate: %.2fMbits/s\n", time_cost, numDataTrans, final_size, (double)numDataTrans/(time_cost*MBITS), (double)final_size/(time_cost*MBITS));
                    fclose(log);
                    logInterval = INTERVAL;
                    freeaddrinfo(servinfo);
                    close(sockfd);
                    FD_CLR(sockfd, &master);
                    freeStorage(storage);
                    return 0;
                }                         
                cnt = 0;
                unsigned long tmpSeq = seqNum;
                while(cnt < WINDOW_SIZE && tmpSeq < numPacket){
                    unsigned short dataSize = tmpSeq==numPacket-1 && fileSize%SLOT_SIZE>0?fileSize%SLOT_SIZE:SLOT_SIZE;
                    
                    if((nbytes = sendPacket(SEND, dataSize, tmpSeq, numPacket, dest_file, p->ai_addr, p->ai_addrlen, sockfd, storage))==0){
                        fprintf(stderr, "Packet SEND2 sending error.\n");
                        return 1;
                    }
                    numDataTrans+=nbytes;
                    cnt++;
                    tmpSeq++;
                }
            }else if(recvPacket.status == NAK){
                // printf("NAK seq:%lu", seqNum);
                unsigned short dataSize = recvPacket.seqNum==numPacket-1 && fileSize%SLOT_SIZE>0?fileSize%SLOT_SIZE:SLOT_SIZE;

                if((nbytes = sendPacket(SEND, dataSize, recvPacket.seqNum, numPacket, dest_file, p->ai_addr, p->ai_addrlen, sockfd, storage))==0){
                    fprintf(stderr, "Packet SEND3 sending error.\n");
                    return 1;
                }
                numDataTrans+=nbytes;
            }
        }else if(curStatus == WAIT){
            printf("timeout\n");

            if((nbytes = sendPacket(SYN, 0, 0, numPacket, dest_file, p->ai_addr, p->ai_addrlen, sockfd, storage))==0){
                fprintf(stderr, "Packet SYN sending error.\n");
                return 1;
            }
            numDataTrans+=nbytes;

            tv.tv_sec *= 2;
        }else{
            cnt = 0;
            unsigned long tmpSeq = seqNum;
            while(cnt < WINDOW_SIZE && tmpSeq < numPacket){
                unsigned short dataSize = tmpSeq==numPacket-1 && fileSize%SLOT_SIZE>0?fileSize%SLOT_SIZE:SLOT_SIZE;

                if((nbytes = sendPacket(SEND, dataSize, tmpSeq, numPacket, dest_file, p->ai_addr, p->ai_addrlen, sockfd, storage))==0){
                    fprintf(stderr, "Packet SEND4 sending error.\n");
                    return 1;
                }
                numDataTrans+=nbytes;

                cnt++;
                tmpSeq++;
            }
        }
    }
    return 0;
}
