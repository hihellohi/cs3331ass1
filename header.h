#define SYN 0
#define ACK 1
#define DATA 2
#define FIN 3

typedef struct _header * Header;

typedef struct _header{
	unsigned int n_seq;
	unsigned int n_ack;
	unsigned int len;
	char flags;
} header;
