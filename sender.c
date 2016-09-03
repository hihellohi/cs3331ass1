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

#include <queue>
#include <string>

#include "header.h"

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

void die(std::string s){
	perror(s.c_str());
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

int trysend(int s, char *buf, int buffsize, sockaddr *si_target, int slen){
	return sendto(s, buf, buffsize, 0, si_target, slen);
}

int make_packet(char* buf, int buffsize, int *n_seq, FILE *fin) {

	memset(buf, 0, buffsize);

	if(!(((Header)buf)->len = fread(buf + sizeof(header), 1, buffsize - sizeof(header), fin))) return 0;
	((Header)buf)->n_seq = *n_seq;
	*n_seq += ((Header)buf)->len;
	((Header)buf)->flags = (1 << DATA);
	return 1;
}


int main(int argc, char **argv){

	if(argc != 10){
		die("args");
	}

	FILE *fin = fopen(argv[3], "r");
	if(!fin){
		die("fopen");
	}

//	int mws = atoi(argv[4]);
	int mss = atoi(argv[5]);
	int timeout = atoi(argv[6]) * 1000;

	sockaddr_in si_other;

	int s, slen = sizeof(si_other);
	int buffsize = (sizeof(header) + mss);
	char *buf = (char*)malloc(buffsize);

	if((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1){
		die("socket");
	}

	memset((char*)&si_other, 0, sizeof(si_other));

	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(atoi(argv[2]));

	if(!inet_aton(argv[1], &si_other.sin_addr)){
		die("inet_aton");
	}

	//queue<char*> q;
	int seq = 0;

	while(make_packet(buf, buffsize, &seq, fin)){

		if(trysend(s, buf, sizeof(header) + ((Header)buf)->len, (sockaddr*)&si_other, slen) == -1){
			die("sendto");
		}
		
		int n = tryrecv(s, buf, buffsize, (sockaddr*)&si_other, &slen, timeout);
		if(n == -2){
			printf("timeout\n");
			continue;
		}
		else if(n == -1){
			die("tryrecv");
		}

		printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
		printf("%s\n", buf + sizeof(header));
		fflush(stdout);

	}

	close(s);
	fclose(fin);
	free(buf);

	return 0;
}
