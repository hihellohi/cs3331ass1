#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include <map>
#include <string>

#include "header.h"

#define BUFFER sizeof(header)

typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr sockaddr;

static timeval global;

static int total_data;
static int total_segments;
static int total_duplicates;

FILE *flog;

void die(std::string s){
	fprintf(stderr, "%s\n", s.c_str());
	exit(1);
}

void set_timer(timeval *tv){
	gettimeofday(tv, 0);
}

int get_timer(timeval *tv){
	timeval tmp;
	gettimeofday(&tmp, 0);
	int now = tmp.tv_sec * 1000 + tmp.tv_usec / 1000;
	int before = tv->tv_sec * 1000 + tv->tv_usec / 1000;
	int out = now - before;
	if(out < 0){
		out += 24 * 3600000;
	}
	return out;
}

void LogPacket(char *buf, std::string type){
	char flags[5];
	int tmp = 0;

	if(((Header)buf)->flags & 1 << SYN){
		flags[tmp++] = 'S';
	}
	if(((Header)buf)->flags & 1 << FIN){
		flags[tmp++] = 'F';
	}
	if(((Header)buf)->flags & 1 << DATA){
		flags[tmp++] = 'D';
	}
	if(((Header)buf)->flags & 1 << ACK){
		flags[tmp++] = 'A';
	}
	flags[tmp] = 0;

	fprintf(flog, "%s\t%d\t%s\t%u\t%u\t%u\n", 
			type.c_str(),
			get_timer(&global), 
			flags, 
			((Header)buf)->n_seq, 
			((Header)buf)->len, 
			((Header)buf)->n_ack);
}

int tryrecv(int s, char *buf, int bufsize, sockaddr_in *si_target, int *slen){

	memset(buf, 0, bufsize);
	int n = recvfrom(s, buf, bufsize, 0, (sockaddr*)si_target, slen);
	LogPacket(buf, "rcv");
	printf("Received packet #%u from %s:%d\n", 
			((Header)buf)->n_seq, 
			inet_ntoa(si_target->sin_addr), 
			ntohs(si_target->sin_port));

	total_segments++;
	total_data += ((Header)buf)->len;

	return n;
}

int trysend(int s, char *buf, int buffsize, sockaddr *si_target, int slen){

	printf("sending ACK #%u\n", ((Header)buf)->n_ack);
	LogPacket(buf, "snd");
	sendto(s, buf, buffsize, 0, si_target, slen);

	return buffsize;
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
	set_timer(&global);

	sockaddr_in si_me, si_other;

	int s, slen = sizeof(si_other), recv_len;
	char* buf = (char*)malloc(BUFFER);
	
	FILE *fout = fopen(argv[2], "w");
	flog = fopen("Receiver_log.txt", "w");

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

	tryrecv(s, buf, BUFFER, &si_other, &slen);
	ack = ((Header)buf)->n_seq + 1;
	buffsize = ((Header)buf)->n_ack + BUFFER;
	win = ((Header)buf)->size;

	free(buf);
	buf = (char*)malloc(buffsize);

	memset(buf, 0, buffsize);
	((Header)buf)->n_ack = ack;
	((Header)buf)->n_seq = seq;
	((Header)buf)->flags = 1 << SYN | 1 << ACK;

	trysend(s, buf, sizeof(header), (sockaddr*)&si_other, slen);

	std::map<unsigned int, char*> save;
	bool finished = false;

	while(!finished){

		recv_len = tryrecv(s, buf, buffsize, &si_other, &slen);
		
		if(((Header)buf)->flags & 1 << ACK){
			continue;
		}
		else if(((Header)buf)->n_seq - ack > win){
			printf("sequence # eclipsed!\n");
			total_duplicates++;
		}

		else if(((Header)buf)->n_seq != ack){
			printf("out of sequence packet - caching...\n");

			if(save.count(((Header)buf)->n_seq)){
				total_duplicates++;
			}
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
		}

		make_ack(buf, ack, seq, finished);

		trysend(s, buf, sizeof(header), (sockaddr*)&si_other, slen);

		fflush(fout);
		fflush(flog);
		fflush(stdout);
	}

	tryrecv(s, buf, buffsize, &si_other, &slen);

	fprintf(flog, "Data Received: %d bytes\n", total_data);
	fprintf(flog, "Num data segments: %d\n", total_segments);
	fprintf(flog, "Num duplicates: %d\n", total_duplicates);

	close(s);
	fclose(fout);
	fclose(flog);
	free(buf);

	return 0;
}
