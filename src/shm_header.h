#ifndef _SHM_HEADER_H
#define _SHM_HEADER_H

// #define SHM_KEY 19942017
#define MAX_BUF_SIZE 1024

#define DEBUG 0
#define NANO_MOD 1000000000
#define FRAME_SIZE 1024

typedef struct 
{
	long long seconds;
	long long nanoseconds;
	char shmMsg[MAX_BUF_SIZE];
	int sigNotReceived;
} shared_oss_struct;

typedef struct 
{
	long long seconds;
	long long nanoseconds;
	pid_t procID;
	int pageNumRequested;
	int readwrite;
} shmMsg;

typedef struct {
	int pageID;
	int dirty;
	int unused;
	int valid;
	int second_chance;
	int frameContent[FRAME_SIZE];
} Page;

typedef struct {
	Page page;
	int isFrameAssigned;
	int referenceBit;
} Frame;

typedef struct {
	Frame frames[256];
	int frameID;
	int delimiter;
} PageTable;

typedef struct msgbuf {
  long mType;
  char mText[80];
} msgbuf;

// queue
struct node
{
    int pageNum;
    struct node *ptr;
}*front,*rear,*temp,*front1;

int frontelement();
void enq(int data);
int deq();
void empty();
void display();
void create();
void queuesize();

#endif