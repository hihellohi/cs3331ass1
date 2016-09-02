#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
//#include <netinet/in.h>

#define BUFFER 1000

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

void die(char *s){
	perror(s);
	exit(1);
}

int tryrecv(int s, char *buf, int bufsize, sockaddr *si_target, int *slen, int us){
	struct timeval timeout;
	memset((char*)&timeout, 0, sizeof(timeout));
	timeout.tv_usec = us;
	
	fd_set readfds;

	FD_ZERO(&readfds);
	FD_SET(s, &readfds);

	int n = select(s+1, &readfds, NULL, NULL, &timeout);

	if(n == 0){
		return -2;
	}else if(n == -1){
		die("select");
	}

	memset(buf, 0, bufsize);

	return recvfrom(s, buf, bufsize, 0, si_target, slen);
}


int main(int argc, char **argv){

	if(argc != 3){
		die("args");
	}

	sockaddr_in si_other;

	int s, slen = sizeof(si_other);
	char buf[BUFFER];

	if((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1){
		die("socket");
	}

	memset((char*)&si_other, 0, sizeof(si_other));

	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(atoi(argv[2]));

	if(!inet_aton(argv[1], &si_other.sin_addr)){
		die("inet_aton");
	}


	int i = 0;

	while(1){
		fflush(stdout);

		sprintf(buf, "%d", i++);

		if(sendto(s, buf, 12, 0, (sockaddr*)&si_other, slen) == -1){
			die("sendto");
		}
		
		int n = tryrecv(s, buf, BUFFER, (sockaddr*)&si_other, &slen, 10000);
		if(n == -2){
			printf("timeout\n");
			continue;
		}
		else if(n == -1){
			die("tryrecv");
		}

		printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
		printf("%s\n",buf);

	}
	close(s);

	return 0;
}
