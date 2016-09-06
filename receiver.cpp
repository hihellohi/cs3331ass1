#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <map>
#include <string>

#include "header.h"

#define BUFFER sizeof(header)

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

void die(std::string s){
	perror(s.c_str());
	exit(1);
}


void make_ack(char *buf, unsigned int ack, unsigned int seq, bool finished){
	memset(buf, 0, BUFFER);
	((Header)buf)->n_ack = ack;
	((Header)buf)->n_seq = seq;
	((Header)buf)->flags = 1 << ACK;
	if(finished){
		((Header)buf)->flags |= 1 << FIN;
	}
}

int main(int argc, char **argv){

	if(argc != 3){
		die("args");
	}
	srand(time(NULL));

	sockaddr_in si_me, si_other;

	int s, slen = sizeof(si_other), recv_len;
	char* buf = (char*)malloc(BUFFER);
	
	FILE *fout = fopen(argv[2], "w");

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

	unsigned int ack = 0, seq = rand();
	unsigned int win, buffsize;

	recvfrom(s, buf, BUFFER, 0, (sockaddr*) &si_other, &slen);
	ack = ((Header)buf)->n_seq + 1;
	buffsize = ((Header)buf)->n_ack + BUFFER;
	win = ((Header)buf)->size;

	free(buf);
	buf = (char*)malloc(buffsize);

	memset(buf, 0, buffsize);
	((Header)buf)->n_ack = ack;
	((Header)buf)->n_seq = seq;
	((Header)buf)->flags = 1 << SYN | 1 << ACK;

	sendto(s, buf, sizeof(header), 0, (sockaddr*)&si_other, slen);

	std::map<unsigned int, char*> save;
	bool finished = false;

	while(!finished){

		memset(buf, 0, buffsize);
		recv_len = recvfrom(s, buf, buffsize, 0, (sockaddr*) &si_other, &slen);

		printf("Received packet #%u from %s:%d\n", ((Header)buf)->n_seq, inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
		
		if(((Header)buf)->n_seq - ack > win){
			printf("sequence # eclipsed!\n");
			continue;
		}

		else if(((Header)buf)->n_seq != ack){
			printf("out of sequence packet - caching...\n");
			save[((Header)buf)->n_seq] = (char*)memcpy(malloc(recv_len), buf, recv_len);
		}

		else if(((Header)buf)->flags & 1 << DATA){
			fwrite(buf + sizeof(header), sizeof(char), ((Header)buf)->len, fout);
			ack += ((Header)buf)->len;

			std::map<unsigned int, char*>::iterator it;
			while((it = save.find(ack)) != save.end()){
				fwrite(it->second + sizeof(header), sizeof(char), ((Header)it->second)->len, fout);
				ack += ((Header)it->second)->len;
				free(it->second);
				save.erase(it);
			}
		}else if(((Header)buf)->flags & 1 << FIN){
			finished = true;
			seq++;
			ack++;
		}else if(((Header)buf)->flags & 1 << ACK){
			continue;
		}

		make_ack(buf, ack, seq, finished);

        printf("Sending ACK #%u\n", ((Header)buf)->n_ack);

		if(sendto(s, buf, sizeof(header), 0, (sockaddr*)&si_other, slen) == -1){
			die("sendto");
		}

		fflush(stdout);
		fflush(fout);
	}

	close(s);
	fclose(fout);
	free(buf);

	return 0;
}
