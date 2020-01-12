#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>

void *memcpy(void *dest, const void *src, size_t n);
static int first_time = 1;
static int cutoff = 25; /* default is 25% loss */

void sendto_dbg_init(int percent)
{
    /* percent is in integer form (i.e. 1 = 1%, 5 = 5%) */
    cutoff = percent;
    if( cutoff < 0 ) cutoff = 0;
    else if( cutoff > 100 ) cutoff = 100;

    printf("\n++++++++++ cutoff value is %d +++++++++\n", cutoff);
}

int sendto_dbg(int s, const char *buf, int len, int flags,
               const struct sockaddr *to, int tolen )
{
    int ret;
    int decision;
    struct sockaddr_in drop_addr;

    if( first_time )
    {
        struct timeval t;
        gettimeofday( &t, NULL );
        srand( t.tv_sec );
        printf("\n+++++++++\n seed is %d\n++++++++\n", (int)t.tv_sec);
        first_time = 0;
    }
    decision = rand() % 100 + 1;
    if( (cutoff > 0) && (decision <= cutoff ) ) {
        // printf("dropping pkt\n");
        memcpy(&drop_addr, to, tolen);
        drop_addr.sin_port = htons(ntohs(drop_addr.sin_port) + 1);
        {
            int     ret;
            int     decision;
            struct sockaddr_in drop_addr;

            if( first_time )
            {
                struct timeval t;
                gettimeofday( &t, NULL );
                srand( t.tv_sec );
                printf("\n+++++++++\n seed is %d\n++++++++\n", (int)t.tv_sec);
                first_time = 0;
            }
            decision = rand() % 100 + 1;
            if( (cutoff > 0) && (decision <= cutoff ) ) {
                // printf("dropping pkt\n");
                memcpy(&drop_addr, to, tolen);
                drop_addr.sin_port = htons(ntohs(drop_addr.sin_port) + 1);
                ret = sendto( s, buf, len, flags, (struct sockaddr *) &drop_addr, tolen );
                return(ret);
            }
            ret = sendto( s, buf, len, flags, to, tolen );
            return( ret );
        }   
        ret = sendto( s, buf, len, flags, (struct sockaddr *) &drop_addr, tolen );
        return(ret);
    }
    ret = sendto( s, buf, len, flags, to, tolen );
    return( ret );
}
