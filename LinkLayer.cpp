#include <iostream>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include "LinkLayer.h"
#include "constants.h"
#include "LinkLayer.h"

using namespace std;

LinkLayer::LinkLayer(phy_info localPhy, vector<itf_info> itfs) {
	this->localPhy = localPhy;
	this->itfs = itfs;
	localAI = new struct addrinfo;
	rcvSocket = createSocket(localPhy, localAI, true);

	// print link layer configuration
	cout << "===================================" << endl;
	cout << "Node Configuration" << endl;
	cout << "===================================" << endl;
	cout << "Local phy address : " << localPhy.ipAddr << endl;
	cout << "Local phy port    : " << localPhy.port << endl;
	for(vector<itf_info>::size_type i = 0; i != itfs.size(); i++){
		// initialize send socket locks
		pthread_mutex_t ssLock;
		ssLocks.push_back(ssLock);

		// print interface info
		cout << "-----------------------------------" << endl;
		cout << "Interface " << i << endl;
		cout << "-----------------------------------" << endl;
		cout << "Remote phy address : " << itfs[i].rmtPhy.ipAddr << endl;
		cout << "Remote phy port    : " << itfs[i].rmtPhy.port << endl;
		cout << "Local vip          : " << itfs[i].locAddr << endl;
		cout << "Remote vip         : " << itfs[i].rmtAddr << endl;
		cout << "MTU                : " << itfs[i].mtu << endl;
	}
	cout << "===================================" << endl;
}

/**
 * Print interfaces
 */
void LinkLayer::printInterfaces() {
	for(int i = 0; i < itfs.size(); i++){
		printf("%d\t%s\t%s\n", i+1, itfs[i].locAddr, (itfs[i].down) ? "down" : "up");
	}
}

/**
 * Returns the local IP address associated with the specified interface
 */
char* LinkLayer::getInterfaceAddr(int itf) {
	if (!itfNumValid(itf)) return NULL;
	char* addr = itfs[itf].locAddr;
	return addr;
}

/**
 * Returns ID of interface with local VIP matched addr
 * Returns -1 if no such interface exists
 */
int LinkLayer::getInterfaceID(u_int32_t addr) {
	for(int i = 0; i < itfs.size(); i++) {
		if (addr == inet_addr(getInterfaceAddr(i)))
			return i;
	}
	return -1;
}

/**
 * Returns true if the interface number is valid
 */
bool LinkLayer::itfNumValid(int itfNum) {
	bool isValid = (itfNum >= 0) && (itfNum < itfs.size());
	if (!isValid) cout << "Interface number " << itfNum << " is invalid" << endl;
	return isValid;
}

/**
 * Sends dataLen bytes of data over the interface specified by itfNum
 */
int LinkLayer::send(char* data, int dataLen, int itfNum) {
	if (!itfNumValid(itfNum)) return -1;

	if (itfs[itfNum].down) return -1;

	int sendSocket, bytesSent;
	struct addrinfo *aiDest = new addrinfo;

	// get remote phy info
	phy_info pinfo = itfs[itfNum].rmtPhy;

	// lock socket mutex
	pthread_mutex_lock(&ssLocks[itfNum]);
	sendSocket = createSocket(pinfo, aiDest, false);

	if ((bytesSent = sendto(sendSocket, data, dataLen, 0, aiDest->ai_addr, aiDest->ai_addrlen)) == -1) {
		perror("Send error:");
		return -1;
	}

	freeaddrinfo(aiDest);

	close(sendSocket);

	// unlock socket mutex
	pthread_mutex_unlock(&ssLocks[itfNum]);

	return bytesSent;
}

/**
 * Listen on UDP socket and copy data received to specified buffer
 * Return number of bytes received
 */
int LinkLayer::listen(char* buf, int bufLen) {
	int bytesRcvd;
	pthread_mutex_lock(&rsLock);
	if ((bytesRcvd = recvfrom(rcvSocket, buf, bufLen, 0, NULL, NULL)) == -1) {
		perror("Receive error:");
		return -1;
	}
	pthread_mutex_unlock(&rsLock);
	return bytesRcvd;
}

/**
 * Creates UDP socket and populates &aiRet with socket address info
 */
int LinkLayer::createSocket(phy_info phyInfo, struct addrinfo* aiRet, bool bindSock) {
	int sockfd, gai_ret;
	struct addrinfo aiHints, *aiList, *ai;

	// zero out address info hints structure
	memset(&aiHints, 0, sizeof aiHints);

	// populate address info hints
	aiHints.ai_family = AF_INET; // IPv4
	aiHints.ai_socktype = SOCK_DGRAM; // UDP

	if ((gai_ret = getaddrinfo(phyInfo.ipAddr, phyInfo.port, &aiHints, &aiList)) != 0) {
		perror("Get address info error:");
		return -1;
	}

	// loop through getaddrinfo() results. connect (and bind if flagged) to first one possible
	for(ai = aiList; ai != NULL; ai = ai->ai_next) {
		if ((sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
			perror("Socket creation error:");
			continue;
		}

		if (bindSock) {
			if (bind(sockfd, ai->ai_addr, ai->ai_addrlen) == -1) {
				close(sockfd);
				perror("Socking binding error:");
				continue;
			}
		}

		break;
	}

	if (ai == NULL) {
		printf("No valid address info structure found.\n");
		return -1;
	}

	// copy address info data
	*aiRet = *ai;

	// free address info list
	freeaddrinfo(aiList);

	return sockfd;
}
