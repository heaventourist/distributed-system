#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <regex.h>
#include <unistd.h>

#include "sp.h"

#define MAX_MESSLEN 102400
#define MAX_VSSETS 10
#define MAX_MEMBERS 100
#define SERVERS_GROUP "servers"
#define MAX_LINE 1024
#define MAX_CHAT_ROOM 50
#define MAX_PARTICIPANTS 20
#define MAX_MESSAGE_SIZE 2000
#define MAX_LAMPORT 10000000000
#define SPREAD_PORT "4817"


// Packet type
enum Type{
    JOIN, LIKE, UNLIKE, REGULAR, HISTORY, NETWORK, PARTICIPANT
};

// UserNamePacket type, when new user came, its name is added, when user left, delete from the current server
enum UserNameType{
    ADD, DELETE
};


// The user might change its username, server connected, chat room he is currently in, the server need to make sure
// everything goes well on the server side such that the client can proceed with the change. Normal means no change.
enum ChangeType{
    USERNAME, SERVER, CHATROOM, NORMAL
};

// Packet for communication between client and server
struct Packet{
    char chat_room[MAX_MESSAGE_SIZE];
    char user_name[MAX_MESSAGE_SIZE];
    char message[MAX_MESSAGE_SIZE];
    enum Type type;
    enum ChangeType ctype;
    int status;
};

// Packet used for message propagation among servers
struct PropPacket{
    enum Type type;
    char chat_room[MAX_MESSAGE_SIZE];
    char user_name[MAX_MESSAGE_SIZE];
    char message[MAX_MESSAGE_SIZE];
    char from_server[MAX_MESSAGE_SIZE];
    int from_server_id;
    unsigned long lamport;
};

// The participant attributes including the chat room he is in, the user name he is using, and the server he connected to.
struct Participant{
    char chat_room[MAX_MESSAGE_SIZE];
    char user_name[MAX_MESSAGE_SIZE];
    int server;
};

// To reconcile among servers when network partition happens
struct GroupPacket{
    unsigned long version_vector[5];
    struct Participant participants[MAX_PARTICIPANTS];
    int num_participants;
    int from_server_id;
};

// Used to add or delete user name from the server
struct UserNamePacket{
    char chat_room[50];
    char user_name[50];
    int from_server_id;
    enum UserNameType type;
};

// doubly linked list to represent the chat history in memory
struct Node{
    enum Type type;
    char stamp[MAX_MESSAGE_SIZE];
    char chat_room[MAX_MESSAGE_SIZE];
    char user_name[MAX_MESSAGE_SIZE];
    char message[MAX_MESSAGE_SIZE];
    char user_liked[MAX_MESSAGE_SIZE][MAX_MESSAGE_SIZE];
    struct Node *next;
    struct Node *prev;
};

/*
********************************************************************************
    the relevant info including the chat room names, head and tail of the linked list, 
    the head of the most recent 25 chat messages, the participants in each chat room.
    All under the same index.
*/
char chat_room_names[MAX_CHAT_ROOM][MAX_MESSAGE_SIZE];
struct Node* chat_rooms[MAX_CHAT_ROOM];
struct Node* chat_rooms_tail[MAX_CHAT_ROOM];
struct Node* top_nodes[MAX_CHAT_ROOM];
static char chat_room_participants[MAX_CHAT_ROOM][MAX_PARTICIPANTS][20];
/*
********************************************************************************
*/

// the participants connected to each server known to the local server
static struct Participant* server_to_participants[5][MAX_PARTICIPANTS];
static int server_to_num_participants[5];


static char	User[80];
static char Spread_name[80];

// the group filled only by the local server, used to communicate with the client connected to it
static char local_group[50];

// the group filled by the client and the serve it is connected to, used for indicating the joining 
// and leaving of the client. The group name is in the following format: user_name+"$"+server_{1,2,3,4,5]
char chat_group[MAX_CHAT_ROOM][50];

// indicate the lamport timestamp of the most recent chatting message received by 
// each server the client is connected to known to the local server. 
static unsigned long version_vector[5];

// the version vectors of all the servers in the network partition known to the local server
static unsigned long matrix[5][5];
// the server ids in the partition
static int group_members[5];
// indicate the servers that have sent the version vectors to the local server
static int vector_received[5];

/*
****************************************************
    to indicate the partition group ids sent from the servers, to avoid duplicate processing 
    of the same partition
*/
// static char senders[MAX_PARTICIPANTS][50];
// static char group_ids[MAX_PARTICIPANTS][50];
/*
****************************************************
*/

static char Private_group[MAX_GROUP_NAME];
static mailbox Mbox;


static int To_exit = 0;
// local server id
static int server_id;
// the file descriptors of the write logs corresponding to each server
static FILE* pFile[5];
static char file_name[5][50];

/*
******************************************************
    lamport timestamp manipulation
*/
static unsigned long counter = 1;


unsigned long incrementCounter(){
    version_vector[server_id-1] = counter;
    return counter++;
}

unsigned long currentCounter(){
    return counter-1;
}

void updateCounter(unsigned long cnt){
    counter = counter>cnt?counter:cnt;
    counter++;
}
/*
******************************************************
*/

// function prototypes
// process the regular Packet
static char* Process_packet(struct Packet packet);
// read the write logs to the memory from the files corresponding the the server id
static int Read_log(char *file, int id);
// clear the linked list in the memory
// static void Clear_chat_room();
// sort the linked list based on the lamport timestamp in the memory
static void Sort(int index);
static void	Bye();

static void Read_message();
// send the message string to the client after the processing of the Packet received
static void Send_message(struct Packet packet, char *receiver);
// propagate the packet received by the local server to other servers in the partition
static void Propagate_message(struct Packet packet, unsigned long lamport, int server_index);
// process the PropPacket
static void Process_prop_packet(struct PropPacket packet);
// send GroupPacket when network partition changes
static void Send_group_packet();
// process the GroupPacket
static void Process_group_packet(struct GroupPacket packet);
// after knowing which chat history is missing by the other servers, resend
static int Resend_packet(unsigned long start, int server_index);
// the original linked list formed by directly reading the write logs include like and unlike node,
// get rid of them after reformat
static void Reformat_list(int index);
// process UserNamePacket
static void Process_user_name_packet(struct UserNamePacket packet);
// propagate UserNamePacket to other servers
static void Propagate_user_name(char *chat_room, char *user_name, enum UserNameType type);
// remember the chat group denotes the group filled by the client and the server the client is connected to
static void Add_chat_group(struct Packet packet);
static void Remove_chat_group(char *user_name, char *server, char *chat_room);

// add participant to each chat room
static void Add_participant(struct Participant p);
// record the server id that the client joined
static void Add_participant_to_server(char *user_name, char *chat_room, int server_index);
// clear the record of the server<->participants mapping
static void Remove_all_participant_from_server(int server_index);
// remove a single participant from the server it is connected to
static void Remove_participant_from_server(char *user_name, char *chat_room, int server_index);
// look up for which server the participant joined
static int find_server_id(char *user_name, char *chat_room);
// check if there is duplicate user name in the chat room, duplicate is not allowed in a chatroom, bu allowed in different chat rooms
static int check_duplicate_name(char *usr_name, char *chat_room);

int main(int argc, char **argv){

    if(argc!=2){
        fprintf(stderr, "Usage: ./server <machine_index>\n");
        exit(0);
    }

    int ret, i;
    int mver, miver, pver;
    char mess[50];
    sp_time test_timeout;
	
    test_timeout.sec = 5;
    test_timeout.usec = 0;

    memset(chat_room_names, 0, MAX_CHAT_ROOM*MAX_MESSAGE_SIZE*sizeof(char));
    memset(vector_received, 0, 5*sizeof(int));
    memset(chat_room_participants, 0, MAX_CHAT_ROOM*MAX_PARTICIPANTS*20*sizeof(char));
    memset(server_to_num_participants, 0, 5*MAX_PARTICIPANTS*sizeof(int));
    // memset(senders, 0, sizeof(senders));
    // memset(group_ids, 0, sizeof(group_ids));

    server_id = atoi(argv[1]);
    strcpy(mess, "server_");
    strcat(mess, argv[1]);
    strcpy(local_group, mess);

    for(int i=0; i<5; i++){
        char tmp1[50];
        char tmp2[50];
        strcpy(tmp1, mess);
        strcat(tmp1, "#");
        sprintf(tmp2, "%d", i+1);
        strcat(tmp1, tmp2);
        strcat(tmp1, ".log");
        strcpy(file_name[i], tmp1);
        pFile[i] = fopen(tmp1, "a+");
    }

    

    // configure spread
    if (!SP_version( &mver, &miver, &pver)){
	    printf("main: Illegal variables passed to SP_version()\n");
	    Bye();
	}
	printf("Spread library version is %d.%d.%d\n", mver, miver, pver);

    strcpy( Spread_name, SPREAD_PORT);
    strcpy( User, "" );
    ret = SP_connect_timeout( Spread_name, User, 0, 1, &Mbox, Private_group, test_timeout );
	if( ret != ACCEPT_SESSION ) 
	{
		SP_error( ret );
		Bye();
	}
	printf("User: connected to %s with private group %s\n", Spread_name, Private_group );

    E_init();

    // join the group with all servers
    ret = SP_join( Mbox, SERVERS_GROUP);
    if( ret < 0 ) SP_error( ret );

    // join the group with only the local server
    ret = SP_join( Mbox, local_group);
    if( ret < 0 ) SP_error( ret );

    E_attach_fd(Mbox, READ_FD, Read_message, 0, NULL, HIGH_PRIORITY );

    // Clear_chat_room();
    // prepare the linked list in memory
    for(i=0; i<5; i++){
        Read_log(file_name[i], i+1);
    }   
    for(i=0; i<MAX_CHAT_ROOM; i++){
        if(chat_rooms[i]!=NULL){
            Sort(i);
            Reformat_list(i);
        }
    }
    
    E_handle_events();
}



static void Read_message(){
    static char	mess[MAX_MESSLEN];
    char sender[MAX_GROUP_NAME];
    char target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
    membership_info memb_info;
    
    int num_groups;
    int service_type;
    int16 mess_type;
    int	endian_mismatch;
    int	i,j,k;
    int	ret;

    service_type = 0;

	ret = SP_receive( Mbox, &service_type, sender, 100, &num_groups, target_groups, 
		&mess_type, &endian_mismatch, sizeof(mess), mess );

	if(ret < 0){
        if((ret == GROUPS_TOO_SHORT) || (ret == BUFFER_TOO_SHORT)){
            service_type = DROP_RECV;
            printf("\n========Buffers or Groups too Short=======\n");
            ret = SP_receive( Mbox, &service_type, sender, MAX_MEMBERS, &num_groups, target_groups, 
                                &mess_type, &endian_mismatch, sizeof(mess), mess );
        }
    }
    if(ret < 0 ){
        if(!To_exit){
            SP_error( ret );
            printf("\n============================\n");
            printf("\nBye.\n");
        }
        exit( 0 );
	}
	if(Is_regular_mess( service_type)){
		
        if(ret==sizeof(struct Packet)){
            struct Packet packet;
            memcpy(&packet, mess, ret);
            switch(packet.type){
                case JOIN:
                    Send_message(packet, sender);                 
                    break;
                case LIKE:
                    Send_message(packet, "");
                    break;
                case UNLIKE:
                    Send_message(packet, "");
                    break;
                case REGULAR:   
                    Send_message(packet, "");
                    break;
                case HISTORY:
                    Send_message(packet, sender);
                    break;
                case NETWORK:
                    Send_message(packet, sender);
                    break; 
                case PARTICIPANT:
                    printf("not handled\n");
                    break;
            }
        }else if(ret==sizeof(struct PropPacket)){
            struct PropPacket packet;
            memcpy(&packet, mess, ret);
            Process_prop_packet(packet);
        }else if(ret==sizeof(struct GroupPacket)){
            struct GroupPacket packet;
            memcpy(&packet, mess, ret);
            Process_group_packet(packet);
        }else if(ret==sizeof(struct UserNamePacket)){
            struct UserNamePacket packet;
            memcpy(&packet, mess, ret);
            Process_user_name_packet(packet);
        }
        
        
        
	}else if(Is_membership_mess( service_type)){
        ret = SP_get_memb_info( mess, service_type, &memb_info );
        if(ret < 0) {
            printf("BUG: membership message does not have valid body\n");
            SP_error( ret );
            exit( 1 );
        }
		if(Is_reg_memb_mess(service_type)){
                
            if(strcmp(sender, SERVERS_GROUP)==0){
                int has_join = 0;
                int tmp_group_members[5];
                memset(tmp_group_members, 0, 5*sizeof(int));

                // detect if new server joined the partition, if true, need to propagate the chat history to the joiner
                // if no one joined and possibly server left the partition, no need to propagate
                for(i=0; i < num_groups; i++){
                    int len = strlen(&target_groups[i][0]);
                    int id = atoi(&(&target_groups[i][0])[len-1]);
                    if(group_members[id-1]==0){
                        has_join = 1;
                    }
                    tmp_group_members[id-1] = 1;
                }

                for(i=0; i<5; i++){
                    if(!tmp_group_members[i]){
                        // if the server is not in the current partition, remove all the relevant information including the participant info
                        Remove_all_participant_from_server(i);
                    }
                }
                // the membership in the current partition is updated
                memcpy(group_members, tmp_group_members, 5*sizeof(int));

                if(has_join){
                    Send_group_packet();
                }
            }

            // check if the user name left the connected server, if true, propagate it to the other servers
            if(!Is_caused_join_mess(service_type)){
                printf("%s\n", sender);
                for(i=0; i<MAX_CHAT_ROOM; i++){
                    if(strcmp(chat_group[i], sender)==0){
                        char *user_name = strtok(sender, "$");
                        char *server = strtok(NULL, "$");
                        char *chat_room = strtok(NULL, "$");
                        Remove_chat_group(user_name, server, chat_room);
                        for(j=0; j<MAX_CHAT_ROOM; j++){
                            if(strcmp(chat_room, chat_room_names[j])==0){
                                for(k=0; k<MAX_PARTICIPANTS; k++){
                                    if(strcmp(chat_room_participants[j][k], user_name)==0){
                                        memset(chat_room_participants[j][k], 0, 20*sizeof(char));
                                        Remove_participant_from_server(user_name, chat_room, server_id-1);
                                        Propagate_user_name(chat_room, user_name, DELETE);
                                        break;
                                    }
                                }
                            }
                        }
                        break;                     
                    }
                }  
            }
		}
	}else{
        printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);
    }
}

static void	Bye(){
	To_exit = 1;
	printf("\nBye.\n");
	SP_disconnect( Mbox );
	exit(0);
}


static void Send_message(struct Packet packet, char *receiver){
    char groups[10][MAX_GROUP_NAME];
    int num_groups;
    int ret;
    struct Packet sendPacket;

    // check if duplicate user name is in the chat room
    if(packet.type==JOIN && check_duplicate_name(packet.user_name, packet.chat_room)){
        packet.status = 1;
        packet.ctype = USERNAME;
        sprintf(packet.message, "user name <%.20s> has already being occupied in room <%.20s>, please select a new name\n", packet.user_name, packet.chat_room);

        strcpy(groups[0], receiver);
        num_groups = 1;
        ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(packet), (char*)&packet );
        if( ret < 0 ) {
            SP_error( ret );
            Bye();
        }
        return;
    }

    // send back to the sender or the entire chat room
    if(strlen(receiver)>0){
        strcpy(groups[0], receiver);
    }else{
        strcpy(groups[0], packet.chat_room);
    }
    
    num_groups = 1;

    char *ret_message = Process_packet(packet);
    strcpy(sendPacket.user_name, local_group);
    sendPacket.type = packet.type;
    strcpy(sendPacket.message, ret_message);
    strcpy(sendPacket.chat_room, packet.chat_room);
    sendPacket.status = 0;
    sendPacket.ctype = packet.ctype;

    ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(sendPacket), (char*)&sendPacket );
    free(ret_message);
    if( ret < 0 ) {
        SP_error( ret );
        Bye();
    }
}

static void Send_group_packet(){
    char groups[10][MAX_GROUP_NAME];
    int num_groups;
    int ret;
    int i, j, k;
    struct GroupPacket packet;

    k=0;

    // propagate the version vector and the participants in chat rooms
    memcpy(packet.version_vector, version_vector, 5*sizeof(unsigned long));
    for(i=0; i<MAX_CHAT_ROOM; i++){
        for(j=0; j<MAX_PARTICIPANTS; j++){
            if(strlen(chat_room_participants[i][j])>0){
                struct Participant p;
                strcpy(p.chat_room, chat_room_names[i]);
                strcpy(p.user_name, chat_room_participants[i][j]);
                p.server = find_server_id(chat_room_participants[i][j], chat_room_names[i]);
                memcpy(&packet.participants[k++], &p, sizeof(p));
            }
        }
    }
    packet.from_server_id = server_id;
    packet.num_participants = k;

    strcpy(groups[0], SERVERS_GROUP);
    num_groups = 1;

    ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(packet), (char*)&packet );
    if( ret < 0 ) {
        SP_error( ret );
        Bye();
    }
}

static void Process_group_packet(struct GroupPacket packet){
    int i, j;
    int ready;
    unsigned long min[5];
    vector_received[packet.from_server_id-1] = 1;
    memcpy(matrix[packet.from_server_id-1], packet.version_vector, sizeof(packet.version_vector));
    for(i=0; i<5; i++){
        min[i] = MAX_LAMPORT;
    }

    // process the participants info
    if(packet.from_server_id!=server_id){
        for(i=0; i<packet.num_participants; i++){
            Add_participant(packet.participants[i]);
            Add_participant_to_server(packet.participants[i].user_name, packet.participants[i].chat_room, packet.participants[i].server-1);
        }
    }
    
    // check if has received all the version vectors in the current partition
    ready = 1;
    for(i=0; i<5; i++){
        if(group_members[i]!=vector_received[i]){
            ready = 0;
            break;
        }
    }

    


    // find the min lamport timestamp and resend
    if(ready){
        for(i=0; i<5; i++){
            if(group_members[i]){
                for(j=0; j<5; j++){
                    min[j] = min[j]>matrix[i][j]?matrix[i][j]:min[j];
                }
            }
        }
        for(i=0; i<5; i++){
            if(group_members[i]==1){
                printf("server %d is active, min %ld\n", i+1, min[i]);
            }
        }
        memset(vector_received, 0, 5*sizeof(int));
        for(j=0; j<5; j++){
            if(min[j]!=MAX_LAMPORT){
                Resend_packet(min[j]+1, j);
            }
        }
        
    }
}

static int Resend_packet(unsigned long start, int server_index){
    FILE *fp; 
	char strLine[MAX_LINE];
    char *regexString = "<(.+)> (.+) (.+) (.+): (.+)\n";
    size_t maxGroups = 6;
    char splitted[MAX_LINE];

    regex_t regexCompiled;
    regmatch_t groupArray[maxGroups];
    struct Packet packet;
    int num_sent = 0;

    if (regcomp(&regexCompiled, regexString, REG_EXTENDED)){
      printf("Could not compile regular expression.\n");
      return 1;
    }

    if((fp = fopen(file_name[server_index],"r+")) == NULL){ 
		printf("Open Falied!"); 
		return -1; 
	}
    while (1){ 
		fgets(strLine,MAX_LINE,fp);
        if(feof(fp)){
            break;
        }

		
        if (regexec(&regexCompiled, strLine, maxGroups, groupArray, 0) == 0){
            unsigned int g = 0;
            char matched[maxGroups][MAX_MESSAGE_SIZE];
            unsigned long cur_lamport;
            for (g = 0; g < maxGroups; g++){
                if (groupArray[g].rm_so == (size_t)-1)
                    break;  // No more groups

                char sourceCopy[strlen(strLine) + 1];
                strcpy(sourceCopy, strLine);
                sourceCopy[groupArray[g].rm_eo] = 0;
                strcpy(matched[g], sourceCopy + groupArray[g].rm_so);
                // printf("Group %u: [%2u-%2u]: %s\n",
                //         g, groupArray[g].rm_so, groupArray[g].rm_eo,
                //         sourceCopy + groupArray[g].rm_so);
            }
            strcpy(splitted, matched[1]);
            cur_lamport = atol(strtok(splitted, " "));
            if(cur_lamport<start){
                continue;
            }
            num_sent++;
            if(num_sent%40==0){
                usleep(1000);
            }
            // printf("%s", strLine);
            strcpy(packet.user_name, matched[4]);
            strcpy(packet.chat_room, matched[2]);
            strcpy(packet.message, matched[5]);
            if(strcmp(matched[3], "regular")==0){               
                packet.type = REGULAR;    
            }else if(strcmp(matched[3], "like")==0){
                packet.type = LIKE;
            }else if(strcmp(matched[3], "unlike")==0){
                packet.type = UNLIKE;
            }
            Propagate_message(packet, cur_lamport, server_index);
            
        }
    }
    return 0;
}

// propagate the user name and return the message to the client
static void Propagate_user_name(char *chat_room, char *user_name, enum UserNameType type){
    char groups[10][MAX_GROUP_NAME];
    int num_groups;
    int ret;
    struct UserNamePacket user_name_packet;
    struct Packet packet;
    strcpy(user_name_packet.chat_room, chat_room);
    strcpy(user_name_packet.user_name, user_name);
    user_name_packet.from_server_id = server_id;
    user_name_packet.type = type;

    strcpy(groups[0], SERVERS_GROUP);
    num_groups = 1;
    ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(user_name_packet), (char*)&user_name_packet );
    if( ret < 0 ) {
        SP_error( ret );
        Bye();
    }

    strcpy(packet.chat_room, chat_room);
    strcpy(packet.user_name, local_group);
    packet.type = PARTICIPANT;
    if(type == ADD){
        sprintf(packet.message, "%s joined chat room: %s from server %d\n", user_name, chat_room, server_id);
    }else if(type == DELETE){
        sprintf(packet.message, "%s left chat room: %s from server %d\n", user_name, chat_room, server_id);
    }

    strcpy(groups[0], chat_room);
    num_groups = 1;
    ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(packet), (char*)&packet );
    if( ret < 0 ) {
        SP_error( ret );
        Bye();
    }
}

// add or remove the user name
static void Process_user_name_packet(struct UserNamePacket packet){
    if(packet.from_server_id == server_id){
        return;
    }
    int i, j;
    for(i=0; i<MAX_CHAT_ROOM; i++){
        if(strlen(chat_room_names[i])>0 && strcmp(chat_room_names[i], packet.chat_room)==0){
            break;
        }
    }
    if(i==MAX_CHAT_ROOM){
        for(i=0; i<MAX_CHAT_ROOM; i++){
            if(strlen(chat_room_names[i])==0){
                strcpy(chat_room_names[i], packet.chat_room);
                break;
            }
        }
    }
    switch(packet.type){
        case ADD:
            for(j=0; j<MAX_PARTICIPANTS; j++){
                if(strlen(chat_room_participants[i][j])==0){
                    strcpy(chat_room_participants[i][j], packet.user_name);
                    Add_participant_to_server(packet.user_name, packet.chat_room, packet.from_server_id-1);
                    printf("add %s\n", packet.user_name);
                    return;
                }
            }
            break;
        case DELETE:
            for(j=0; j<MAX_PARTICIPANTS; j++){
                if(strcmp(chat_room_participants[i][j], packet.user_name)==0){
                    memset(chat_room_participants[i][j], 0, 20*sizeof(char));
                    Remove_participant_from_server(packet.user_name, packet.chat_room, packet.from_server_id-1);
                    printf("remove %s\n", packet.user_name);
                    return;
                }
            }
            break;
    }
}

// propagate the message, the receiving server need to update the lamport timestamp
static void Propagate_message(struct Packet packet, unsigned long lamport, int server_index){
    char groups[10][MAX_GROUP_NAME];
    int num_groups;
    int ret;
    struct PropPacket prop_packet;

    strcpy(prop_packet.chat_room, packet.chat_room);
    strcpy(prop_packet.user_name, packet.user_name);
    strcpy(prop_packet.message, packet.message);
    sprintf(prop_packet.from_server, "server_%d", server_index+1);
    prop_packet.type = packet.type;
    prop_packet.lamport = lamport;
    prop_packet.from_server_id = server_index+1;
    strcpy(groups[0], SERVERS_GROUP);
    num_groups = 1;
    ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(prop_packet), (char*)&prop_packet );
    if( ret < 0 ) {
        SP_error( ret );
        Bye();
    }
}

// need to both update the linked list in the memory and the write log
static void Process_prop_packet(struct PropPacket packet){
    if(packet.lamport<=version_vector[packet.from_server_id-1]){
        return;
    }
    char stamp[200];
    int i;
    struct Node *cur;
    int from_server_id = packet.from_server_id;
    
    updateCounter(packet.lamport);

    sprintf(stamp, "<%.10ld %.10s>", packet.lamport, packet.from_server);
    
    cur = (struct Node*)calloc(1, sizeof(struct Node));
    strcpy(cur->stamp, stamp);
    strcpy(cur->chat_room, packet.chat_room);
    strcpy(cur->user_name, packet.user_name);
    strcpy(cur->message, packet.message);
    cur->type = packet.type;
    
    switch(packet.type){
        case REGULAR:
            fprintf(pFile[from_server_id-1], "%s %s regular %s: %s\n", stamp, packet.chat_room, packet.user_name, packet.message);
            fflush(pFile[from_server_id-1]);
            break;
        case LIKE:
            fprintf(pFile[from_server_id-1], "%s %s like %s: %s\n", stamp, packet.chat_room, packet.user_name, packet.message);
            fflush(pFile[from_server_id-1]);
            break;
        case UNLIKE:
            fprintf(pFile[from_server_id-1], "%s %s unlike %s: %s\n", stamp, packet.chat_room, packet.user_name, packet.message);
            fflush(pFile[from_server_id-1]);
            break;
        case JOIN:
        case HISTORY:
        case NETWORK:
        case PARTICIPANT:
            printf("not implemented\n");
            break;
    }

    version_vector[from_server_id-1] = packet.lamport;

    for(i=0; i<MAX_CHAT_ROOM; i++){
        if(strlen(chat_room_names[i])>0 && strcmp(chat_room_names[i], packet.chat_room)==0){
            break;
        }
    }

    if(i==MAX_CHAT_ROOM){
        for(i=0; i<MAX_CHAT_ROOM; i++){
            if(strlen(chat_room_names[i])==0){
                strcpy(chat_room_names[i], packet.chat_room);
                break;
            }
        }
    }
    
    if(chat_rooms[i]==NULL){
        chat_rooms[i] = cur;
        chat_rooms_tail[i] = cur;
    }else{
        chat_rooms_tail[i]->next = cur;
        cur->prev = chat_rooms_tail[i];
        chat_rooms_tail[i] = cur;
    }
    
    Sort(i);
    Reformat_list(i);
}


static char* Process_packet(struct Packet packet){
    char stamp[200];
    char mess[130];
    int i, j, k;
    int index;
    struct Node *cur;
    char *ret_message;
    int num_likes;
    int num_line;
    char participants[MAX_PARTICIPANTS][20];

    memset(participants, 0, MAX_PARTICIPANTS*20*sizeof(char));

    ret_message = (char*)calloc(MAX_MESSAGE_SIZE, sizeof(char));
    switch(packet.type){
        case JOIN:
            // join the chat group
            Add_chat_group(packet);

            for(i=0; i<MAX_CHAT_ROOM; i++){
                if(strlen(chat_room_names[i])>0 && strcmp(chat_room_names[i], packet.chat_room)==0){
                    break;
                }
            }

            // if the chat room doesn't exist, creat one
            if(i==MAX_CHAT_ROOM){
                for(i=0; i<MAX_CHAT_ROOM; i++){
                    if(strlen(chat_room_names[i])==0){
                        strcpy(chat_room_names[i], packet.chat_room);
                        break;
                    }
                }
            }

            // for(k=0; k<MAX_CHAT_ROOM; k++){
            //     if(strlen(chat_room_names[k])>0){
            //         printf("chat room is: %s\n", chat_room_names[k]);
            //     }
            // }
        
            sprintf(mess, "Room: %.20s\n", packet.chat_room);
            strcat(ret_message, mess);

            struct Participant p;
            strcpy(p.chat_room, packet.chat_room);
            strcpy(p.user_name, packet.user_name);
            p.server = server_id;

            Add_participant(p);

            Add_participant_to_server(packet.user_name, packet.chat_room, server_id-1);
            
            Propagate_user_name(packet.chat_room, packet.user_name, ADD);

            strcat(ret_message, "Current participants: ");
            
            for(j=0; j<MAX_PARTICIPANTS; j++){
                if(strlen(chat_room_participants[i][j])>0){
                    for(k=0; k<MAX_PARTICIPANTS; k++){
                        if(strlen(participants[k])>0 && strcmp(participants[k], chat_room_participants[i][j])==0){
                            break;
                        }
                    }
                    if(k==MAX_PARTICIPANTS){
                        for(k=0; k<MAX_PARTICIPANTS; k++){
                            if(strlen(participants[k])==0){
                                strcpy(participants[k], chat_room_participants[i][j]);
                                break;
                            }
                        }
                    }     
                }
            }
            for(j=0; j<MAX_PARTICIPANTS; j++){
                if(strlen(participants[j])>0){
                    sprintf(mess, "%.20s, ", participants[j]);
                    strcat(ret_message, mess);
                }
            }

            strcat(ret_message, "\n");

            // find the 25 most recent chat message
            cur = chat_rooms_tail[i];
            if(cur!=NULL){
                j = 0;
                while(cur->prev!=NULL && j<24){
                    cur = cur->prev;
                    j++;
                }
                top_nodes[i] = cur;

                i = 0;
                while(cur!=NULL){
                    num_likes = 0;
                    for(j=0; j<MAX_MESSAGE_SIZE; j++){
                        if(strlen(cur->user_liked[j])>0){
                            num_likes++;
                        }
                    }
                    sprintf(mess, "%d. %.10s: %.80s \t Likes: %d\n", ++i, cur->user_name, cur->message, num_likes);
                    strcat(ret_message, mess);
                    cur = cur->next;
                }
            }
            
             
            break;
        case HISTORY:
            for(i=0; i<MAX_CHAT_ROOM; i++){
                if(strlen(chat_room_names[i])>0 && strcmp(chat_room_names[i], packet.chat_room)==0){
                    break;
                }
            }

            if(chat_rooms[i]==NULL){
                sprintf(ret_message, "No history for chat room: %s\n", packet.chat_room);
            }else{
                cur = chat_rooms[i];
                top_nodes[i] = cur;
                i = 0;
                while(cur!=NULL){
                    num_likes = 0;
                    for(j=0; j<MAX_MESSAGE_SIZE; j++){
                        if(strlen(cur->user_liked[j])>0){
                            num_likes++;
                        }
                    }
                    sprintf(mess, "%d. %.10s: %.80s \t Likes: %d\n", ++i, cur->user_name, cur->message, num_likes);
                    strcat(ret_message, mess);
                    cur = cur->next;
                }
            }
            break;
        
        
        case REGULAR:
            // update the linked list
            sprintf(stamp, "%.10ld %s", incrementCounter(), local_group);
            cur = (struct Node*)calloc(1, sizeof(struct Node));
            sprintf(cur->stamp, "<%s>", stamp);
            strcpy(cur->chat_room, packet.chat_room);
            strcpy(cur->user_name, packet.user_name);
            strcpy(cur->message, packet.message);

            index = -1;
            for(i=0; i<MAX_CHAT_ROOM; i++){
                if(strlen(chat_room_names[i])>0 && strcmp(chat_room_names[i], packet.chat_room)==0){
                    index = i;
                    break;
                }
            }
            if(chat_rooms[index]==NULL){
                chat_rooms[i] = cur;
                chat_rooms_tail[i] = cur;
            }else{
                chat_rooms_tail[index]->next = cur;
                cur->prev = chat_rooms_tail[index];
                chat_rooms_tail[index] = cur;
            }           

            sprintf(ret_message, "%s: %s\n", packet.user_name, packet.message);
            Propagate_message(packet, currentCounter(), server_id-1);
            // write to the log
            fprintf(pFile[server_id-1], "<%s> %s regular %s: %s\n", stamp, packet.chat_room, packet.user_name, packet.message);
            fflush(pFile[server_id-1]);

            break;
        case LIKE:
            for(i=0; i<MAX_CHAT_ROOM; i++){
                if(strlen(chat_room_names[i])>0 && strcmp(chat_room_names[i], packet.chat_room)==0){
                    cur = top_nodes[i];
                    break;
                }
            }
            if(cur==NULL){
                sprintf(ret_message, "cannot like, chat room %s not found\n", packet.chat_room);
            }else{
                num_line = atoi(packet.message);
                while(--num_line>0){
                    cur = cur->next;
                }
                // printf("<%s> %s: %s\n", cur->stamp, cur->user_name, cur->message);
                if(strcmp(cur->user_name, packet.user_name)==0){
                    sprintf(ret_message, "%s can not like his/her own message %s: %s\n", packet.user_name, cur->user_name, cur->message);
                }else{
                    int found = 0;
                    for(i=0; i<MAX_MESSAGE_SIZE; i++){
                        if(strcmp(cur->user_liked[i], packet.user_name)==0){
                            found = 1;
                            break;
                        }
                    }
                    if(!found){
                        for(i=0; i<MAX_MESSAGE_SIZE; i++){
                            if(strlen(cur->user_liked[i])==0){
                                strcpy(cur->user_liked[i], packet.user_name);
                                break;
                            }
                        }
                        sprintf(stamp, "%.10ld %s", incrementCounter(), local_group);
                        sprintf(ret_message, "%s likes %s: %s\n", packet.user_name, cur->user_name, cur->message);
                        strcpy(packet.message, cur->stamp);
                        Propagate_message(packet, currentCounter(), server_id-1);
                        fprintf(pFile[server_id-1], "<%s> %s like %s: %s\n", stamp, packet.chat_room, packet.user_name, cur->stamp);
                        fflush(pFile[server_id-1]);
                    }else{
                        sprintf(ret_message, "%s has already liked %s: %s\n", packet.user_name, cur->user_name, cur->message);
                    }                    
                }
            }
            break;
        case UNLIKE:
            for(i=0; i<MAX_CHAT_ROOM; i++){
                if(strlen(chat_room_names[i])>0 && strcmp(chat_room_names[i], packet.chat_room)==0){
                    cur = top_nodes[i];
                    break;
                }
            }
            if(cur==NULL){
                sprintf(ret_message, "cannot unlike, chat room %s not found\n", packet.chat_room);
            }else{
                num_line = atoi(packet.message);
                while(--num_line>0){
                    cur = cur->next;
                }
                // printf("<%s> %s: %s\n", cur->stamp, cur->user_name, cur->message);
                if(strcmp(cur->user_name, packet.user_name)==0){
                    sprintf(ret_message, "%s can not unlike his/her own message %s: %s\n", packet.user_name, cur->user_name, cur->message);
                }else{
                    int found = 0;
                    for(i=0; i<MAX_MESSAGE_SIZE; i++){
                        if(strcmp(cur->user_liked[i], packet.user_name)==0){
                            memset(cur->user_liked[i], 0, MAX_MESSAGE_SIZE);
                            found = 1;
                            break;
                        }
                    }
                    if(!found){
                        sprintf(ret_message, "cannot unlike, you did not like it\n");
                    }else{
                        sprintf(stamp, "%.10ld %s", incrementCounter(), local_group);
                        sprintf(ret_message, "%s unlike %s: %s\n", packet.user_name, cur->user_name, cur->message);
                        strcpy(packet.message, cur->stamp);
                        Propagate_message(packet, currentCounter(), server_id-1);
                        fprintf(pFile[server_id-1], "<%s> %s unlike %s: %s\n", stamp, packet.chat_room, packet.user_name, cur->stamp);
                        fflush(pFile[server_id-1]);
                    }
                }
            }
            break;
        case NETWORK:
            strcpy(ret_message, "The server id(s) in the network are:\n");
            for(i=0; i<5; i++){               
                if(group_members[i]){
                    sprintf(mess, "%d \t", i+1);
                    strcat(ret_message, mess);
                }
            }
            strcat(ret_message, "\n");
            break;
        case PARTICIPANT:
            printf("not handled\n");
            break;
    }
    return ret_message;
}

static int Read_log(char *file, int id){
    FILE *fp; 
	char strLine[MAX_LINE];
    char *regexString = "<(.+)> (.+) (.+) (.+): (.+)\n";
    size_t maxGroups = 6;
    char splitted[MAX_LINE];

    regex_t regexCompiled;
    regmatch_t groupArray[maxGroups];
    int index;
    int i;

    if (regcomp(&regexCompiled, regexString, REG_EXTENDED)){
      printf("Could not compile regular expression.\n");
      return 1;
    }
	if((fp = fopen(file,"r+")) == NULL){ 
		printf("Open Falied!"); 
		return -1; 
	} 

	while (1){ 
		fgets(strLine,MAX_LINE,fp);
        if(feof(fp)){
            break;
        }

		// printf("%s", strLine);
        if (regexec(&regexCompiled, strLine, maxGroups, groupArray, 0) == 0){
            unsigned int g = 0;
            char matched[maxGroups][MAX_MESSAGE_SIZE];
            for (g = 0; g < maxGroups; g++){
                if (groupArray[g].rm_so == (size_t)-1)
                    break;  // No more groups

                char sourceCopy[strlen(strLine) + 1];
                strcpy(sourceCopy, strLine);
                sourceCopy[groupArray[g].rm_eo] = 0;
                strcpy(matched[g], sourceCopy + groupArray[g].rm_so);
                // printf("Group %u: [%2u-%2u]: %s\n",
                //         g, groupArray[g].rm_so, groupArray[g].rm_eo,
                //         sourceCopy + groupArray[g].rm_so);
            }
            strcpy(splitted, matched[1]);
            // recover the version vector and lamport timestamp
            version_vector[id-1] = atol(strtok(splitted, " "));
            counter = version_vector[id-1]>=counter?(version_vector[id-1]+1):counter;

            index = -1;
            for(i=0; i<MAX_CHAT_ROOM; i++){
                if(strlen(chat_room_names[i])>0 && strcmp(chat_room_names[i], matched[2])==0){
                    index = i;
                    break;
                }
            }

            struct Node *node = (struct Node*)calloc(1, sizeof(struct Node));
            sprintf(node->stamp, "<%s>", matched[1]);
            strcpy(node->chat_room, matched[2]);
            strcpy(node->user_name, matched[4]);
            strcpy(node->message, matched[5]);

            if(strcmp(matched[3], "regular")==0){
                node->type = REGULAR;
            }else if(strcmp(matched[3], "like")==0){
                node->type = LIKE;
            }else if(strcmp(matched[3], "unlike")==0){
                node->type = UNLIKE;
            }

            if(index==-1){
                for(i=0; i<MAX_CHAT_ROOM; i++){
                    if(strlen(chat_room_names[i])==0){
                        strcpy(chat_room_names[i], matched[2]);
                        chat_rooms[i] = node;
                        chat_rooms_tail[i] = node;
                        break;
                    }
                }
            }else{
                chat_rooms_tail[index]->next = node;
                node->prev = chat_rooms_tail[index];
                chat_rooms_tail[index] = node;
            }
        }
		
	}

    regfree(&regexCompiled);
	fclose(fp);
	return 0;
}

static void Reformat_list(int index){
    int i;
    struct Node *cur;
    struct Node *node;
    struct Node *tmp;

    node = chat_rooms[index];
    while(node!=NULL){
        if(node->type == LIKE){
            cur = chat_rooms[index];
            while(cur!=NULL && strcmp(cur->stamp, node->message)!=0){
                cur = cur->next;
            }
            if(cur==NULL){
                printf("cannot like, message not found\n");
                chat_rooms_tail[index] = node;
                node = node->next;
                continue;
            }else{
                int found = 0;
                for(i=0; i<MAX_MESSAGE_SIZE; i++){
                    if(strcmp(cur->user_liked[i], node->user_name)==0){
                        found = 1;
                        break;
                    }
                }
                if(!found){
                    for(i=0; i<MAX_MESSAGE_SIZE; i++){
                        if(strlen(cur->user_liked[i])==0){
                            strcpy(cur->user_liked[i], node->user_name);
                            break;
                        }
                    }
                }
            }
            if(node->prev!=NULL){
                node->prev->next = node->next;
            }
            if(node->next!=NULL){
                node->next->prev = node->prev;
            }
            tmp = node;
            node = node->next;
            free(tmp);
        }else if(node->type == UNLIKE){
            cur = chat_rooms[index];
            while(cur!=NULL && strcmp(cur->stamp, node->message)!=0){
                cur = cur->next;
            }
            if(cur==NULL){
                chat_rooms_tail[index] = node;
                node = node->next;
                printf("cannot unlike, message not found\n");
                continue;
            }else{
                int found = 0;
                for(i=0; i<MAX_MESSAGE_SIZE; i++){
                    if(strcmp(cur->user_liked[i], node->user_name)==0){
                        memset(cur->user_liked[i], 0, MAX_MESSAGE_SIZE);
                        break;
                    }
                }
                if(!found){
                    printf("cannot unlike, you did not like it\n");
                }
            }
            if(node->prev!=NULL){
                node->prev->next = node->next;
            }
            if(node->next!=NULL){
                node->next->prev = node->prev;
            }
            tmp = node;
            node = node->next;
            free(tmp);
        }else{
            chat_rooms_tail[index] = node;
            node = node->next;
        }
    }
}

// static void Clear_chat_room(){
//     int i;
//     struct Node *cur;
//     struct Node *tmp;
//     for(i=0; i<MAX_CHAT_ROOM; i++){
//         if(chat_rooms[i]!=NULL){
//             memset(chat_room_names[i], 0, sizeof(chat_room_names[i]));
//             cur = chat_rooms[i];
//             chat_rooms[i] = NULL;
//             chat_rooms_tail[i] = NULL;
//             while(cur!=NULL){
//                 tmp = cur;
//                 cur = cur->next;
//                 free(tmp);
//             }
//         }
//     }
// }

static void Swap(struct Node* node1, struct Node* node2){
    struct Node* tmp;
    if(node1->next==node2){
        if(node1->prev!=NULL){
            node1->prev->next = node2;
        }
        if(node2->next!=NULL){
            node2->next->prev = node1;
        }
        node2->prev = node1->prev;
        node1->next = node2->next;
        node2->next = node1;
        node1->prev = node2;
    }else if(node2->next==node1){
        if(node2->prev!=NULL){
            node2->prev->next = node1;
        }
        if(node1->next!=NULL){
            node1->next->prev = node2;
        }
        node1->prev = node2->prev;
        node2->next = node1->next;
        node1->next = node2;
        node2->prev = node1;
    }else if(node1!=node2){
        if(node1->prev!=NULL){
            node1->prev->next = node2;
        }
        if(node1->next!=NULL){
            node1->next->prev = node2;
        }

        if(node2->prev!=NULL){
            node2->prev->next = node1;
        }
        if(node2->next!=NULL){
            node2->next->prev = node1;
        }

        tmp = node1->prev;
        node1->prev = node2->prev;
        node2->prev = tmp;

        tmp = node1->next;
        node1->next = node2->next;
        node2->next = tmp;
    }
    
}

static void Sort(int index){
    struct Node *cur;
    struct Node *next;
    struct Node *tmp;
    struct Node *dummy;

    dummy = (struct Node*)calloc(1, sizeof(struct Node));
    dummy->next = chat_rooms[index];
    chat_rooms[index]->prev = dummy;

    cur = dummy->next;
    while(cur->next!=NULL){
        next = cur->next;
        while(next!=NULL){
            if(strcmp(next->stamp, cur->stamp)<0){
                Swap(cur, next);
                tmp = next;
                next = cur;
                cur = tmp;
            }
            next = next->next;
        }       
        cur = cur->next;
    }
    chat_rooms[index] = dummy->next;
    chat_rooms_tail[index] = cur;
    dummy->next->prev = NULL;
    free(dummy);  
}

static void Add_participant(struct Participant p){
    int i, k;
    for(i=0; i<MAX_CHAT_ROOM; i++){
        if(strlen(chat_room_names[i])>0 && strcmp(chat_room_names[i], p.chat_room)==0){
            break;
        }
    }

    if(i==MAX_CHAT_ROOM){
        for(i=0; i<MAX_CHAT_ROOM; i++){
            if(strlen(chat_room_names[i])==0){
                strcpy(chat_room_names[i], p.chat_room);
                break;
            }
        }
    }

    for(k=0; k<MAX_PARTICIPANTS; k++){
        if(strlen(chat_room_participants[i][k])==0){
            strcpy(chat_room_participants[i][k], p.user_name);
            printf("add participant %s in %s\n", p.user_name, p.chat_room);
            break;
        }
    }    
}


static void Add_participant_to_server(char *user_name, char *chat_room, int server_index){
    int i;
    for(i=0; i<server_to_num_participants[server_index]; i++){
        if(strcmp(server_to_participants[server_index][i]->user_name, user_name)==0 &&
            strcmp(server_to_participants[server_index][i]->chat_room, chat_room)==0){
            printf("cannot add, already in the server list\n");
            return;
        }
    }
    struct Participant *p = (struct Participant *)calloc(1, sizeof(struct Participant));
    strcpy(p->chat_room, chat_room);
    strcpy(p->user_name, user_name);
    p->server = server_index+1;
    server_to_participants[server_index][server_to_num_participants[server_index]] = p;
    server_to_num_participants[server_index]++;
    printf("add %s in %s from server index %d\n", user_name, chat_room, server_index);
}


static void Remove_all_participant_from_server(int server_index){
    int i, j, k;
    int found;
    for(i=0; i<server_to_num_participants[server_index]; i++){
        for(j=0; j<MAX_CHAT_ROOM; j++){
            if(strlen(chat_room_names[i])>0 && strcmp(chat_room_names[i], server_to_participants[server_index][i]->chat_room)==0){
                break;
            }
        }

        if(j<MAX_CHAT_ROOM){
            found = 0;
            for(k=0; k<MAX_PARTICIPANTS; k++){
                if(strcmp(server_to_participants[server_index][i]->user_name, chat_room_participants[i][k])==0){
                    found = 1;
                    break;
                }
            }
            if(found){
                memset(chat_room_participants[i][k], 0, sizeof(20*sizeof(char)));
                printf("remove %s in %s\n", server_to_participants[server_index][i]->user_name, server_to_participants[server_index][i]->chat_room);
                free(server_to_participants[server_index][i]);
                server_to_participants[server_index][i] = NULL;
                
            }else{
                printf("participant <%s> is not found\n", server_to_participants[server_index][i]->user_name);
            }
        }else{
            printf("chat_room <%s> not found\n", server_to_participants[server_index][i]->chat_room);
        }
    }
    server_to_num_participants[server_index] = 0;
}

static void Remove_participant_from_server(char *user_name, char *chat_room, int server_index){
    int i;
    for(i=0; i<server_to_num_participants[server_index]; i++){
        if(strcmp(server_to_participants[server_index][i]->user_name, user_name)==0 &&
            strcmp(server_to_participants[server_index][i]->chat_room, chat_room)==0){
            break;
        }
    }
    if(i<server_to_num_participants[server_index]){
        free(server_to_participants[server_index][i]);
        server_to_participants[server_index][i] = server_to_participants[server_index][server_to_num_participants[server_index]-1];
        server_to_participants[server_index][server_to_num_participants[server_index]-1] = NULL;
        server_to_num_participants[server_index]--;
        printf("remove %s in %s from server index %d\n", user_name, chat_room, server_index);
    }else{
        printf("participant not found in the server\n");
    }
    
}

static int find_server_id(char *user_name, char *chat_room){
    int i, j;
    for(i=0; i<5; i++){
        for(j=0; j<server_to_num_participants[i]; j++){
            if(strcmp(server_to_participants[i][j]->user_name, user_name)==0 &&
                strcmp(server_to_participants[i][j]->chat_room, chat_room)==0){
                return i+1;
            }
        }
    }
    return -1;
}

static int check_duplicate_name(char *user_name, char *chat_room){
    int i, j;
    for(i=0; i<5; i++){
        for(j=0; j<server_to_num_participants[i]; j++){
            if(i+1==server_id && strcmp(server_to_participants[i][j]->user_name, user_name)==0 &&
                strcmp(server_to_participants[i][j]->chat_room, chat_room)==0){
                printf("found in server %d\n", i);
                return 1;
            }
        }
    }
    return 0;
}

static void Add_chat_group(struct Packet packet){
    int i, ret;
    for(i=0; i<MAX_CHAT_ROOM; i++){
        if(strlen(chat_group[i])==0){
            sprintf(chat_group[i], "%s$%s$%s", packet.user_name, local_group, packet.chat_room);
            ret = SP_join( Mbox, chat_group[i]);
            if( ret < 0 ) SP_error( ret );
            printf("add chat group %s\n", chat_group[i]);
            break;
        }
    }
}

static void Remove_chat_group(char *user_name, char *server, char *chat_room){
    int i, ret;
    char group_name[50];
    sprintf(group_name, "%s$%s$%s", user_name, server, chat_room);

    for(i=0; i<MAX_CHAT_ROOM; i++){
        if(strlen(chat_group[i])>0)
            printf("chat group %s\n", chat_group[i]);
        if(strlen(chat_group[i])>0 && strcmp(chat_group[i], group_name)==0){
            ret = SP_leave( Mbox, chat_group[i]);
            if( ret < 0 ) SP_error( ret );
            memset(chat_group[i], 0, sizeof(chat_group[i]));
            printf("remove chat group %s\n", group_name);
            break;
        }
    }
}
