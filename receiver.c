#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
//#include <netinet/in.h>

#include "header.h"

#define BUFFER 1000

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

void die(char *s){
	perror(s);
	exit(1);
}

int main(int argc, char **argv){

	if(argc != 2){
		die("args");
	}

	sockaddr_in si_me, si_other;

	int s, slen = sizeof(si_other), recv_len;
	char buf[BUFFER];

	if((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1){
		die("socket");
	}

	memset((char*)&si_me, 0, sizeof(si_me));

	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(atoi(argv[1]));
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	
	if(bind(s, (sockaddr*)&si_me, sizeof(si_me)) == -1){
		die("bind");
	}


	while(1){
		printf("waiting...\n");
		fflush(stdout);

		if((recv_len = recvfrom(s, buf, BUFFER, 0, (sockaddr*) &si_other, &slen)) == -1){
			die("recvfrom");
		}
		buf[recv_len] = 0;

		printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
        printf("Data: %s\n", buf + sizeof(header));
		usleep(100000);

		if(sendto(s, buf, recv_len, 0, (sockaddr*)&si_other, slen) == -1){
			die("sendto");
		}
	}
	close(s);

	return 0;
}
