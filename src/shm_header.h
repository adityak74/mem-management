#ifndef _SHM_HEADER_H
#define _SHM_HEADER_H

// #define SHM_KEY 19942017
#define MAX_BUF_SIZE 1024

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

#endif