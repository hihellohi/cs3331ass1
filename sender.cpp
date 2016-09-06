#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>

#include <queue>
#include <string>
#include <algorithm>

#include "header.h"

static double dropchance;
static FILE *fout;
static timeval global;

void die(std::string s){
	perror(s.c_str());
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

	fprintf(fout, "%s\t%d\t%s\t%u\t%u\t%u\n", 
			type.c_str(),
			get_timer(&global), 
			flags, 
			((Header)buf)->n_seq, 
			((Header)buf)->len, 
			((Header)buf)->n_ack);
}

int tryrecv(int s, char *buf, int bufsize, sockaddr_in *si_target, int us){
	timeval timeout;
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
	n = recvfrom(s, buf, bufsize, 0, NULL, NULL);
	LogPacket(buf, "rcv");
	printf("Received ACK #%u from %s:%d\n", 
			((Header)buf)->n_ack, 
			inet_ntoa(si_target->sin_addr), 
			ntohs(si_target->sin_port));

	return n;
}

int trysend(int s, char *buf, int buffsize, sockaddr *si_target, int slen){

	printf("sending packet #%u\n", ((Header)buf)->n_seq);
	if(rand()/(((double)RAND_MAX + 1)) > dropchance){
		LogPacket(buf, "snd");
		sendto(s, buf, buffsize, 0, si_target, slen);
	}
	else {
		LogPacket(buf, "drop");
	}

	return buffsize;
}

int make_packet(char* buf, int buffsize, unsigned int *n_seq, unsigned int ack, FILE *fin) {

	memset(buf, 0, buffsize);

	if(!(((Header)buf)->len = fread(buf + sizeof(header), 1, buffsize - sizeof(header), fin))) return 0;
	((Header)buf)->n_seq = *n_seq;
	((Header)buf)->n_ack = ack;
	*n_seq += ((Header)buf)->len;
	((Header)buf)->flags = (1 << DATA);
	return 1;
}

int main(int argc, char **argv){

	if(argc != 9){
		die("usage: ./sender receiver_host_ip receiver_port file.txt MWS MSS timeout pdrop seed");
	}

	FILE *fin = fopen(argv[3], "r");
	fout = fopen("Sender_log.txt", "w");
	if(!fin){
		die("fopen");
	}

	set_timer(&global);

	int mws = atoi(argv[4]);
	int mss = atoi(argv[5]);
	int timeout = atoi(argv[6]) * 1000;
	srand(atoi(argv[8]));

	sockaddr_in si_other;

	int s, slen = sizeof(si_other);
	int n;
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

	dropchance = -1;
	unsigned int seq = rand(), ack = 0;

	((Header)buf)->n_ack = mss;
	((Header)buf)->n_seq = seq++;
	((Header)buf)->size = mss * mws; 	
	((Header)buf)->flags = 1 << SYN;

	trysend(s, buf, sizeof(header), (sockaddr*)&si_other, slen);
	tryrecv(s, buf, buffsize, &si_other, RAND_MAX);
	ack = ((Header)buf)->n_seq + 1;

	memset(buf, 0, buffsize);
	((Header)buf)->n_ack = ack;
	((Header)buf)->n_seq = seq;
	((Header)buf)->flags = 1 << ACK;

	trysend(s, buf, sizeof(header), (sockaddr*)&si_other, slen);

	dropchance = atof(argv[7]);

	std::queue<char*> q;

	timeval timer;
	int fast = 0;

	while(!q.empty() || !feof(fin)){

		while((int)q.size() < mws && make_packet(buf, buffsize, &seq, ack, fin)){
			q.push((char*)memcpy(malloc(buffsize), buf, buffsize));
			
			trysend(s, buf, sizeof(header) + ((Header)buf)->len, (sockaddr*)&si_other, slen);

			if(q.size() == 1){
				set_timer(&timer);
			}
		}

		n = tryrecv(s, buf, buffsize, &si_other, std::max(0, timeout - get_timer(&timer)));
		if(n == -2){

			printf("timeout\n");
			trysend(s, q.front(), sizeof(header) + ((Header)q.front())->len, (sockaddr*)&si_other, slen);

			set_timer(&timer);
			continue;
		}
		else if(((Header)buf)->flags & (1 << ACK)){

			if(!q.empty() && ((Header)buf)->n_ack == ((Header)q.front())->n_seq){
				if(++fast == 3){

					printf("preemptive timeout\n");
					trysend(s, q.front(), sizeof(header) + ((Header)q.front())->len, (sockaddr*)&si_other, slen);

					set_timer(&timer);
				}
			}

			while(!q.empty() && ((Header)buf)->n_ack > ((Header)q.front())->n_seq){
				fast = 0;
				set_timer(&timer);

				free(q.front());
				q.pop();
			}

			fflush(stdout);
		}
	}

	dropchance = -1;

	memset(buf, 0, buffsize);
	((Header)buf)->n_ack = ack;
	((Header)buf)->n_seq = seq++;
	((Header)buf)->flags = 1 << FIN;

	trysend(s, buf, sizeof(header), (sockaddr*)&si_other, slen);
	n = tryrecv(s, buf, buffsize, &si_other, RAND_MAX);
	ack = ((Header)buf)->n_seq + 1;

	memset(buf, 0, buffsize);
	((Header)buf)->n_ack = ack;
	((Header)buf)->n_seq = seq;
	((Header)buf)->flags = 1 << ACK;

	trysend(s, buf, sizeof(header), (sockaddr*)&si_other, slen);

	close(s);
	fclose(fin);
	free(buf);

	return 0;
}
