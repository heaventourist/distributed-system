#include "sp.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define int32u unsigned int

static	char	User[80];
static  char    Spread_name[80];

static  char    Private_group[MAX_GROUP_NAME];
static  mailbox Mbox;

static  int     To_exit = 0;

#define MAX_MESSLEN     102400
#define PAYLOAD_SIZE 	1300
#define MAX_VSSETS      10
#define MAX_MEMBERS     100
#define GROUP_NAME		"group_wh"
#define WINDOW_SIZE	 	50

static	void	User_command();
static	void	Read_message();
static  void	Bye();

// Packet structure
struct Packet{
    int processIndex;
    int messageIndex;
    int randomNumber;
    char payload[PAYLOAD_SIZE];
};

int numOfMessages;
int processIndex;
int numberOfProcesses;
int messageIndex = 0;
FILE* pFile;
int count[11];
clock_t start;

int main( int argc, char *argv[] )
{
	if(argc!=4){
        fprintf(stderr, "Usage: ./mcast <num_of_messages> <process_index> <number of processes>\n");
        exit(0);
    }
	int     ret;
	int     mver, miver, pver;
	sp_time test_timeout;

	test_timeout.sec = 5;
	test_timeout.usec = 0;

	numOfMessages = atoi(argv[1]);
	processIndex = atoi(argv[2]);
	numberOfProcesses = atoi(argv[3]);
	char* filename = strcat(argv[2], ".out");
	pFile = fopen(filename, "w");

	for(int i=0; i<11; i++){
		count[i] = 0;
	}


	if (!SP_version( &mver, &miver, &pver)) 
	{
		printf("main: Illegal variables passed to SP_version()\n");
		Bye();
	}
	printf("Spread library version is %d.%d.%d\n", mver, miver, pver);

	ret = SP_connect_timeout( Spread_name, User, 0, 1, &Mbox, Private_group, test_timeout );
	if( ret != ACCEPT_SESSION ) 
	{
		SP_error( ret );
		Bye();
	 }
	 printf("User: connected to %s with private group %s\n", Spread_name, Private_group );

	 E_init();

	 E_attach_fd( 0, READ_FD, User_command, 0, NULL, LOW_PRIORITY );

	 E_attach_fd( Mbox, READ_FD, Read_message, 0, NULL, HIGH_PRIORITY );

	 printf("\nUser> ");
	 fflush(stdout);

	 E_handle_events();

	 return( 0 );
}

static	void	User_command()
{
	char	command[130];
	int	ret;
	int	i;

	for( i=0; i < sizeof(command); i++ ) command[i] = 0;
	if( fgets( command, 130, stdin ) == NULL ) 
            Bye();

	switch( command[0] )
	{
		case 'j':
			ret = SP_join( Mbox, GROUP_NAME);
			if( ret < 0 ) SP_error( ret );

			break;
		default:
			printf("\nUnknown commnad\n");

			break;
	}
	fflush(stdout);

}


static	void	Read_message()
{

	static	char	 mess[MAX_MESSLEN];
	char	 sender[MAX_GROUP_NAME];
	char	 target_groups[MAX_MEMBERS][MAX_GROUP_NAME];
	membership_info  memb_info;
	vs_set_info      vssets[MAX_VSSETS];
	unsigned int     my_vsset_index;
	int      num_vs_sets;
	char     members[MAX_MEMBERS][MAX_GROUP_NAME];
	int		 num_groups;
	int		 service_type;
	int16	 mess_type;
	int		 endian_mismatch;
	int		 i,j;
	int		 ret;

    service_type = 0;

	ret = SP_receive( Mbox, &service_type, sender, 100, &num_groups, target_groups, 
		&mess_type, &endian_mismatch, sizeof(mess), mess );

	if( ret < 0 ) 
	{
		if ( (ret == GROUPS_TOO_SHORT) || (ret == BUFFER_TOO_SHORT) ) {
				service_type = DROP_RECV;
				printf("\n========Buffers or Groups too Short=======\n");
				ret = SP_receive( Mbox, &service_type, sender, MAX_MEMBERS, &num_groups, target_groups, 
									&mess_type, &endian_mismatch, sizeof(mess), mess );
		}
	}
	if (ret < 0 )
	{
		if( ! To_exit )
		{
			SP_error( ret );
			printf("\n============================\n");
			printf("\nBye.\n");
		}
		exit( 0 );
	}
	if( Is_regular_mess( service_type ) )
	{
		struct Packet packet;
		memcpy(&packet, mess, sizeof(struct Packet));

		// alert packets
		if(mess_type == 2){
			// invert the sign to indicate the end of sending useful packets
			if(count[packet.processIndex]>0){
				count[packet.processIndex] = -count[packet.processIndex];
			}
			// keep counting
			count[packet.processIndex]--;

			// check if it's the time to terminate
			int end = 1;
			for(int i=1; i<=numberOfProcesses; i++){
				if(count[i] >= 0){
					end = 0;
					break;
				}
			}
			if(end){
				clock_t end = clock();
                double duration = (double)(end - start)/CLOCKS_PER_SEC;
                printf("time cost %f s\n", duration);
				fflush(pFile);
				fclose(pFile);
				printf("mcast finished!\n");
				Bye();
			}
		}else{
			fprintf(pFile, "%2d, %8d, %8d\n", packet.processIndex, packet.messageIndex, packet.randomNumber);

			// counting
			count[packet.processIndex]++;
		}

		// find the slowest process
		int min = 100000000;
		int minIndex = -1;

		for(int i=1; i<=numberOfProcesses; i++){
			if(abs(count[i])<min){
				min = abs(count[i]);
				minIndex = i;
			}
		}
		
		// if it is the slowest process, when conditions are satisfied, send more packets
		if(abs(count[processIndex])>=messageIndex-WINDOW_SIZE/2 && minIndex == processIndex){
			
			// send useful packets if possible
			for( i=0; i<WINDOW_SIZE && messageIndex<numOfMessages; i++ )
			{
				struct Packet packet;
				packet.processIndex = processIndex;
				packet.messageIndex = messageIndex++;
				packet.randomNumber = rand()%1000000+1;
				
				ret= SP_multicast( Mbox, AGREED_MESS, GROUP_NAME, 1, sizeof(struct Packet), (char*)&packet );

				if( ret < 0 ) 
				{
					SP_error( ret );
					Bye();
				}
			}

			// otherwise, send alert packets
			for(;i<WINDOW_SIZE; i++){
				struct Packet packet;
				packet.processIndex = processIndex;
				packet.messageIndex = messageIndex++;
				
				ret= SP_multicast( Mbox, AGREED_MESS, GROUP_NAME, 2, sizeof(struct Packet), (char*)&packet );

				if( ret < 0 ) 
				{
					SP_error( ret );
					Bye();
				}
			}
		}
		
	}else if( Is_membership_mess( service_type ) )
	{
		ret = SP_get_memb_info( mess, service_type, &memb_info );
		if (ret < 0) {
				printf("BUG: membership message does not have valid body\n");
				SP_error( ret );
				exit( 1 );
		}
		if( Is_reg_memb_mess( service_type ) )
		{
			printf("Received REGULAR membership for group %s with %d members, where I am member %d:\n",
				sender, num_groups, mess_type );
			for( i=0; i < num_groups; i++ )
				printf("\t%s\n", &target_groups[i][0] );
			printf("grp id is %d %d %d\n",memb_info.gid.id[0], memb_info.gid.id[1], memb_info.gid.id[2] );

			if( Is_caused_join_mess( service_type ) )
			{
				printf("Due to the JOIN of %s\n", memb_info.changed_member );
			}else if( Is_caused_leave_mess( service_type ) ){
				printf("Due to the LEAVE of %s\n", memb_info.changed_member );
			}else if( Is_caused_disconnect_mess( service_type ) ){
				printf("Due to the DISCONNECT of %s\n", memb_info.changed_member );
			}else if( Is_caused_network_mess( service_type ) ){
				printf("Due to NETWORK change with %u VS sets\n", memb_info.num_vs_sets);
				num_vs_sets = SP_get_vs_sets_info( mess, &vssets[0], MAX_VSSETS, &my_vsset_index );
				if (num_vs_sets < 0) {
						printf("BUG: membership message has more then %d vs sets. Recompile with larger MAX_VSSETS\n", MAX_VSSETS);
						SP_error( num_vs_sets );
						exit( 1 );
				}
				for( i = 0; i < num_vs_sets; i++ )
				{
						printf("%s VS set %d has %u members:\n",
								(i  == my_vsset_index) ?
								("LOCAL") : ("OTHER"), i, vssets[i].num_members );
						ret = SP_get_vs_set_members(mess, &vssets[i], members, MAX_MEMBERS);
						if (ret < 0) {
								printf("VS Set has more then %d members. Recompile with larger MAX_MEMBERS\n", MAX_MEMBERS);
								SP_error( ret );
								exit( 1 );
						}
						for( j = 0; j < vssets[i].num_members; j++ )
								printf("\t%s\n", members[j] );
				}
			}

			// all the processes are ready
			if(num_groups == numberOfProcesses){
				printf("start...\n");
				start = clock();
				
				// send useful packets if possible
				for( i=0; i<WINDOW_SIZE && messageIndex<numOfMessages; i++ )
				{
					struct Packet packet;
					packet.processIndex = processIndex;
					packet.messageIndex = messageIndex++;
					packet.randomNumber = rand()%1000000+1;
					
					ret= SP_multicast( Mbox, AGREED_MESS, GROUP_NAME, 1, sizeof(struct Packet), (char*)&packet );

					if( ret < 0 ) 
					{
						SP_error( ret );
						Bye();
					}
				}

				// otherwise, send alert packets
				for(;i<WINDOW_SIZE; i++){
					struct Packet packet;
					packet.processIndex = processIndex;
					packet.messageIndex = messageIndex++;
					
					ret= SP_multicast( Mbox, AGREED_MESS, GROUP_NAME, 2, sizeof(struct Packet), (char*)&packet );

					if( ret < 0 ) 
					{
						SP_error( ret );
						Bye();
					}
				}
			}
		}else if( Is_transition_mess(   service_type ) ) {
			printf("received TRANSITIONAL membership for group %s\n", sender );
		}else if( Is_caused_leave_mess( service_type ) ){
			printf("received membership message that left group %s\n", sender );
		}else printf("received incorrecty membership message of type 0x%x\n", service_type );
	} else if ( Is_reject_mess( service_type ) )
	{
	printf("REJECTED message from %s, of servicetype 0x%x messtype %d, (endian %d) to %d groups \n(%d bytes): %s\n",
		sender, service_type, mess_type, endian_mismatch, num_groups, ret, mess );
	}else printf("received message of unknown message type 0x%x with ret %d\n", service_type, ret);
}

static  void	Bye()
{
	To_exit = 1;

	printf("\nBye.\n");

	SP_disconnect( Mbox );

	exit( 0 );
}
