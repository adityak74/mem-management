#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

#include "shm_header.h"

#define PERM 0666
#define MAXCHILD 20

const int TOTAL_SLAVES = 100;
const int MAXSLAVES = 20;
const long long INCREMENTER = 4000;

void showHelpMessage();
void intHandler(int);
void spawnChildren(int);
void cleanup();

static int max_processes_at_instant = 1; // to count the max running processes at any instance
int process_spawned = 0; // hold the process number
volatile sig_atomic_t cleanupCalled = 0;
key_t key;
key_t shMsgIDKey = 1234;
int shmid, shmMsgID, semid, i;
pid_t childpid;
shared_oss_struct *shpinfo;
shmMsg *ossShmMsg;
int timerVal;
int numChildren;
char *short_options = "hs:l:t:";
char *default_logname = "default.log";
char c;
FILE* file;
int status;
int messageReceived = 0;

char *i_arg;
char *m_arg;
char *x_arg;
char *s_arg;
char *k_arg;

int main(int argc, char const *argv[])
{
	sem_t *semlockp;
	int helpflag = 0;
	int nonoptargflag = 0;
	int index;

	// memory for args
	i_arg = malloc(sizeof(char)*20);
	m_arg = malloc(sizeof(char)*20);
	x_arg = malloc(sizeof(char)*20);
	s_arg = malloc(sizeof(char)*20);
	k_arg = malloc(sizeof(char)*20); 

	opterr = 0;

	while ((c = getopt (argc, argv, short_options)) != -1)
	switch (c) {
		case 'h':
			helpflag = 1;
			break;
		case 's':
			numChildren = atoi(optarg);
			if(numChildren > MAXCHILD) {
			  numChildren = 20;
			  fprintf(stderr, "No more than 20 child processes allowed at one instance. Reverting to 20.\n");
			}
			break;
		case 't':
			timerVal = atoi(optarg);  
			break;
		case 'l':
			default_logname = optarg;  
			break;
		case '?':
			if (optopt == 's') {
			  fprintf(stderr, "Option -%c requires an argument. Using default value [5].\n", optopt);
			  numChildren = 5;
			}
			else if (optopt == 't') {
			  fprintf(stderr, "Option -%c requires an argument. Using default value [20].\n", optopt);
			  timerVal = 20;
			}
			else if( optopt == 'l') {
			  fprintf(stderr, "Option -%c requires an argument. Using default value [default.log] .\n", optopt);
			  default_logname = "default.log";
			}
			else if (isprint (optopt)) {
			  fprintf(stderr, "Unknown option -%c. Terminating.\n", optopt);
			  return -1;
			}
			else {
			  showHelpMessage();
			  return 0; 
			}
	}

	//print out all non-option arguments
	for (index = optind; index < argc; index++) {
		fprintf(stderr, "Non-option argument %s\n", argv[index]);
		nonoptargflag = 1;
	}

	//if above printed out, print help message
	//and return from process
	if(nonoptargflag) {
		showHelpMessage();
		return 0;
	}

	//if help flag was activated, print help message
	//then return from process
	if(helpflag) {
		showHelpMessage();
		return 0;
	}

	if( numChildren<=0 || timerVal<=0 ) {
		showHelpMessage();
		return 0;
	}

	// handle SIGNALS callback attached
	signal(SIGALRM, intHandler);
	signal(SIGINT, intHandler);
	signal(SIGCHLD, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);

	//set alarm
	alarm(timerVal);

	// generate key using ftok
	key = ftok(".", 'c');

	// key = SHM_KEY;
	if(key == (key_t)-1) {
		fprintf(stderr, "Failed to derive key\n");
	}

	shmid = shmget(key, sizeof(shared_oss_struct), IPC_CREAT | 0777);
	if((shmid == -1) && (errno != EEXIST)){
		perror("Unable to create shared mem");
		exit(-1);
	}
	if(shmid == -1) {
		if (((shmid = shmget(key, sizeof(shared_oss_struct), PERM)) == -1) || 
			(shpinfo = (shared_oss_struct*)shmat(shmid, NULL, 0) == (void *)-1) ) {
			perror("Unable to attach existing shared memory");
			exit(-1);
		}
	} else {
		shpinfo = shmat(shmid, NULL, 0);
		if(shpinfo == (void *)-1){
			perror("Couldn't attach the shared mem");
			exit(-1);
		}

		// initialize shmem params
		// initalize clock to zero

		shpinfo -> seconds = 0;
		shpinfo -> nanoseconds = 0;
		shpinfo -> sigNotReceived = 1;
		strcpy(shpinfo -> shmMsg, "");

	}

	shmMsgID = shmget(shMsgIDKey, sizeof(shmMsg), IPC_CREAT | 0777);
	if((shmMsgID == -1) && (errno != EEXIST)){
		perror("Unable to create shared mem");
		exit(-1);
	}
	if(shmMsgID == -1) {
		if (((shmMsgID = shmget(shMsgIDKey, sizeof(shmMsg), PERM)) == -1) || 
			(ossShmMsg = (shmMsg*)shmat(shmMsgID, NULL, 0) == (void *)-1) ) {
			perror("Unable to attach existing shared memory");
			exit(-1);
		}
	} else {
		ossShmMsg = shmat(shmMsgID, NULL, 0);
		if(ossShmMsg == (void *)-1){
			perror("Couldn't attach the shared mem");
			exit(-1);
		}

		ossShmMsg -> seconds = -1;
		ossShmMsg -> nanoseconds = -1;
		ossShmMsg -> procID = -1;

	}

	//Open file and mark the beginning of the new log
	file = fopen(default_logname, "a");
	if(!file) {
		perror("Error opening file");
		exit(-1);
	}

	fprintf(file,"***** BEGIN LOG *****\n");

	// Fork Off Processes
	spawnChildren(numChildren);

	// loop through clock and keep checking shmMsg

	while(messageReceived < TOTAL_SLAVES && shpinfo -> seconds < 2 && shpinfo->sigNotReceived) {
		
		//fprintf(stderr, "CURRENT MASTER TIME : %ld.%ld\n", shpinfo -> seconds, shpinfo -> nanoseconds);
		shpinfo -> nanoseconds += INCREMENTER;
		if( shpinfo -> nanoseconds == 1000000000 ) {
			shpinfo -> seconds += 1;
			shpinfo -> nanoseconds = 0;
		}

		if ( ossShmMsg -> procID != -1 ) {
			fprintf(file, "Master: Child %d is terminating at %lld.%lld at my time %lld.%lld\n\n", ossShmMsg -> procID, ossShmMsg -> seconds, ossShmMsg -> nanoseconds, shpinfo -> seconds, shpinfo -> nanoseconds);
			ossShmMsg -> procID = -1;
		}

		if(max_processes_at_instant <= TOTAL_SLAVES) {
	      spawnChildren(1);
	    }
	    //fprintf(stderr, "CURRENT MASTER TIME : %ld.%ld\n", shpinfo -> seconds, shpinfo -> nanoseconds);
	}

	// Cleanup

	if(!cleanupCalled) {
		cleanupCalled = 1;
		printf("Master cleanup called from main\n");
		cleanup();
	}

	return 0;
}

// spawnChildren function
void spawnChildren(int childrenCount) {
	int j;

	for (j = 0; j < childrenCount; ++j){
		printf("About to spawn process #%d\n", max_processes_at_instant);
		
		//perror on bad fork
	    if((childpid = fork()) < 0) {
	      perror("Fork Failure");
	      //exit(1);
	    }

		/* child process */
		if(childpid == 0) {
			
			fprintf(stderr, "Max processes running now: %d\n", max_processes_at_instant);
			childpid = getpid();
      		pid_t gpid = getpgrp();
			sprintf(i_arg, "%d", j);
			sprintf(s_arg, "%d", max_processes_at_instant);
			sprintf(x_arg, "%d", shmMsgID);
			// share shmid with children
			sprintf(k_arg, "%d", shmid);
			char *userOptions[] = {"./user", "-i", i_arg, "-s", s_arg, "-k", k_arg, "-x", x_arg, (char *)0};
			execv("./user", userOptions);
			fprintf(stderr, "Print if error %s\n");
		}
		max_processes_at_instant++;
	}	
}

// Detach and remove sharedMem function
int detachAndRemove(int shmid, shared_oss_struct *shmaddr) {
  printf("Master: Remove Shared Memory\n");
  int error = 0;
  if(shmdt(shmaddr) == -1) {
    error = errno;
  }
  if((shmctl(shmid, IPC_RMID, NULL) == -1) && !error) {
    error = errno;
  }
  if(!error) {
    return 0;
  }

  return -1;
}

void cleanup() {
  signal(SIGQUIT, SIG_IGN);
  shpinfo -> sigNotReceived = 0;
  printf("Master sending SIGQUIT\n");
  kill(-getpgrp(), SIGQUIT);

  //free up the malloc'd memory for the arguments
  free(i_arg);
  free(s_arg);
  free(k_arg);
  free(x_arg);
  printf("Master waiting on all processes do die\n");
  childpid = wait(&status);

  printf("Master about to detach from shared memory\n");
  //Detach and remove the shared memory after all child process have died
  if(detachAndRemove(shmid, shpinfo) == -1) {
    perror("Failed to destroy shared memory segment");
  }

  if(detachAndRemove(shmMsgID, ossShmMsg) == -1) {
    perror("Failed to destroy shared memory segment");
  }

  if(fclose(file)) {
    perror("    Error closing file");
  }

  printf("Master about to kill itself\n");
  //Kill this master process
  kill(getpid(), SIGKILL);
}

// handle interrupts
void intHandler(int SIGVAL) {
	signal(SIGQUIT, SIG_IGN);
	signal(SIGINT, SIG_IGN);

	if(SIGVAL == SIGINT) {
		fprintf(stderr, "%sCTRL-C Interrupt initiated.\n");
	}

	if(SIGVAL == SIGALRM) {
		fprintf(stderr, "%sMaster timed out. Terminating rest all process.\n");
	}

	kill(-getpgrp(), SIGQUIT);

}

// help message for running options
void showHelpMessage() {
	printf("-h: Prints this help message.\n");
    printf("-s: Allows you to set the number of child process to run.\n");
    printf("\tThe default value is 5. The max is 20.\n");
    printf("-l: filename for the log file.\n");
    printf("\tThe default value is 'default.log' .\n");
    printf("-t: Allows you set the wait time for the master process until it kills the slaves and itself.\n");
    printf("\tThe default value is 20.\n");
}
