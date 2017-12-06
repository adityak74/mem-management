#ifndef _SHM_HEADER_H
#define _SHM_HEADER_H

// #define SHM_KEY 19942017
#define MAX_BUF_SIZE 1024

#define DEBUG 1
#define NANO_MOD 1000000000

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
} shmMsg;

typedef struct msgbuf {
  long mType;
  char mText[80];
} msgbuf;

#endif