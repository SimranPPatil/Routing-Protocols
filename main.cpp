#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/time.h>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <list>
#include <utility>
#include <mutex>

using namespace std;
void listenForNeighbors();
void* announceToNeighbors(void* unusedParam);

int globalMyID = 0;
int costsToNeighbors [256];
int neighborsPresent [256];
int sequence_num_lsp[256];
list<pair<int,int>> topology[256];
int sequence_num = 0;
mutex neighborsPresent_lock;
//last time you heard from each node. TODO: you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
struct sockaddr_in globalNodeAddrs[256];

int main(int argc, char** argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "Usage: %s mynodeid initialcostsfile logfile\n\n", argv[0]);
		exit(1);
	}

	//initialization: get this process's node ID, record what time it is,
	//and set up our sockaddr_in's for sending to the other nodes.
	globalMyID = atoi(argv[1]);
	int i;
	for(i=0;i<256;i++)
	{
		gettimeofday(&globalLastHeartbeat[i], 0);

		char tempaddr[100];
		sprintf(tempaddr, "10.1.1.%d", i);
		memset(&globalNodeAddrs[i], 0, sizeof(globalNodeAddrs[i]));
		globalNodeAddrs[i].sin_family = AF_INET;
		globalNodeAddrs[i].sin_port = htons(7777);
		inet_pton(AF_INET, tempaddr, &globalNodeAddrs[i].sin_addr);
	}

	//TODO: read and parse initial costs file. default to cost 1 if no entry for a node. file may be empty.

  //structure to maintain costs to other nodes and default to 1

	for(i = 0; i<256; i++)
  {
		if(i == globalMyID)
			costsToNeighbors[i] = 0;
		else
			costsToNeighbors[i] = 1;
	}

  for(i = 0; i<256; i++)
    neighborsPresent[i] = 0;

  for(i = 0; i < 256; i++)
    sequence_num_lsp[i] = 0;

  list<pair<int,int>> emptylist;
  for(i = 0; i < 256; i++)
    topology[i] = emptylist;

	//Open initial costs file
	FILE *fptr;
	fptr = fopen(argv[2], "r");
	if (fptr == NULL)
  {
      printf("Cannot open file \n");
      exit(0);
  }

  //parse file and fill costsToNeighbors array
  int node, cost;
  while(fscanf(fptr, "%d %d", &node, &cost) == 2)
  {
  	costsToNeighbors[node] = cost;
  }

	// for(i = 0; i<256; i++)
	// {
  //   	printf("Cost to node %d is %d\n", i, costsToNeighbors[i]);
	// }

	//socket() and bind() our socket. We will do all sendto()ing and recvfrom()ing on this one.
	if((globalSocketUDP=socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket");
		exit(1);
	}
	char myAddr[100];
	struct sockaddr_in bindAddr;
	sprintf(myAddr, "10.1.1.%d", globalMyID);
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = htons(7777);
	inet_pton(AF_INET, myAddr, &bindAddr.sin_addr);
	if(::bind(globalSocketUDP, (struct sockaddr*)&bindAddr, sizeof(struct sockaddr_in)) < 0)
	{
		perror("bind");
		close(globalSocketUDP);
		exit(1);
	}

	//start threads... feel free to add your own, and to remove the provided ones.
	pthread_t announcerThread;
	pthread_create(&announcerThread, 0, announceToNeighbors, (void*)0);

	//good luck, have fun!
	listenForNeighbors();
}
