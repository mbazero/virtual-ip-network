#include <vector>
#include <queue>
#include <map>
#include <pthread.h>
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
#include "ipsum.h"

#include "ip.h"
//#include <netinet/ip.h>

#include "IPLayer.h"

#define MAX_MSG_LEN (512)
#define HDR_SIZE sizeof(struct iphdr)

using namespace std;

IPLayer::IPLayer(LinkLayer* link) {
	linkLayer = link;

	// create thread to handle forwarding tasks
	pthread_t* fwdWorker;
	ipl_thread_pkg* pkg = new ipl_thread_pkg;
	pkg->ipl = this;
	pkg->toRun = "forwarding";
	int err = 0;
	//int err = pthread_create(fwdWorker, NULL, runThread, this);
	if(err != 0) {
			perror("Threading error:");
	}
}

static void IPLayer::runThread(ipl_thread_pkg pkg) {
	IPLayer ipl = pkg.ipl;
	string toRun = pkg.toRun;

	if (toRun == "forwarding") {
		ipl->runForwarding();
	} else if (toRun == "routing") {
		ipl->runRouting();
	}
}

void IPLayer::runForwarding() {
	int rcvLen;
	char buf[MAX_MSG_LEN];

	while(1) {
		// get packet
		rcvLen = linkLayer->listen(buf, MAX_MSG_LEN);
		if (rcvLen < 0) {
			printf("IP Layer receive error.");
			continue;
		} else {
			printf("IP packet received.");
		}

		//TODO spawn new thread here
		handleNewPacket(buf, rcvLen);

		memset(buf, 0, sizeof(buf));
	}
}

void IPLayer::runRouting() {
}

void IPLayer::handleNewPacket(char* packet, int len) {
	int fwdItf;
	int checksum;
	struct iphdr* hdr;

	// parse header
	hdr = (struct iphdr*) &packet; //TODO might want to deal with network ordering issues

	// check to make sure receive length equals header total length
	if (hdr->tot_len != len) {
		printf("Partial packet recieved, discarding.");
		return;
	}

	// verfiy checksum
	if (hdr->check != ip_sum(packet, sizeof(struct iphdr))) {
		printf("Invalid checksum, discarding packet.");
		return;
	}

	// decrement ttl or return if zero
	if (hdr->ttl == 0) {
		printf("TTL = 0, discarding packet.");
		return;
	} else { // decrement ttl and recalculate checksum
		hdr->ttl--;
		hdr->check = ip_sum(packet, sizeof(struct iphdr));
	}

	// forward or deliver locally
	if ((fwdItf = getFwdInterface(hdr->daddr)) == -1) {
		deliverLocal(packet);
	} else {
		linkLayer->send(packet, hdr->tot_len, fwdItf);
	}

}

struct iphdr* IPLayer::parseHeader(char* packet) {
	struct iphdr* hdr = (struct iphdr*) &packet;
	return hdr;
}

/**
 * Returns the next string in the receive buffer
 */
string IPLayer::getData() {
	string data = rcvQueue.front();
	rcvQueue.pop();
	return data;
}

/**
 * Return true if the IP layer has buffered data
 */
bool IPLayer::hasData() {
	return (rcvQueue.size() > 0);
}

/**
 * Copies packet data into receive queue and makes it available for retreival by the application layer
 */
void IPLayer::deliverLocal(char* packet) {
	struct iphdr* hdr = (struct iphdr*) &packet;

	// computer data length
	int dataLen = hdr->tot_len - (hdr->ihl * 4);

	// copy data into string
	string data (packet, HDR_SIZE, dataLen);

	// add data buffer to data vector
	rcvQueue.push(data);
}

/**
 * Encapsulates data in IP header and sends via link layer
 */
int IPLayer::send(char* data, int dataLen, char* destIP) {
	int bytesSent, itfNum;
	u_int32_t daddr, saddr;
	struct iphdr* hdr;

	// initialize buffer to store new packet
	int packetLen = dataLen + sizeof(struct iphdr);
	char packet[packetLen];

	// convert destination ip in dots-and-number form to network order int form
	daddr = inet_addr(destIP);

	// get forwarding interface
	if ((itfNum = getFwdInterface(daddr)) < 0) {
		printf("Source address belongs to host. Aborting send.");
		return -1;
	}

	// get local IP address associated with interface in network order int form
	saddr = inet_addr(linkLayer->getInterfaceAddr(itfNum));

	// generate new IP header
	hdr = genHeader(dataLen, saddr, daddr);

	// copy header to packet buffer
	memcpy(&packet[0], hdr, sizeof(struct iphdr));

	// copy data to packet buffer
	memcpy(&packet[sizeof(struct iphdr)], data, dataLen);

	// send packet via link layer
	if((bytesSent = linkLayer->send(packet, packetLen, itfNum)) < 0) {
		printf("Sending error.");
		return -1;
	} else {
		printf("IP packet length = %d\n Sent %d bytes", packetLen, bytesSent);
	}

	return bytesSent;
}

/**
 * Returns pointer to newly populated IP header
 */
struct iphdr* IPLayer::genHeader(int dataLen, u_int32_t saddr, u_int32_t daddr) {
	struct iphdr* hdr = new struct iphdr;

	// pack header
	hdr->version = 4; // IP version 4
	hdr->ihl = 5; // no options
	hdr->tos = 0; // no TOS protocol
	hdr->tot_len = (hdr->ihl * 4) + dataLen; // header length (in bytes) + data length
	hdr->id = 0; // fragmentation not supported
	hdr->frag_off = 0; // fragmentation not supported
	hdr->ttl = MAX_TTL; // maximum TTL
	hdr->protocol = 143; // custom protocol (raw string data)
	hdr->check = 0; // checksum is zero for calculation
	hdr->saddr = saddr; // source address in network byte order
	hdr->daddr = daddr; // destination address in network byte order

	// calculate checksum
	hdr->check = ip_sum((char*) hdr, sizeof(struct iphdr));

	return hdr;
}

/**
 * Gets the interface number to use for forwarding the given IP address.
 * Returns -1 if the address matches one of the interface addresses.
 */
int IPLayer::getFwdInterface(u_int32_t daddr) {
	// check if network number is equal to the destination of any of the interfaces

	// check if network num is in forwarding table
	int itfNum = fwdTable[daddr];

	// return default interface if no matches found

	return itfNum;
}

