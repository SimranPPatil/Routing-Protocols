#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <iostream>
#include <pthread.h>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <list>
#include <utility>
#include <mutex>


#define RECVBUFSIZE 10000
using namespace std;

extern int globalMyID;
extern int sequence_num;
extern int costsToNeighbors [256];
extern int neighborsPresent [256];
extern int sequence_num_lsp[256];
extern mutex neighborsPresent_lock;
extern std::list<std::pair<int,int>> topology[256];
//last time you heard from each node. TODO: you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
extern struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
extern int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
extern struct sockaddr_in globalNodeAddrs[256];


//Yes, this is terrible. It's also terrible that, in Linux, a socket
//can't receive broadcast packets unless it's bound to INADDR_ANY,
//which we can't do in this assignment.
void hackyBroadcast(const char* buf, int length)
{
	int i;
	for(i=0;i<256;i++)
		if(i != globalMyID) //(although with a real broadcast you would also get the packet yourself)
			sendto(globalSocketUDP, buf, length, 0,
				  (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
}

void selectiveBroadcast(const char* buf, int length, int msgfrom)
{
	int i;
	for(i=0;i<256;i++)
		if(i != globalMyID && i != msgfrom) //(although with a real broadcast you would also get the packet yourself)
			sendto(globalSocketUDP, buf, length, 0,
				  (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
}

void* announceToNeighbors(void* unusedParam)
{
	struct timespec sleepFor;
	sleepFor.tv_sec = 0;
	sleepFor.tv_nsec = 300 * 1000 * 1000; //300 ms
	while(1)
	{
		hackyBroadcast("HEREIAM", 7);
		nanosleep(&sleepFor, 0);
	}
}

string createLSPPacket(){
	// format: LSP globalMyID sequence_num {vector of <neighbor, cost> pairs}
	string vectorpairs;
	for(int i = 0; i < 256; i++){
		neighborsPresent_lock.lock();
		if(neighborsPresent[i]){
			neighborsPresent_lock.unlock();
			vectorpairs.append(to_string(i));
			vectorpairs.append(" ");
			vectorpairs.append(to_string(costsToNeighbors[i]));
			vectorpairs.append(" ");
		}
		else
			neighborsPresent_lock.unlock();
	}
	string msg("LSP ");
  msg.append(to_string(globalMyID));
  msg.append(" ");
  msg.append(to_string(sequence_num));
  msg.append(" ");
  msg.append(vectorpairs); // add the cost pairs
  msg.append("\n");

	// increment sequence_num
	sequence_num++;
  return msg;
}

void check_alive(){
	struct timeval now;
	gettimeofday(&now, 0);
	bool flag = false;
	cout << "now: " << now.tv_sec << endl;
	for(int i = 0; i < 256; i++){
		neighborsPresent_lock.lock();
		if(neighborsPresent[i]){
			neighborsPresent_lock.unlock();
			cout << "latest heartbeat: of " << i << " is " << globalLastHeartbeat[i].tv_sec << endl;
			if(now.tv_sec - globalLastHeartbeat[i].tv_sec > 3){
				cout << "LINK FAILURE: " << i << endl;
				neighborsPresent_lock.lock();
				neighborsPresent[i] = 0;
				neighborsPresent_lock.unlock();
				cout << "list size before: " << topology[globalMyID].size() << endl;
				pair<int, int> dead = make_pair(i, costsToNeighbors[i]);
				topology[globalMyID].remove(dead);
				cout << "list size after removal: " << topology[globalMyID].size() << endl;
				flag = true;
			}
		}
		else
			neighborsPresent_lock.unlock();
		if(flag){
			flag = false;
			string LSP = createLSPPacket();
			thread LSP_broadcast_thread(hackyBroadcast, LSP.c_str(), LSP.length()+1);
			LSP_broadcast_thread.detach();
		}
	}
}

void handle_LSP_msg(string msg, int msgfrom){
		// MSG format: LSP ID sequence_num {vector of <neighbor, cost> pairs}
    std::istringstream iss;
    vector<std::string> result;
    iss.str(msg);
    for (string s; iss>>s; )
        result.push_back(s);

		string nodeID = result[1];
		string seq = result[2];
		// check seq num
		cout << "sequence_num_lsp[stoi(nodeID)]: " << sequence_num_lsp[stoi(nodeID)] << endl;
		cout << "stoi(seq): " << stoi(seq) << endl;
		if(sequence_num_lsp[stoi(nodeID)] < stoi(seq)){
			// The new sequence_num is greater; then accept the update
			list <pair<int, int>> connections;

			for(unsigned int i = 3; i < result.size()-1; i+=2){
				int node, cost;
				cout << "i : "<<i << endl;
				node = std::stoi(result[i].c_str());
				cost = std::stoi(result[i+1].c_str());
				connections.push_back(make_pair(node, cost));
			}
			// For this particular node, the distances are such
			topology[stoi(nodeID)] = connections;
			sequence_num_lsp[stoi(nodeID)] = stoi(seq);
		}
		thread forwardLSP(selectiveBroadcast, msg.c_str(), msg.length()+1, msgfrom);
		forwardLSP.detach();
}


void listenForNeighbors()
{
	char fromAddr[100];
	struct sockaddr_in theirAddr;
	socklen_t theirAddrLen;
	unsigned char recvBuf[RECVBUFSIZE];
	memset(recvBuf, 0 , RECVBUFSIZE);

	int bytesRecvd;
	while(1)
	{
		theirAddrLen = sizeof(theirAddr);
		memset(recvBuf, 0 , RECVBUFSIZE);
		if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, RECVBUFSIZE , 0,
					(struct sockaddr*)&theirAddr, &theirAddrLen)) == -1)
		{
			perror("connectivity listener: recvfrom failed");
			exit(1);
		}

		inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);
		int msgfrom;
		short int heardFrom = -1;
		if(strstr(fromAddr, "10.1.1."))
		{
			heardFrom = atoi(
					strchr(strchr(strchr(fromAddr,'.')+1,'.')+1,'.')+1);
			msgfrom = heardFrom;
			//TODO: this node can consider heardFrom to be directly connected to it; do any such logic now.
			// cout<<"HEARD FROM"<<heardFrom<<endl;
			gettimeofday(&globalLastHeartbeat[heardFrom], 0);

			neighborsPresent_lock.lock();
			if(neighborsPresent[heardFrom] != 1){
				neighborsPresent[heardFrom] = 1;
				neighborsPresent_lock.unlock();

				pair <int, int> t = make_pair(heardFrom, costsToNeighbors[heardFrom]);
				topology[globalMyID].push_back(t);
				list <pair <int,int>> temp = topology[globalMyID];
				for(auto it = temp.begin(); it!= temp.end(); it++){
					cout << "Neighbor, cost: " << it->first << " " << it->second << endl;
				}

				// spawn thread for hackybroadcast of LSP PACKET
				string LSPPacket = createLSPPacket();
				cout << "LSP PACKET CREATED IS: " << LSPPacket << endl;
				thread LSP_broadcast_thread(hackyBroadcast, LSPPacket.c_str(), LSPPacket.length()+1);
				LSP_broadcast_thread.detach();
			}
			else
				neighborsPresent_lock.unlock();
		}
		// need to acquire lock
		thread check_alive_thread(check_alive);
		check_alive_thread.detach();

		recvBuf[RECVBUFSIZE-1] = '\0';
		std::string receiveBuf( recvBuf, recvBuf + sizeof recvBuf / sizeof recvBuf[0] );
		cout << "receive buf" << endl;
		cout << receiveBuf << endl;
		//Is it a packet from the manager? (see mp2 specification for more details)
		//send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
		for(int i = 0; i < 256; i++){
			if(!topology[i].empty()){
				for(auto it = topology[i].begin(); it !=  topology[i].end(); it++){
					cout << "in check" << endl;
					cout << " At " << i <<"node: " << it->first << " cost " << it->second << endl;
				}
			}
		}

		if(!strncmp(receiveBuf.c_str(), "send", 4))
		{
			//TODO send the requested message to the requested destination node
			// ...
		}
		//'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
		else if(!strncmp(receiveBuf.c_str(), "cost", 4))
		{
			//TODO record the cost change (remember, the link might currently be down! in that case,
			//this is the new cost you should treat it as having once it comes back up.)
			// ...
			cout << "NODE Id with cost change : "<< endl;

			uint16_t destID;
			memcpy(&destID, &recvBuf[4], 2);
			short int destination = ntohs(destID);
			cout << "destination : " << destination << endl;

			uint32_t ncost;
			memcpy(&ncost, &recvBuf[6], 4);
			short int new_cost = ntohl(ncost);
			cout << "new_cost : " << new_cost << endl;

			int old_cost = costsToNeighbors[destination];
			pair<int, int> to_update = make_pair(destination, old_cost);
			costsToNeighbors[destination] = new_cost;
			pair<int, int> new_data = make_pair(destination, new_cost);

			neighborsPresent_lock.lock();
			if(neighborsPresent[destination]){
				neighborsPresent_lock.unlock();
				topology[globalMyID].remove(to_update);
				topology[globalMyID].push_back(new_data);
				string LSPPacket = createLSPPacket();
				cout << "LSP PACKET CREATED IS in cost: " << LSPPacket << endl;
				thread LSP_broadcast_thread(hackyBroadcast, LSPPacket.c_str(), LSPPacket.length()+1);
				LSP_broadcast_thread.detach();
			}
			else
				neighborsPresent_lock.unlock();


		}
		else if(!strncmp(receiveBuf.c_str(), "LSP", 3)){
			// call messsage handler
			cout << "in lsp" << endl;
			handle_LSP_msg(receiveBuf, msgfrom);
		}
	}
	//(should never reach here)
	close(globalSocketUDP);
}
