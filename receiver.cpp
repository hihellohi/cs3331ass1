#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
//#include <netinet/in.h>

//#include <queue>
#include <map>
#include <vector>
#include <utility>
#include <string>

#include "header.h"

#define BUFFER 1000

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

void die(std::string s){
	perror(s.c_str());
	exit(1);
}


void make_ack(char *buf, unsigned int seq){
	memset(buf, 0, BUFFER);
	((Header)buf)->n_ack = seq;
	((Header)buf)->flags = 1 << ACK;
}

int main(int argc, char **argv){

	if(argc != 3){
		die("args");
	}

	sockaddr_in si_me, si_other;

	int s, slen = sizeof(si_other), recv_len;
	char buf[BUFFER];
	
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

	unsigned int seq = 0;
//	std::priority_queue<
//		std::pair<unsigned int, char*>,
//		std::vector<std::pair<unsigned int, char*> >,
//		std::greater<std::pair<unsigned int, char*> > > save;
	std::map<unsigned int, char*> save;
	bool finished = false;

	while(!finished){

		memset(buf, 0, BUFFER);
		if((recv_len = recvfrom(s, buf, BUFFER, 0, (sockaddr*) &si_other, &slen)) == -1){
			die("recvfrom");
		}

		printf("Received packet #%u from %s:%d\n", ((Header)buf)->n_seq, inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
		
		if(((Header)buf)->n_seq < seq){
			printf("sequence # eclipsed!\n");
			continue;
		}

		else if(((Header)buf)->n_seq > seq){
			printf("out of sequence packet - caching...\n");
			//save.push(std::make_pair( ((Header)buf)->n_seq, (char*)memcpy(malloc(recv_len), buf, recv_len)));
			save[((Header)buf)->n_seq] = (char*)memcpy(malloc(recv_len), buf, recv_len);
		}

		else if(((Header)buf)->flags & 1 << DATA){
			fwrite(buf + sizeof(header), sizeof(char), ((Header)buf)->len, fout);
			seq += ((Header)buf)->len;

			//while(!save.empty() && save.top().first == seq){
			std::map<unsigned int, char*>::iterator it;
			while((it = save.find(seq)) != save.end()){
				//fwrite(save.top().second + sizeof(header), sizeof(char), ((Header)save.top().second)->len, fout);
				fwrite(it->second + sizeof(header), sizeof(char), ((Header)it->second)->len, fout);
				//seq += ((Header)save.top().second)->len;
				seq += ((Header)it->second)->len;
				free(it->second);
				save.erase(it);
				//free(save.top().second);
				//save.pop();
			}
		}else if(((Header)buf)->flags & 1 << FIN && ((Header)buf)->n_seq == seq){
			finished = true;
			seq++;
		}

		make_ack(buf, seq);

        printf("Sending ACK #%u\n", ((Header)buf)->n_ack);

		if(sendto(s, buf, sizeof(header), 0, (sockaddr*)&si_other, slen) == -1){
			die("sendto");
		}

		fflush(stdout);
		fflush(fout);
	}

	close(s);
	fclose(fout);

	return 0;
}
