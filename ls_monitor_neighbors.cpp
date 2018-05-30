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
#include <limits.h>
#include <map>

#define RECVBUFSIZE 10000
using namespace std;

extern int globalMyID;
extern int sequence_num;
extern int costsToNeighbors [256];
extern int neighborsPresent [256];
extern int sequence_num_lsp[256];
extern mutex neighborsPresent_lock;
extern mutex topology_lock;
extern mutex nexthop_lock;
extern std::list<std::pair<int,int>> topology[256];
extern int topoMatrix[256][256];
#define MaxVertices 256
extern string filename;
extern FILE* fp;
extern FILE* logfile;
extern map<int, int> nexthop;
extern string logLine;

/*
sprintf(logLine, "sending packet dest %d nexthop %d message %s\n", dest, nexthop, message);
sprintf(logLine, "forward packet dest %d nexthop %d message %s\n", dest, nexthop, message);
sprintf(logLine, "receive packet message %s\n", message);
sprintf(logLine, "unreachable dest %d\n", dest);
*/


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
	string receiveBuf( buf, buf + sizeof buf / sizeof buf[0] );
	for(i=0;i<256;i++)
		if(i != globalMyID){
			//cout << "current cost to " << i << " is " << costsToNeighbors[i] << endl;
			sendto(globalSocketUDP, buf, length, 0,
				  (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
		}
}

void hackyBroadcast2(const char* buf, int length)
{
	int i;
	string receiveBuf( buf, buf + sizeof buf / sizeof buf[0] );
	for(i=0;i<256;i++)
		if(i != globalMyID){
			//cout << "current cost to " << i << " is " << costsToNeighbors[i] << endl;
			if(!nexthop[i]){
				fprintf (logfile, "forward packet dest %d nexthop %d message %s\n", i,  nexthop[i], receiveBuf.c_str());
				fflush (logfile);
				sendto(globalSocketUDP, buf, length, 0,
					  (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
			}
			else{
				fprintf (logfile, "unreachable dest %d\n", i);
				fflush (logfile);
			}
		}
}

void selectiveBroadcast(const char* buf, int length)
{
	topology_lock.lock();
	for(auto it = topology[globalMyID].begin(); it!= topology[globalMyID].end(); it++){
		//cout << "dest in topo: " << it->first << endl;
		//cout << "current cost to " << it->first << " is " << costsToNeighbors[it->first] << endl;
		fprintf (logfile, "forward packet dest %d nexthop %d message %s\n", it->first,  nexthop[it->first], buf);
		fflush (logfile);
		sendto(globalSocketUDP, buf, length, 0,
				(struct sockaddr*)&globalNodeAddrs[it->first], sizeof(globalNodeAddrs[it->first]));
	}
	topology_lock.unlock();
}

string createPeriodicLSP(int dist[MaxVertices]){
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

string createDATAPacket(string data, int destination){
	// DATA destID data
	string msg("DATA ");
  msg.append(to_string(destination));
  msg.append(" ");
  msg.append(data);
  msg.append("\n");

  return msg;
}

void get_path(int path[], int i, vector<int> &temp){
	  if(path[i] == -1)
			return;
    get_path(path, path[i], temp);
		temp.push_back(i);
}

void pr_sol(int dist[], int n, int path[])
{
		vector<int> temp[256];
    int src = globalMyID;
    for (int i = 1; i < 10; i++){
        printf("\n%d -> %d    %d  %d ", src, i, dist[i], src);
				// lock it
				neighborsPresent_lock.lock();
				if(dist[i] < costsToNeighbors[i] && neighborsPresent[i]){
					costsToNeighbors[i] = dist[i];
				}
				neighborsPresent_lock.unlock();
        get_path(path, i, temp[i]);
    }

		for(int i = 0 ; i < MaxVertices ; i++){
		  if(!temp[i].empty()){
				nexthop_lock.lock();
				nexthop[i] = *temp[i].begin();
				nexthop_lock.unlock();
				cout << "path to : " << i << "    -->   ";
		 	 	for(auto it = temp[i].begin(); it != temp[i].end(); it++){
		 		 	cout << *it << " " ;
		 	 	}
			 	cout << endl;
			}
		}

		// neighborsPresent_lock.lock();
		// for(int i = 0; i < 10; i++){
		// 	cout << " for i = " << i << "np is " << neighborsPresent[i] << "cost to ne is " << costsToNeighbors[i] << endl;
		// 	if(neighborsPresent[i])
		// 		cout << "current cost to neighbor " << i << " is -- " << costsToNeighbors[i] << endl;
		// }
		// neighborsPresent_lock.unlock();
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

int mindist(int dist[], bool spt[]){
   int min = INT_MAX;
	 int min_index;

   for (int v = 0; v < MaxVertices; v++)
     if (!spt[v] && dist[v] <= min)
         min = dist[v], min_index = v;

   return min_index;
}

void dijkstra(int topoMatrix[MaxVertices][MaxVertices], int dist[MaxVertices], bool spt[MaxVertices], int path[MaxVertices]){
		 cout << "Starting Djikstras: " << endl;
     dist[globalMyID] = 0;
     for (int count = 0; count < MaxVertices-1; count++){
       int u = mindist(dist, spt);
       spt[u] = true;
       for (int v = 0; v < MaxVertices; v++)
         if (topoMatrix[u][v] != 0 && dist[u] != INT_MAX && !spt[v] && dist[u]+topoMatrix[u][v] < dist[v]){
					 dist[v] = dist[u] + topoMatrix[u][v];
					 path[v] = u;
				 }
     }
		 pr_sol(dist, MaxVertices, path);
}

void fillMatrix(){

	int dist[MaxVertices];
	bool spt[MaxVertices];
	int path[MaxVertices];

	for(int i = 0; i <MaxVertices; i++){
		for(int j = 0; j < MaxVertices; j++){
				topoMatrix[i][j] = 0;
		}
	}

	for(int i = 0; i < MaxVertices; i++){
		topology_lock.lock();
		list<pair<int,int>> curr = topology[i];
		topology_lock.unlock();
		if(!curr.empty()){
			for(auto it = curr.begin(); it!=curr.end(); it++){
				topoMatrix[i][it->first] = it->second;
			}
		}
	}

	for(int i = 0; i < MaxVertices; i++){
		dist[i] = INT_MAX;
		spt[i] = false;
		path[i] = -1;
	}
	dijkstra(topoMatrix, dist, spt, path);
}

void check_alive(){
	while(1){
		struct timeval now;
		gettimeofday(&now, 0);
		bool flag = false;
		//cout << "now: " << now.tv_sec << endl;
		for(int i = 0; i < 256; i++){
			neighborsPresent_lock.lock();
			if(neighborsPresent[i]){
				neighborsPresent_lock.unlock();
				//cout << "latest heartbeat: of " << i << " is " << globalLastHeartbeat[i].tv_sec << endl;
				if(now.tv_sec - globalLastHeartbeat[i].tv_sec > 3){
					cout << "LINK FAILURE: " << i << endl;
					neighborsPresent_lock.lock();
					neighborsPresent[i] = 0;
					neighborsPresent_lock.unlock();
					//cout << "list size before: " << topology[globalMyID].size() << endl;
					pair<int, int> dead = make_pair(i, costsToNeighbors[i]);
					topology_lock.lock();
					topology[globalMyID].remove(dead);
					topology_lock.unlock();
					//cout << "list size after removal: " << topology[globalMyID].size() << endl;
					flag = true;
				}
			}
			else
				neighborsPresent_lock.unlock();
			if(flag){
				flag = false;
				string LSP = createLSPPacket();
				thread LSP_broadcast_thread(selectiveBroadcast, LSP.c_str(), LSP.length()+1);
				LSP_broadcast_thread.detach();
				thread Dj_thread(fillMatrix);
				Dj_thread.detach();
			}
		}
	}
}


void handle_LSP_msg(string msg, int msgfrom){
		// MSG format: LSP ID sequence_num {vector of <neighbor, cost> pairs}
		cout << " in LSP " << endl;
    std::istringstream iss;
    vector<std::string> result;
    iss.str(msg);
    for (string s; iss>>s; )
        result.push_back(s);

		string nodeID = result[1];
		string seq = result[2];
		cout << "stoi(nodeID): " << stoi(nodeID) << endl;
 		cout << "sequence_num_lsp[stoi(nodeID)]: " << sequence_num_lsp[stoi(nodeID)] << endl;
		cout << "sequence_num: " << stoi(seq) << endl;
		if(sequence_num_lsp[stoi(nodeID)] < stoi(seq)){
			// The new sequence_num is greater; then accept the update
			list <pair<int, int>> connections;
			cout << "ACCEPT" << endl;
			for(unsigned int i = 3; i < result.size()-1; i+=2){
				int node, cost;
				node = std::stoi(result[i].c_str());
				cost = std::stoi(result[i+1].c_str());
				connections.push_back(make_pair(node, cost));
			}
			// For this particular node, the distances are such
			topology_lock.lock();
			topology[stoi(nodeID)] = connections;
			topology_lock.unlock();
			sequence_num_lsp[stoi(nodeID)] = stoi(seq);
		}

		thread forwardLSP(hackyBroadcast2, msg.c_str(), msg.length()+1);
		forwardLSP.detach();

		thread Dj_thread(fillMatrix);
		Dj_thread.detach();
}

void handle_DATA_msg(string msg, int msgfrom){
		// MSG format: DATA destID data
    std::istringstream iss;
    vector<std::string> result;
    iss.str(msg);
    for (string s; iss>>s; )
        result.push_back(s);

		string destID = result[1];
		result.erase (result.begin(),result.begin()+2);
		string data;
		for(auto it = result.begin(); it!= result.end(); it++){
			data += *it;
			data += " ";
		}
		int destination = stoi(destID.c_str());
		if(destination == globalMyID){
			// data for self
			// TODO: LOG IT
			cout << " I RECEIVED : " << data << endl;
			fprintf (logfile, "receive packet message %s \n", data.c_str());
			fflush (logfile);
		}
		else{
			// find next hop if not to self and forward
			nexthop_lock.lock();
			sendto(globalSocketUDP, msg.c_str(), msg.length()+1, 0,
				  (struct sockaddr*)&globalNodeAddrs[nexthop[destination]], sizeof(globalNodeAddrs[nexthop[destination]]));
			nexthop_lock.unlock();
			fprintf (logfile, "forward packet dest %d nexthop %d message %s\n", destination,  nexthop[destination], msg.c_str());
			cout << "sending ahead to destination " << destID << " via nexthop:  " << nexthop[destination] << endl;
			fflush (logfile);
		}
}

void listenForNeighbors()
{
	char fromAddr[100];
	struct sockaddr_in theirAddr;
	socklen_t theirAddrLen;
	unsigned char recvBuf[RECVBUFSIZE];
	memset(recvBuf, 0 , RECVBUFSIZE);

	int bytesRecvd;

	thread check_alive_thread(check_alive);
	check_alive_thread.detach();
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
				topology_lock.lock();
				topology[globalMyID].push_back(t);
				list <pair <int,int>> temp = topology[globalMyID];
				topology_lock.unlock();
				for(auto it = temp.begin(); it!= temp.end(); it++){
					cout << "Neighbor, cost: " << it->first << " " << it->second << endl;
				}

				// spawn thread for hackybroadcast of LSP PACKET
				string LSPPacket = createLSPPacket();
				cout << "LSP PACKET CREATED IS on New : " << LSPPacket << endl;
				thread LSP_broadcast_thread(hackyBroadcast2, LSPPacket.c_str(), LSPPacket.length()+1);
				LSP_broadcast_thread.detach();
				thread Dj_thread(fillMatrix);
				Dj_thread.detach();
			}
			else
				neighborsPresent_lock.unlock();
		}

		recvBuf[RECVBUFSIZE-1] = '\0';
		std::string receiveBuf( recvBuf, recvBuf + sizeof recvBuf / sizeof recvBuf[0] );
		// std::string receiveBuf( (const char*) recvBuf, bytesRecvd );
		// cout << "receive buf" << endl;
		// cout << receiveBuf << endl;
		//Is it a packet from the manager? (see mp2 specification for more details)
		//send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>

		// nexthop_lock.lock();
		// for(auto it = nexthop.begin(); it != nexthop.end(); it++){
		// 	cout << " destination: " << it->first << " nexthop: " << it->second << endl;
		// }
		// nexthop_lock.unlock();
		if(!strncmp(receiveBuf.c_str(), "LSP", 3)){
			// call messsage handler
			//cout << "in lsp" << endl;
			handle_LSP_msg(receiveBuf, msgfrom);
		}
		else if(!strncmp(receiveBuf.c_str(), "send", 4))
		{
			//TODO send the requested message to the requested destination node
			uint16_t destID;
			memcpy(&destID, &recvBuf[4], 2);
			short int destination = ntohs(destID);
			cout << "destination : " << destination << endl;

			cout << "In send: " << receiveBuf << endl;
			string data = receiveBuf.substr(6, receiveBuf.length()-6);
			cout << "DATA to send :" << data << endl;
			// create a data packet
			logLine = "receive packet message " + data + "\n";
			fprintf (logfile, "receive packet message %s \n", data.c_str());
			fflush (logfile);
			string msgtoforward = createDATAPacket(data, destination);

			// send the data out through nexthop
			logLine = "sending packet dest " + to_string(destination) + " nexthop " + to_string(nexthop[destination]) +" message " + data + "\n";
			fprintf (logfile, "sending packet dest %d nexthop %d message %s \n", destination, nexthop[destination], data.c_str());
			fflush (logfile);
			nexthop_lock.lock();
			sendto(globalSocketUDP, msgtoforward.c_str(), msgtoforward.length()+1, 0,
				  (struct sockaddr*)&globalNodeAddrs[nexthop[destination]], sizeof(globalNodeAddrs[nexthop[destination]]));
			nexthop_lock.unlock();
		}
		//'cost'<4 ASCII bytes>, destID<net order 2 byte signed> newCost<net order 4 byte signed>
		else if(!strncmp(receiveBuf.c_str(), "cost", 4))
		{
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
				topology_lock.lock();
				topology[globalMyID].remove(to_update);
				topology[globalMyID].push_back(new_data);
				topology_lock.unlock();
				string LSPPacket = createLSPPacket();
				thread Dj_thread(fillMatrix);
				Dj_thread.detach();
				cout << "LSP PACKET CREATED after cost change: " << LSPPacket << endl;
				thread LSP_broadcast_thread(hackyBroadcast2, LSPPacket.c_str(), LSPPacket.length()+1);
				LSP_broadcast_thread.detach();

			}
			else
				neighborsPresent_lock.unlock();
		}
		else if(!strncmp(receiveBuf.c_str(), "DATA", 4)){
			// call messsage handler
			//cout << "in lsp" << endl;
			handle_DATA_msg(receiveBuf, msgfrom);
		}
	}
	//(should never reach here)
	close(globalSocketUDP);
}
