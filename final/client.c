#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

#include "sp.h"

#define MAX_MESSLEN     102400
#define MAX_VSSETS      10
#define MAX_MEMBERS     100
#define MAX_CHAT_ROOM   50
#define MAX_MESSAGE_SIZE 2000
#define SPREAD_PORT "4817"

enum Type{
    JOIN, LIKE, UNLIKE, REGULAR, HISTORY, NETWORK, PARTICIPANT
};

enum ChangeType{
    USERNAME, SERVER, CHATROOM, NORMAL
};

struct Packet{
    char chat_room[MAX_MESSAGE_SIZE];
    char user_name[MAX_MESSAGE_SIZE];
    char message[MAX_MESSAGE_SIZE];
    enum Type type;
    enum ChangeType ctype;
    int status;
};

static char prev_user_name[MAX_MESSAGE_SIZE];
static char user_name[MAX_MESSAGE_SIZE];

static char prev_chat_room[MAX_MESSAGE_SIZE];
static char chat_room[MAX_MESSAGE_SIZE];

char *prev_server = NULL;
char *server = NULL;

static char	User[80];
static char Spread_name[80];

static char Private_group[MAX_GROUP_NAME];
static mailbox Mbox;

static int chatting = 0;

char chat_group[MAX_CHAT_ROOM][50];

static int To_exit = 0;

static void	User_command();
static void Read_message();
static void	Print_menu();
static void	Bye();
// the chat group denotes the group filled by the client and the server the client is connected to
static void Add_chat_group(char *user_name, char *server, char *chat_room);
static void Remove_chat_group(char *user_name, char *server, char *chat_room);


int main(int argc, char **argv){
    int ret;
    int mver, miver, pver;
    sp_time test_timeout;
	
    test_timeout.sec = 5;
    test_timeout.usec = 0;

    memset(user_name, 0, MAX_MESSAGE_SIZE*sizeof(char));
    memset(prev_user_name, 0, MAX_MESSAGE_SIZE*sizeof(char));
    memset(chat_group, 0, MAX_CHAT_ROOM*50*sizeof(char));

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

    E_attach_fd(0, READ_FD, User_command, 0, NULL, LOW_PRIORITY );
    E_attach_fd(Mbox, READ_FD, Read_message, 0, NULL, HIGH_PRIORITY );

    printf("User> ");
    fflush(stdout);
    E_handle_events();
}

static void	User_command(){
	char command[130];
	char groups[10][MAX_GROUP_NAME];
	int	num_groups;
	int	ret;
	int	i;
    int mess_len;
    

	for( i=0; i < sizeof(command); i++ ) command[i] = 0;

	if(fgets(command, 130, stdin ) == NULL) 
        Bye();

	switch(command[0]){
        case 'u':
            if(chatting){
                strcpy(prev_user_name, user_name);
            }           
            ret = sscanf(&command[2], "%s", user_name);
			if(ret < 1) {
				printf(" invalid user name \n");
			}

            if(strcmp(prev_user_name, user_name)==0){
                printf("You are using name %s right now!\n", user_name);
            }else if(chatting){
                // the user name is changed when chatting
                strcpy(groups[0], server);
                num_groups = 1;

                struct Packet packet;

                strcpy(packet.user_name, user_name);
                strcpy(packet.chat_room, chat_room);
                packet.type = JOIN;
                packet.ctype = USERNAME;

                ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(packet), (char*)&packet );
                if( ret < 0 ) {
                    SP_error( ret );
                    Bye();
                }
            }
            break;
        case 'c':
            prev_server = server;
            switch(command[2]){
                case '1':
                    server = "server_1";
                    break;
                case '2':
                    server = "server_2";
                    break;
                case '3':
                    server = "server_3";
                    break;
                case '4':
                    server = "server_4";
                    break;
                case '5':
                    server = "server_5";
                    break;
                default:
                    printf("invalid server id\n");
            }

            if(prev_server!=NULL && strcmp(server, prev_server)==0){
                printf("You are already connected with %s\n", server);
            }else if(chatting){
                // the server is changed when chatting
                strcpy(groups[0], server);
                num_groups = 1;

                struct Packet packet;

                strcpy(packet.user_name, user_name);
                strcpy(packet.chat_room, chat_room);
                packet.type = JOIN;
                packet.ctype = SERVER;

                ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(packet), (char*)&packet );
                if( ret < 0 ) {
                    SP_error( ret );
                    Bye();
                }
            }
            
			break;
		case 'j':
            strcpy(prev_chat_room, chat_room);
			ret = sscanf( &command[2], "%s", chat_room);
			if( ret < 1 ){
				printf(" invalid group \n");
				break;
			}

            if(strcmp(chat_room, prev_chat_room)==0){
                printf("You are already in the chat room %s\n", chat_room);
            }else{
                strcpy(groups[0], server);
                num_groups = 1;

                struct Packet packet;
                
                strcpy(packet.user_name, user_name);
                strcpy(packet.chat_room, chat_room);
                packet.type = JOIN;

                if(chatting){
                    // the chat room is changed when chatting
                    packet.ctype = CHATROOM;
                }else{
                    packet.ctype = NORMAL;
                }


                ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(packet), (char*)&packet );
                if( ret < 0 ) {
                    SP_error( ret );
                    Bye();
                }
            }

            
			break;

		case 'l':
            if(!chatting){
                printf("join a chat room first\n");
            }else{
                strcpy(groups[0], server);
                num_groups = 1;
                
                struct Packet packet;

                strcpy(packet.user_name, user_name);
                strcpy(packet.chat_room, chat_room);
                packet.type = LIKE;
                strcpy(packet.message, &command[2]);

                mess_len = strlen(packet.message);
                if(mess_len>0 && packet.message[mess_len-1] == '\n'){
                    packet.message[mess_len-1] = '\0';
                }

                ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(packet), (char*)&packet );
                if( ret < 0 ) {
                    SP_error( ret );
                    Bye();
                }
            }
            
			break;

		case 'a':
            if(!chatting){
                printf("join a chat room first\n");
            }else{
                strcpy(groups[0], server);
                num_groups = 1;

                struct Packet packet;

                strcpy(packet.user_name, user_name);
                strcpy(packet.chat_room, chat_room);
                packet.type = REGULAR;
                strcpy(packet.message, &command[2]);

                mess_len = strlen(packet.message);
                if(mess_len>0 && packet.message[mess_len-1] == '\n'){
                    packet.message[mess_len-1] = '\0';
                }

                ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(packet), (char*)&packet );
                if( ret < 0 ) {
                    SP_error( ret );
                    Bye();
                }
            }
			break;

		case 'r':
            if(!chatting){
                printf("join a chat room first\n");
            }else{
                strcpy(groups[0], server);
                num_groups = 1;

                struct Packet packet;

                strcpy(packet.user_name, user_name);
                strcpy(packet.chat_room, chat_room);
                packet.type = UNLIKE;
                strcpy(packet.message, &command[2]);

                mess_len = strlen(packet.message);
                if(mess_len>0 && packet.message[mess_len-1] == '\n'){
                    packet.message[mess_len-1] = '\0';
                }

                ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(packet), (char*)&packet );
                if( ret < 0 ) {
                    SP_error( ret );
                    Bye();
                }
            }
			break;

		case 'h':
            if(!chatting){
                printf("join a chat room first\n");
            }else{
                strcpy(groups[0], server);
                num_groups = 1;

                struct Packet packet;

                strcpy(packet.user_name, user_name);
                strcpy(packet.chat_room, chat_room);
                packet.type = HISTORY;

                ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(packet), (char*)&packet );
                if( ret < 0 ) {
                    SP_error( ret );
                    Bye();
                }
            }
			break;

		case 'v':
            if(!chatting){
                printf("join a chat room first\n");
            }else{
                strcpy(groups[0], server);
                num_groups = 1;

                struct Packet packet;

                strcpy(packet.user_name, user_name);
                strcpy(packet.chat_room, chat_room);
                packet.type = NETWORK;

                ret= SP_multigroup_multicast( Mbox, AGREED_MESS, num_groups, (const char (*)[MAX_GROUP_NAME]) groups, 1, sizeof(packet), (char*)&packet );
                if( ret < 0 ) {
                    SP_error( ret );
                    Bye();
                }
            }
			break;

		default:
			printf("\nUnknown commnad\n");
			Print_menu();

			break;
	}
	printf("User> ");
	fflush(stdout);

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
    int	i;
    int	ret;

    service_type = 0;

	ret = SP_receive( Mbox, &service_type, sender, 100, &num_groups, target_groups, 
		&mess_type, &endian_mismatch, sizeof(mess), mess );
	// printf("\n============================\n");
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
		struct Packet packet;
        memcpy(&packet, mess, ret);

        switch(packet.type){
            case JOIN:
                if(packet.status == 0){
                    // if no error happens on the server side
                    if(chatting){
                        if(packet.ctype == USERNAME){
                            Remove_chat_group(prev_user_name, server, chat_room);
                        }else if(packet.ctype == SERVER){
                            Remove_chat_group(user_name, prev_server, chat_room);
                        }else if(packet.ctype == CHATROOM){
                            Remove_chat_group(user_name, server, prev_chat_room);
                            ret = SP_leave( Mbox, prev_chat_room);
                            if( ret < 0 ) SP_error( ret );
                        }

                    }else{
                        chatting = 1;
                    }
                    Add_chat_group(user_name, server, chat_room);
                    ret = SP_join( Mbox, chat_room);
			        if( ret < 0 ) SP_error( ret );
                }else{
                    // if error happens on the server side, need to recover to the previous state
                    if(packet.ctype == USERNAME){
                        if(chatting){
                            strcpy(user_name, prev_user_name);
                            memset(prev_user_name, 0, sizeof(prev_user_name));
                        }else{
                            memset(chat_room, 0, sizeof(chat_room));
                        }
                    }else if(packet.ctype == SERVER){
                        if(chatting){
                            server = prev_server;
                            prev_server = NULL;
                        }else{
                            memset(chat_room, 0, sizeof(chat_room));
                        }
                    }else if(packet.ctype == CHATROOM){
                        if(chatting){
                            strcpy(chat_room, prev_chat_room);
                            memset(prev_chat_room, 0, sizeof(prev_chat_room));
                        }else{
                            memset(chat_room, 0, sizeof(chat_room));
                        }
                    }
                    // printf("recovered\n");
                }
                printf("%s", packet.message);
                break;
            case REGULAR:
                printf("%s", packet.message);
                break;
            case HISTORY:
                printf("%s", packet.message);
                break;
            case LIKE:
                printf("%s", packet.message);
                break;
            case UNLIKE:
                printf("%s", packet.message);
                break;
            case PARTICIPANT:
                printf("%s", packet.message);
                break;
            case NETWORK:
                printf("%s", packet.message);
                break;
        }
        
	}else if(Is_membership_mess( service_type)){
        ret = SP_get_memb_info( mess, service_type, &memb_info );
        if(ret < 0) {
            printf("BUG: membership message does not have valid body\n");
            SP_error( ret );
            exit( 1 );
        }
		if(Is_reg_memb_mess(service_type)){

            // when participant left
            if(!Is_caused_join_mess(service_type)){
                for(i=0; i<MAX_CHAT_ROOM; i++){
                    if(strcmp(chat_group[i], sender)==0){
                        strtok(sender, "$");
                        memset(chat_group[i], 0, 50*sizeof(char));
                        ret = SP_leave( Mbox, sender);
                        if( ret < 0 ) SP_error( ret );
                        printf("The server %s is disconnected\n", strtok(NULL, "$"));
                        break;
                    }
                }
            }
        }	
    }else if(Is_reject_mess(service_type)){
        printf("REJECTED message from %s, of servicetype 0x%x messtype %d, (endian %d) to %d groups \n(%d bytes): %s\n",
            sender, service_type, mess_type, endian_mismatch, num_groups, ret, mess );
	}else{
        printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);
    }
    printf("User> ");
	fflush(stdout);
}

static void	Bye(){
	To_exit = 1;
	printf("\nBye.\n");
	SP_disconnect( Mbox );
	exit(0);
}

static void	Print_menu(){
	printf("\n");
	printf("==========\n");
	printf("User Menu:\n");
	printf("----------\n");
	printf("\n");
    printf("\tu <user name> -- login with the user name\n");
	printf("\tc <server_id> -- connect to server, server_id is in [1,2,3,4,5]\n");
	printf("\tj <chat room> -- join a chat room\n");
    printf("\ta <message> -- append message to chat\n");
    printf("\tl <message id> -- like a message\n");
    printf("\tr <message id> -- remove like from a message\n");
    printf("\th -- print all the history of the current chat room\n");
    printf("\tv -- lprint the membership (identities 1-5) of the servers in the current chat server's network component\n");
	printf("\n");
	fflush(stdout);
}

static void Add_chat_group(char *user_name, char *server, char *chat_room){
    int i, ret;
    for(i=0; i<MAX_CHAT_ROOM; i++){
        if(strlen(chat_group[i])==0){
            sprintf(chat_group[i], "%s$%s$%s", user_name, server, chat_room);
            ret = SP_join( Mbox, chat_group[i]);
            if( ret < 0 ) SP_error( ret );
            // printf("add chat group %s at index %d\n", chat_group[i], i);
            break;
        }
    }
}

static void Remove_chat_group(char *user_name, char *server, char *chat_room){
    int i, ret;
    char group_name[50];
    sprintf(group_name, "%s$%s$%s", user_name, server, chat_room);
    // printf("try to remove %s\n", group_name);
    for(i=0; i<MAX_CHAT_ROOM; i++){
        if(strcmp(chat_group[i], group_name)==0){
            ret = SP_leave( Mbox, chat_group[i]);
            if( ret < 0 ) SP_error( ret );
            memset(chat_group[i], 0, sizeof(chat_group[i]));
            // printf("remove chat group %s at index %d\n", group_name, i);
            break;
        }
    }
}