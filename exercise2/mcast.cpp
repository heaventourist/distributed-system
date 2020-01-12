#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h> 
#include <netdb.h>
#include <errno.h>
#include <time.h>
#include <string>
#include <unordered_set>
extern "C" {
    #include "recv_dbg.h"
}

#define PORT	     10250
#define MAX_MESS_LEN 1400
#define MAX_BUFF_SIZE 2000
#define WINDOW_SIZE 20

// Packet fields
struct Packet{
    int machineIndex;
    int packetIndex;
    int randomNumber;
    int valid;
    char payload[MAX_MESS_LEN];
};

// Token fields
struct Token{
    int next;
    int aru;
    int rtr[11][WINDOW_SIZE];
    int seq;
    int round;
    int tokenRound;
};



int main(int argc, char **argv)
{
    if(argc!=5){
        fprintf(stderr, "Usage: ./mcast <num_of_packets> <machine_index> <number of machine> <loss_rate_percent>\n");
        exit(0);
    }
    struct sockaddr_in name;
    struct sockaddr_in send_addr;

    int                mcast_addr;

    struct ip_mreq     mreq;
    unsigned char      ttl_val;

    int                ss,sr;
    fd_set             mask;
    fd_set             temp_mask;
    int                bytes;
    char               mess_buf[MAX_BUFF_SIZE];
    int                numOfPackets;
    int                machineIndex;
    int                numberOfMachine;
    int                lossRate;
    int                packetIndex;
    int                status; //0 denotes not started, 1 denotes started
    FILE*              pFile;
    // FILE*              log;
    struct timeval     tv;
    std::string        filename;
    Packet*            recv_buf[11][WINDOW_SIZE];
    Token              prev;
    Packet             resend_buf[WINDOW_SIZE];
    int                aru;
    int                round;
    int                tokenRound;
    clock_t            start;
    std::unordered_set<int> finishedMachine;

    for(int i=0; i<10; i++){
        for(int j=0; j<WINDOW_SIZE; j++){
            recv_buf[i][j] = NULL;
        }
    }

    numOfPackets = atoi(argv[1]);
    machineIndex = atoi(argv[2]);
    numberOfMachine = atoi(argv[3]);
    lossRate = atoi(argv[4]);

    filename = std::to_string(machineIndex)+".out";
    pFile = fopen(filename.c_str(), "w");

    // a lock variable to prevent machines from receiving 
    // duplicate tokens when the token is missing and retransmitted.
    tokenRound = 0;

    // keeping the number of packets sent from this machine
    packetIndex = 0;

    // 0 denotes waiting for start, 1 denotes in transmitting
    status = 0; 

    // local aru, the next expected packet index to be delivered
    aru = 0; 

    // a variable working with token.round to differentiate circulating stages 
    round = 0;

    tv.tv_sec = 0;
    tv.tv_usec = 10;

    mcast_addr = 225 << 24 | 1 << 16 | 2 << 8 | 50; /* (225.1.2.50) */

    recv_dbg_init(lossRate, machineIndex);
    sr = socket(AF_INET, SOCK_DGRAM, 0); /* socket for receiving */
    if (sr<0) {
        perror("Mcast: socket");
        exit(1);
    }

    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;
    name.sin_port = htons(PORT);

    if ( bind( sr, (struct sockaddr *)&name, sizeof(name) ) < 0 ) {
        perror("Mcast: bind");
        exit(1);
    }

    mreq.imr_multiaddr.s_addr = htonl( mcast_addr );

    /* the interface could be changed to a specific interface if needed */
    mreq.imr_interface.s_addr = htonl( INADDR_ANY );

    if (setsockopt(sr, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&mreq, 
        sizeof(mreq)) < 0) 
    {
        perror("Mcast: problem in setsockopt to join multicast address" );
    }

    ss = socket(AF_INET, SOCK_DGRAM, 0); /* Socket for sending */
    if (ss<0) {
        perror("Mcast: socket");
        exit(1);
    }

    ttl_val = 1;
    if (setsockopt(ss, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl_val, 
        sizeof(ttl_val)) < 0) 
    {
        printf("Mcast: problem in setsockopt of multicast ttl %d - ignore in WinNT or Win95\n", ttl_val );
    }

    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(mcast_addr);  /* mcast address */
    send_addr.sin_port = htons(PORT);

    FD_SET( sr, &mask );
    for(;;)
    {
        temp_mask = mask;
        if(select( sr+1, &temp_mask, 0, 0, &tv) == -1){
            perror("select error");
            close(sr);
            close(ss);
            exit(1);
        }

        if ( FD_ISSET( sr, &temp_mask) ) {
            

            // simply for recording the transmitting time cost. feel free to comment out
            if((int)finishedMachine.size() == numberOfMachine){
                clock_t end = clock();
                double duration = (double)(end - start)/CLOCKS_PER_SEC;
                printf("time cost %f s\n", duration);
                printf("transmission completed\n");
                exit(0);
            }

            // waiting for start_mcast to send token
            if(status == 0){
                start = clock();
                if((bytes = recv( sr, mess_buf, sizeof(mess_buf), 0 ))<0){
                    perror("recvfrom\n");
                    exit(1);
                }

                Token token;
                memcpy(&token, mess_buf, sizeof token);

                if(token.next == machineIndex){
                    round++;
                    tokenRound++;
                    for(int i=0; i<WINDOW_SIZE; i++){
                        Packet sendPacket;
                        sendPacket.machineIndex = machineIndex;
                        sendPacket.packetIndex = packetIndex++;
                        sendPacket.randomNumber = rand()%1000000+1;
                        if(sendPacket.packetIndex >= numOfPackets){
                            sendPacket.valid = 0;
                        }else{
                            sendPacket.valid = 1;
                        }

                        // keep all the packets sent out within the most recent window size
                        resend_buf[sendPacket.packetIndex%WINDOW_SIZE] = sendPacket;
                        sendto( ss, (char*)&sendPacket, sizeof(sendPacket), 0, 
                            (struct sockaddr *)&send_addr, sizeof(send_addr) );
                    }

                    // denote the next machine to receive the token
                    if(token.next == numberOfMachine){
                        token.next = 1;
                    }else{
                        token.next++;
                    }

                    // always update seq and aru when sending out new packets 
                    token.seq = packetIndex;
                    token.aru = token.seq;

                    // unlocked right now
                    token.tokenRound = 0;
                    
                    // for resend
                    prev = token;
                    sendto( ss, (char*)&token, sizeof(token), 0, 
                        (struct sockaddr *)&send_addr, sizeof(send_addr) );
                }
                status = 1;

            }
            // transmitting
            else{
                if((bytes = recv_dbg( sr, mess_buf, sizeof(mess_buf), 0 ))<0){
                    perror("recvfrom\n");
                    exit(1);
                }
                if(bytes==0){
                    continue;
                }

                // if token is received
                if(bytes == sizeof(Token)){
                    Token token;
                    memcpy(&token, mess_buf, sizeof token);
                    
                    // only the next machine needs to process the token
                    if(token.next == machineIndex){

                        // if has received the token in this round, ignore it
                        if(token.tokenRound!=tokenRound){
                            continue;
                        }
                        // increment only when the first time the token is processed in this round
                        tokenRound++;
                        // printf("token1 tokenaru=%d, aru=%d, tokenseq=%d\n", token.aru, aru, token.seq);
                        
                        // 2nd stage: start collecting information on aru
                        if(token.round == round){
                            if(token.aru>aru){
                                token.aru = aru;
                            }
                            if(machineIndex == numberOfMachine){
                                token.round++;
                            }

                            // keep the packets indices for retransmitting
                            int i = aru%WINDOW_SIZE;
                            for(; i<WINDOW_SIZE; i++){
                                for(int j=1; j<=numberOfMachine; j++){
                                    if(recv_buf[j][i] == NULL){
                                        token.rtr[j][i] = 1;
                                    }
                                }
                            }
                        }
                        // 1st stage: send out packets or retransmit missing packets
                        else{
                            // printf("token2 tokenaru=%d, aru=%d, tokenseq=%d, packetIndex=%d\n", token.aru, aru, token.seq, packetIndex);
                            round++;

                            // this is the case when all the previous packets have been delived successfully
                            if(token.aru == token.seq){

                                for(int i=0; i<WINDOW_SIZE; i++){
                                    Packet sendPacket;
                                    sendPacket.machineIndex = machineIndex;
                                    sendPacket.packetIndex = packetIndex++;
                                    sendPacket.randomNumber = rand()%1000000+1;
                                    if(sendPacket.packetIndex >= numOfPackets){
                                        sendPacket.valid = 0;
                                    }else{
                                        sendPacket.valid = 1;
                                    }

                                    resend_buf[sendPacket.packetIndex%WINDOW_SIZE] = sendPacket;
                                    sendto( ss, (char*)&sendPacket, sizeof(sendPacket), 0, 
                                        (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                    // printf("send machineIndex=%d, packetIndex=%d\n", sendPacket.machineIndex, sendPacket.packetIndex);
                                }

                                token.seq = packetIndex;
                                token.aru = token.seq;

                                // initialize
                                for(int i=0; i<WINDOW_SIZE; i++){
                                    for(int j=1; j<=numberOfMachine; j++){
                                        token.rtr[j][i] = 0;  
                                    }
                                }
                            }
                            // this is the case when missing packets need to be retransmitted
                            else{ 
                                int i=0;
                                for(; i<WINDOW_SIZE; i++){
                                    if(token.rtr[machineIndex][i] == 1){
                                        // printf("missing machineIndex=%d, index=%d\n", machineIndex, i);
                                        sendto( ss, (char*)&(resend_buf[i]), sizeof(Packet), 0, 
                                            (struct sockaddr *)&send_addr, sizeof(send_addr) );
                                    }
                                }
                                
                                // reset the aru for decrement in the next stage
                                if(machineIndex == numberOfMachine){
                                    token.aru = token.seq;
                                }
                            }
                        }

                        // start the next round
                        if(machineIndex == numberOfMachine){
                            token.tokenRound++;
                        }
                        if(token.next == numberOfMachine){
                            token.next = 1;
                        }else{
                            token.next++;
                        }
                        prev = token;
                        sendto( ss, (char*)&token, sizeof(token), 0, 
                            (struct sockaddr *)&send_addr, sizeof(send_addr) );
                    }
                }
                // when receiving packets
                else{
                    Packet* recvPacket = (Packet*)calloc(1, sizeof(Packet));
                    memcpy(recvPacket, mess_buf, sizeof(Packet));

                    // the packets before aru are already delivered, ignore it
                    if(recvPacket->packetIndex<aru || recv_buf[recvPacket->machineIndex][(recvPacket->packetIndex)%WINDOW_SIZE] != NULL){
                        continue;
                    }

                    // printf("packetIndex=%d, machineIndex=%d\n", recvPacket->packetIndex, recvPacket->machineIndex);
                    recv_buf[recvPacket->machineIndex][(recvPacket->packetIndex)%WINDOW_SIZE] = recvPacket;

                    int index = aru%WINDOW_SIZE;

                    // check if we can deliver packets
                    while(index<WINDOW_SIZE){
                        int found = 0;
                        for(int j=1; j<=numberOfMachine; j++){
                            if(recv_buf[j][index] == NULL){
                                found = 1;
                                break;
                            }
                        }
                        //if not, break
                        if(found){
                            break;
                        }
                        // deliver packets
                        for(int j=1; j<=numberOfMachine; j++){
                            Packet* tmp_packet = recv_buf[j][index];
                            recv_buf[j][index] = NULL;
                            if(tmp_packet->valid){
                                fprintf(pFile, "%2d, %8d, %8d\n", tmp_packet->machineIndex, tmp_packet->packetIndex, tmp_packet->randomNumber);
                            }else{
                                finishedMachine.insert(j);
                            }                         
                            free(tmp_packet);
                        }
                        index++;
                        aru++;
                    }    
                }
            }
        }
        else{

            // resend token
            if(status == 1){
                sendto( ss, (char*)&prev, sizeof(prev), 0, 
                    (struct sockaddr *)&send_addr, sizeof(send_addr) );
            }
        }
        tv.tv_sec = 0;
        tv.tv_usec = 10;
    }

    return 0;
}
