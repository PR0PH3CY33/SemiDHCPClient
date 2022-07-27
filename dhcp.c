#include <stdio.h>

#include <stdlib.h>

#include <locale.h>

#include <string.h>

#include <errno.h>

#include <unistd.h>

#include <sys/time.h>

#include <sys/ioctl.h>

#include <fcntl.h>

#include <getopt.h>

#include <sys/socket.h>

#include <sys/types.h>

#include <netdb.h>

#include <netinet/in.h>

#include <net/if.h>

#include <arpa/inet.h>

#include <stdbool.h>

#include <pthread.h>


/**** DHCP definitions ****/

#define MAX_DHCP_CHADDR_LENGTH           16
#define MAX_DHCP_SNAME_LENGTH            64
#define MAX_DHCP_FILE_LENGTH             128
#define MAX_DHCP_OPTIONS_LENGTH          312

typedef struct dhcp_packet_struct {

    u_int8_t op;

    u_int8_t htype;

    u_int8_t hlen;

    u_int8_t hops;

    u_int32_t xid;

    u_int16_t secs;

    u_int16_t flags;

    struct in_addr ciaddr;          /* IP address of this machine (if we already have one) */

    struct in_addr yiaddr;          /* IP address of this machine (offered by the DHCP server) */
    
    struct in_addr siaddr;          /* IP address of DHCP server */
    
    struct in_addr giaddr;          /* IP address of DHCP relay */

    unsigned char chaddr [MAX_DHCP_CHADDR_LENGTH];      /* hardware address of this machine */
    
    char sname [MAX_DHCP_SNAME_LENGTH];    /* name of DHCP server */
    
    char file [MAX_DHCP_FILE_LENGTH];      /* boot file name (used for diskless booting?) */

    char options[MAX_DHCP_OPTIONS_LENGTH];  /* options */

} dhcp_packet;


typedef struct dhcp_offer_struct {

    struct in_addr server_address;   /* address of DHCP server that sent this offer */
	
    struct in_addr offered_address;  /* the IP address that was offered to us */
	
    u_int32_t lease_time;            /* lease time in seconds */
	
    u_int32_t renewal_time;          /* renewal time in seconds */
	
    u_int32_t rebinding_time;        /* rebinding time in seconds */
	
    struct dhcp_offer_struct *next;

} dhcp_offer;

typedef struct requested_server_struct{
    
    struct in_addr server_address;
	
    struct requested_server_struct *next;

} requested_server;


#define BOOTREQUEST     1
#define BOOTREPLY       2

#define DHCPDISCOVER    1
#define DHCPOFFER       2
#define DHCPREQUEST     3
#define DHCPDECLINE     4
#define DHCPACK         5
#define DHCPNACK        6
#define DHCPRELEASE     7


#define DHCP_OPTION_MESSAGE_TYPE        53
#define DHCP_OPTION_HOST_NAME           12
#define DHCP_OPTION_BROADCAST_ADDRESS   28
#define DHCP_OPTION_REQUESTED_ADDRESS   50
#define DHCP_OPTION_LEASE_TIME          51
#define DHCP_OPTION_RENEWAL_TIME        58
#define DHCP_OPTION_REBINDING_TIME      59

#define DHCP_INFINITE_TIME              0xFFFFFFFF

#define DHCP_BROADCAST_FLAG 32768

#define DHCP_SERVER_PORT   67
#define DHCP_CLIENT_PORT   68

#define ETHERNET_HARDWARE_ADDRESS            1     /* used in htype field of dhcp packet */
#define ETHERNET_HARDWARE_ADDRESS_LENGTH     6     /* length of Ethernet hardware addresses */

unsigned char client_hardware_address[MAX_DHCP_CHADDR_LENGTH]="";
unsigned int my_client_mac[MAX_DHCP_CHADDR_LENGTH];
int mymac = 0;

char network_interface_name[8] = "wlp10s0";

u_int32_t packet_xid=0;

u_int32_t dhcp_lease_time=0;
u_int32_t dhcp_renewal_time=0;
u_int32_t dhcp_rebinding_time=0;

int dhcpoffer_timeout=2;

dhcp_offer *dhcp_offer_list=NULL;
requested_server *requested_server_list=NULL;

int valid_responses=0;     /* number of valid DHCPOFFERs we received */
int requested_servers=0;   
int requested_responses=0;

int request_specific_address=false;
int received_requested_address=false;
int verbose=0;
struct in_addr requested_address;



int create_dhcp_socket(void) {

    int DHCPClientSock;

    int flag = 1;

    struct sockaddr_in client;

    struct ifreq interface;

    bzero(&client, sizeof(client));

    client.sin_family = AF_INET;

    client.sin_addr.s_addr = INADDR_ANY;

    client.sin_port = htons(67);

    bzero(&client.sin_zero ,sizeof(client.sin_zero));

    DHCPClientSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if(DHCPClientSock < 0) {

        printf("%s\n", "Couldn't Create The DHCP Client Socket!");

        exit(0);

    }

    else {

        /* Set The REUSE Address Flag So We Don't Get Errors When Restarting */

        int sockOptionsResult = setsockopt(DHCPClientSock, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(flag));

        if(sockOptionsResult < 0) {

            printf("%s\n", "Couldn't Set The REUSE Socket Option On The DHCP Client Socket!");
		
	    close(DHCPClientSock);	
		
            exit(0);

        }

        else {
        
            /* Set The Broadbast Option So We Can Listen To DHCP Broadcast Messages */

            int secondSockOptionResult = setsockopt(DHCPClientSock, SOL_SOCKET, SO_BROADCAST, (char *)&flag, sizeof flag);

            if(secondSockOptionResult < 0) {

                printf("%s\n", "Couldn't Set The BROADCAST Socket Option On The DHCP DHCPClientSock Socket!");
		    
	 	close(DHCPClientSock);

                exit(0);

            }

            else {

                int DHCPClientSockBindResult = bind(DHCPClientSock, (struct sockaddr *)&client, sizeof(client));

                if(DHCPClientSockBindResult < 0) {
                
                    printf("%s\n", "Couldn't Bind The DHCP Client Socket!");

		    close(DHCPClientSock);	
			
                    exit(0);

                }

                else {


                    return DHCPClientSock;
                }



            }
                


        }




    }

}


unsigned int *get_hardware_address(int sock, char *interface_name) {

    struct ifreq ifr;

    memcpy(ifr.ifr_name, interface_name, sizeof(interface_name));

    ioctl(sock, SIOCGIFHWADDR, &ifr);

    unsigned int *macAddress = malloc(6);

    memcpy(macAddress, &ifr.ifr_hwaddr.sa_data[0], 1);

    memcpy(macAddress+1, &ifr.ifr_hwaddr.sa_data[1], 1);

    memcpy(macAddress+2, &ifr.ifr_hwaddr.sa_data[2], 1);

    memcpy(macAddress+3, &ifr.ifr_hwaddr.sa_data[3], 1);

    memcpy(macAddress+4, &ifr.ifr_hwaddr.sa_data[4], 1);

    memcpy(macAddress+5, &ifr.ifr_hwaddr.sa_data[5], 1);

    return macAddress;

}



void send_dhcp_packet(int sock, unsigned int *macAddress) {

    dhcp_packet discover_packet;

    struct sockaddr_in sockaddr_broadcast;

    bzero(&discover_packet,sizeof(discover_packet));

    discover_packet.op = BOOTREQUEST;

    discover_packet.htype = ETHERNET_HARDWARE_ADDRESS;

    discover_packet.hlen = ETHERNET_HARDWARE_ADDRESS_LENGTH;

    discover_packet.hlen = 0;

    srand(time(NULL));

    packet_xid = random();

    discover_packet.xid = htonl(packet_xid);

    ntohl(discover_packet.xid);

    discover_packet.secs = 0xFF;

    discover_packet.flags = htons(DHCP_BROADCAST_FLAG);

    memcpy(discover_packet.chaddr, macAddress, ETHERNET_HARDWARE_ADDRESS_LENGTH);

    discover_packet.options[0] = '\x63';
	
    discover_packet.options[1] = '\x82';
	
    discover_packet.options[2] = '\x53';
	
    discover_packet.options[3] = '\x63';

    discover_packet.options[4] = DHCP_OPTION_MESSAGE_TYPE;

    discover_packet.options[5] = '\x01';

    discover_packet.options[6] = DHCPDISCOVER;
	
    bzero(&sockaddr_broadcast.sin_zero,sizeof(sockaddr_broadcast.sin_zero));

    sockaddr_broadcast.sin_family = AF_INET;

    sockaddr_broadcast.sin_port = htons(67);

    sockaddr_broadcast.sin_addr.s_addr = inet_addr("255.255.255.255");

    sendto(sock, (char *)&discover_packet, sizeof(discover_packet), 0, (const struct sockaddr *)&sockaddr_broadcast, sizeof(sockaddr_broadcast));


}




int main() {

    int DHCPClientSocket = create_dhcp_socket();

    unsigned int *macAddress = get_hardware_address(DHCPClientSocket, network_interface_name);

    send_dhcp_packet(DHCPClientSocket, macAddress);




}
