#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

#include "shm_header.h"

#define PERM 0666
#define MAXCHILD 18
#define MAXFRAMES 256


const int TOTAL_SLAVES = 100;
const int MAXSLAVES = 20;
const long long INCREMENTER = 40;
long long int nextProcessSpawnTime = 0;
int count = 0;
int victims[999999] = {-1};

//initialize frame stuff
const int TOTAL_FRAMES = 256; // each frame is 1k = 1024
static int free_frames = 0;
int num_mem_access = 0;
int num_page_faults = 0;
int allocated_frames[TOTAL_FRAMES] = {0};
PageTable *mainPageTable;
void second_chance_alloc();
void display_page_table();

void showHelpMessage();
void intHandler(int);
void spawnChildren(int);
void cleanup();
void sweep_daemon();
int nextSpawnTime();
void setNextSpawnTime();
void sendMessage(int, int);
void processDeath(int, int, FILE*);

static int max_processes_at_instant = 1; // to count the max running processes at any instance
int process_spawned = 0; // hold the process number
volatile sig_atomic_t cleanupCalled = 0;
key_t key;
key_t shMsgIDKey = 1234;
key_t shmPageTableIDKey = 1235;
int shmPageTableID;
int shmid, shmMsgID, semid, i;
pid_t childpid;
shared_oss_struct *shpinfo;
shmMsg *ossShmMsg;
int timerVal;
int numChildren;
int slaveQueueId;
int masterQueueId;
char *short_options = "hs:l:t:";
char *default_logname = "default.log";
char c;
FILE* file;
int status;
int messageReceived = 0;
struct msqid_ds msqid_ds_buf;

char *i_arg;
char *m_arg;
char *x_arg;
char *s_arg;
char *k_arg;
char *p_arg;

int main(int argc, char const *argv[])
{
	sem_t *semlockp;
	int helpflag = 0;
	int nonoptargflag = 0;
	int index;
	key_t masterKey = 128464;
  	key_t slaveKey = 120314;

	// create the queue
	create();

	// memory for args
	i_arg = malloc(sizeof(char)*20);
	m_arg = malloc(sizeof(char)*20);
	x_arg = malloc(sizeof(char)*20);
	s_arg = malloc(sizeof(char)*20);
	k_arg = malloc(sizeof(char)*20); 
	p_arg = malloc(sizeof(char)*20);

	opterr = 0;

	while ((c = getopt (argc, argv, short_options)) != -1)
	switch (c) {
		case 'h':
			helpflag = 1;
			break;
		case 's':
			numChildren = atoi(optarg);
			if(numChildren > MAXCHILD) {
			  numChildren = MAXCHILD;
			  fprintf(stderr, "No more than 18 child processes allowed at one instance. Reverting to 18.\n");
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
			  timerVal = 50;
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
	alarm(200);

	// generate key using ftok
	key = ftok(".", 'c');

	// key = SHM_KEY;
	if(key == (key_t)-1) {
		fprintf(stderr, "Failed to derive key\n");
	}

	if((slaveQueueId = msgget(slaveKey, IPC_CREAT | 0777)) == -1) {
		perror("Master msgget for slave queue");
		exit(-1);
	}

	if((masterQueueId = msgget(masterKey, IPC_CREAT | 0777)) == -1) {
		perror("Master msgget for master queue");
		exit(-1);
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

	shmPageTableID = shmget(shmPageTableIDKey, sizeof(PageTable), IPC_CREAT | 0777);
	if((shmPageTableID == -1) && (errno != EEXIST)){
		perror("Unable to create shared mem");
		exit(-1);
	}
	if(shmPageTableID == -1) {
		if (((shmPageTableID = shmget(shmPageTableIDKey, sizeof(PageTable), PERM)) == -1) || 
			(mainPageTable = (PageTable*)shmat(shmPageTableID, NULL, 0) == (void *)-1) ) {
			perror("Unable to attach existing shared memory");
			exit(-1);
		}
	} else {
		mainPageTable = (PageTable*)shmat(shmPageTableID, NULL, 0);
		if(mainPageTable == (void *)-1){
			perror("Couldn't attach the shared mem");
			exit(-1);
		}

		for (i=0; i < MAXFRAMES; i++) {
			mainPageTable->frameID = i; 
			mainPageTable->frames[i].isFrameAssigned = 0;
			mainPageTable->frames[i].referenceBit = -1;  // -1 unused, ,0 - used, 1 - second chance
			mainPageTable->frames[i].page.pageID = -1;
			mainPageTable->frames[i].page.unused = 1;
			mainPageTable->frames[i].page.second_chance = 0; // if on a second chance its 2, [0,1,2]
			mainPageTable->frames[i].page.dirty = 0; // initially not dirty
			mainPageTable->frames[i].page.valid = 0; // 1 - valid, 0 - invalid
		}

	}

	//Open file and mark the beginning of the new log
	file = fopen(default_logname, "a");
	if(!file) {
		perror("Error opening file");
		exit(-1);
	}

	fprintf(file,"***** BEGIN LOG *****\n");

	// Fork Off Processes
	spawnChildren(MAXCHILD - 1);

	// loop through clock and keep checking shmMsg

	//while(messageReceived < TOTAL_SLAVES && shpinfo -> seconds < 2 && shpinfo->sigNotReceived) {
	while(messageReceived < TOTAL_SLAVES && shpinfo -> seconds < 150 && shpinfo->sigNotReceived) {

		if ( nextSpawnTime() && max_processes_at_instant < MAXCHILD) {
			if ( max_processes_at_instant < MAXCHILD ) {
				spawnChildren(MAXCHILD - max_processes_at_instant); // spawn upto max 18 processes
				setNextSpawnTime();
			} else {
				setNextSpawnTime();
				if ( DEBUG )
					fprintf(stderr, "MAX PROCESSES HIT. TRYING TO SPAWN IN NEXT TIME.");
			}
			if ( DEBUG )
				fprintf(stderr, "MAX PROCESSES RUNNING NOW : %d\n", max_processes_at_instant);
			
		}
		
		//fprintf(stderr, "CURRENT MASTER TIME : %ld.%ld\n", shpinfo -> seconds, shpinfo -> nanoseconds);
		shpinfo -> nanoseconds += INCREMENTER;
		if( shpinfo -> nanoseconds == 1000000000 ) {
			shpinfo -> seconds += 1;
			shpinfo -> nanoseconds = 0;
			display_page_table();
		}

		if ( ossShmMsg -> procID != -1 ) {
			fprintf(file, "Master: Child %d is terminating at %lld.%lld at my time %lld.%lld\n", ossShmMsg -> procID, ossShmMsg -> seconds, ossShmMsg -> nanoseconds, shpinfo -> seconds, shpinfo -> nanoseconds);
			fprintf(file, "MAX PROCESSES RUNNING AT OSS %lld.%lld : %d\n", shpinfo -> seconds, shpinfo -> nanoseconds, max_processes_at_instant);
			fprintf(file, "PAGE NUM REQUESTED : %d for R/W(0/1) : %d\n\n\n", ossShmMsg -> pageNumRequested, ossShmMsg -> readwrite);
			ossShmMsg -> procID = -1;
			max_processes_at_instant--;

			// enqueue the request here
			enq(ossShmMsg -> pageNumRequested);
			//show the queue
			fprintf(file, "Master: Page Frame Request Queue:\n");
			display();
			fprintf(file, "---\n\n");

			// second chance page alloc algo
			second_chance_alloc();
		}

		
		//processDeath(masterQueueId, 3, file);

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

void processDeath(int qid, int msgtype, FILE *file) {
  struct msgbuf msg;
  if(msgrcv(qid, (void *) &msg, sizeof(msg.mText), msgtype, MSG_NOERROR | IPC_NOWAIT) == -1) {
    if(errno != ENOMSG) {
      perror("Master msgrcv");
    }
  }
  else {
    msgctl(masterQueueId, IPC_STAT, &msqid_ds_buf);
    messageReceived++;
    max_processes_at_instant--;
  }
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
			sprintf(p_arg, "%d", shmPageTableID);
			// share shmid with children
			sprintf(k_arg, "%d", shmid);
			char *userOptions[] = {"./user", "-i", i_arg, "-s", s_arg, "-k", k_arg, "-x", x_arg, "-p", p_arg, (char *)0};
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
		fprintf(stderr, "Master timed out. Terminating rest all process.\n");
	}

	kill(-getpgrp(), SIGQUIT);

}



// setting next spawn time
void setNextSpawnTime() {
	nextProcessSpawnTime = (shpinfo -> seconds * NANO_MOD) + shpinfo -> nanoseconds + (rand() % 500000000 + 1000000); // add 1-500 ms randomly
	if ( DEBUG )
		fprintf(stderr, "\nNext child spawn time : %lld.%lld.\n\n", nextProcessSpawnTime / NANO_MOD, nextProcessSpawnTime % NANO_MOD );
}

// check for next spawn time
int nextSpawnTime() {
	if ( ((shpinfo -> seconds * NANO_MOD) + shpinfo -> nanoseconds) >= nextProcessSpawnTime ) {
		if ( DEBUG )
			fprintf(stderr, "\nNext child spawn time hit at : %lld.%lld.\n\n", nextProcessSpawnTime / NANO_MOD, nextProcessSpawnTime % NANO_MOD );
		return 1;
	}
	return 0;
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

/* Create an empty queue */
void create()
{
    front = rear = NULL;
}
 
/* Returns queue size */
void queuesize()
{
    fprintf(stderr, "\n Queue size : %d", count);
}
 
/* Enqueing the queue */
void enq(int data)
{
    if (rear == NULL)
    {
        rear = (struct node *)malloc(1*sizeof(struct node));
        rear->ptr = NULL;
        rear->pageNum = data;
        front = rear;
    }
    else
    {
        temp=(struct node *)malloc(1*sizeof(struct node));
        rear->ptr = temp;
        temp->pageNum = data;
        temp->ptr = NULL;
 
        rear = temp;
    }
    count++;
}
 
/* Displaying the queue elements */
void display()
{
    front1 = front;
 
    if ((front1 == NULL) && (rear == NULL))
    {
        printf("Queue is empty");
        return;
    }
    while (front1 != rear)
    {
		//printf("%d ", front1->pageNum);
		fprintf(file, "%d ", front1->pageNum);
        front1 = front1->ptr;
    }
    if (front1 == rear) {
        //printf("%d", front1->pageNum);
		fprintf(file, "%d ", front1->pageNum);
	}
}
 
/* Dequeing the queue */
int deq()
{
	int pageNumberDeq = -1;
    front1 = front;
 
    if (front1 == NULL)
    {
        printf("\n Error: Trying to display elements from empty queue");
        return -1;
    }
    else
        if (front1->ptr != NULL)
        {
            front1 = front1->ptr;
            printf("\n Dequed value : %d", front->pageNum);
			pageNumberDeq = front->pageNum;
            free(front);
            front = front1;
        }
        else
        {
			pageNumberDeq = front->pageNum;
            printf("\n Dequed value : %d", front->pageNum);
            free(front);
            front = NULL;
            rear = NULL;
        }
        count--;
		return pageNumberDeq;
}
 
/* Returns the front element of queue */
int frontelement()
{
    if ((front != NULL) && (rear != NULL))
        return(front->pageNum);
    else
        return 0;
}
 
/* Display if queue is empty or not */
void empty()
{
     if ((front == NULL) && (rear == NULL))
        fprintf(stderr, "\n Queue empty");
    else
       fprintf(stderr, "Queue not empty");
}


void second_chance_alloc() { 

	int pos = 0, k = 0, page_found = 0;
	int pageNumRequested = frontelement();
	// check if page is already in there
	for( k = 0; k < MAXFRAMES; k++ ) {
		if ( mainPageTable->frames[k].page.pageID == pageNumRequested) {
			page_found = 1;
			pos = k;
			break;
		}
	}
	if( page_found ) {
		// give a chance or set it to be removed from page table
		if ( mainPageTable->frames[pos].referenceBit == 0 ){
			if ( mainPageTable->frames[pos].page.second_chance != 2 ) {
				mainPageTable->frames[pos].referenceBit = 1; // gets chance
			}else{
				mainPageTable->frames[pos].referenceBit = 0; // already given second chance
				mainPageTable->frames[pos].page.dirty = 1;
			}
		} else if ( mainPageTable->frames[pos].referenceBit == 1 ) {
			mainPageTable->frames[pos].referenceBit = 0;
			mainPageTable->frames[pos].page.dirty = 1;
			mainPageTable->frames[pos].page.second_chance += 1;
		}
	} else {
		// check if page table has space
		for( k = 0; k < MAXFRAMES; k++ ) {
			if ( mainPageTable->frames[k].referenceBit == -1) {
				pos = k;
				mainPageTable->delimiter = pos;
				break;
			}
		}
		if(pos == MAXFRAMES) {
			// page table full. Try to remove a page
			int victim_index = -1, iter = 0;
			while( mainPageTable->frames[iter].referenceBit != 1) {
				iter++;
			}
			// remove the page
			mainPageTable->frames[pos].page.pageID = -1;
			mainPageTable->frames[pos].page.dirty = 1;
			mainPageTable->frames[pos].page.unused = 1;
			mainPageTable->frames[pos].page.valid = 0;
			mainPageTable->frames[pos].referenceBit = -1;

			// now add the new page
			pageNumRequested = deq();
			mainPageTable->frames[pos].page.pageID = pageNumRequested;
			mainPageTable->frames[pos].page.dirty = 0;
			mainPageTable->frames[pos].page.unused = 1;
			mainPageTable->frames[pos].page.valid = 1;
			mainPageTable->frames[pos].referenceBit = 0;
			allocated_frames[pos] = 1;
			num_page_faults++;

		} else {
			pageNumRequested = deq();
			mainPageTable->frames[pos].page.pageID = pageNumRequested;
			mainPageTable->frames[pos].page.dirty = 0;
			mainPageTable->frames[pos].page.unused = 1;
			mainPageTable->frames[pos].page.valid = 1;
			mainPageTable->frames[pos].referenceBit = 0;
			allocated_frames[pos] = 1;
			num_page_faults++;
			
			// for( k=0; k < MAXFRAMES; k++ ) {
			// 	if( victims[k] == -1 )
			// 	break;
			// }
			// victims[k] = pos;

		}
	}

}

void display_page_table() {
	int k = 0;
	fprintf(file, "\nMaster: Frame Table at : %lld.%lld\n\n", shpinfo->seconds, shpinfo->nanoseconds);
	for ( k = 0; k < MAXFRAMES; k++ ) {
		if ( mainPageTable->frames[k].referenceBit == -1 )
			fprintf(file, ".");
		else if ( mainPageTable->frames[k].referenceBit == 0 && mainPageTable->frames[k].page.dirty == 1)
			fprintf(file, "D");
		else if ( mainPageTable->frames[k].referenceBit == 0 )
			fprintf(file, "U");
	}
	fprintf(file, "\n");
	for ( k = 0; k < MAXFRAMES; k++ ) {
		if ( mainPageTable->frames[k].referenceBit == -1)
			fprintf(file, "0");
		else
			fprintf(file, "%d", mainPageTable->frames[k].referenceBit);
	}
	fprintf(file, "\n---\n");
}