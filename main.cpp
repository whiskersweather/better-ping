#include <iostream>
#include <chrono>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <fstream> // For generating the DOT file

using namespace std;

// Define the ICMP Header
struct icmp_header {
    u_int8_t type;
    u_int8_t code;
    u_int16_t checksum;
    u_int16_t id;
    u_int16_t seq;
};

// Calculate the checksum
unsigned short checksum(void *b, int len) {
    unsigned short *buf = (unsigned short *)b;
    unsigned int sum = 0;
    unsigned short result;

    for (sum = 0; len > 1; len -= 2) {
        sum += *buf++;
    }

    if (len == 1) {
        sum += *(unsigned char *)buf;
    }

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

// Generate detailed Graphviz DOT file for visualization
void generateDetailedDotFile(const string &host, double rtt1, double rtt2, double rtt3, double rtt4, double distance_m) {
    ofstream dotFile("ping_flow.dot");

    dotFile << "digraph G {" << endl;
    dotFile << "    // Nodes" << endl;
    dotFile << "    source [label=\"Your Machine (Source)\\n(192.168.1.10)\", shape=box, style=filled, color=lightblue, fontsize=12];" << endl;
    dotFile << "    router1 [label=\"Router 1 (ISP Gateway)\\n(192.168.1.1)\", shape=ellipse, style=filled, color=lightgray, fontsize=10];" << endl;
    dotFile << "    router2 [label=\"Router 2 (Regional Router)\\n(10.0.0.1)\", shape=ellipse, style=filled, color=lightgray, fontsize=10];" << endl;
    dotFile << "    router3 [label=\"Router 3 (ISP Backbone)\\n(172.16.0.1)\", shape=ellipse, style=filled, color=lightgray, fontsize=10];" << endl;
    dotFile << "    cloud [label=\"Cloud (Datacenter)\\n(Cloud Provider IP)\", shape=rect, style=dashed, color=lightgreen, fontsize=10];" << endl;
    dotFile << "    destination [label=\"" << host << " (Destination)\\n(101.177.166.241)\", shape=box, style=filled, color=lightyellow, fontsize=12];" << endl;
    dotFile << endl;

    dotFile << "    // Edges (connections) with additional details" << endl;
    dotFile << "    source -> router1 [label=\"TTL=64, RTT=" << rtt1 << " ms\", color=blue, penwidth=2, fontsize=10];" << endl;
    dotFile << "    router1 -> router2 [label=\"TTL=63, RTT=" << rtt2 << " ms, Packet Loss=0%\", color=green, penwidth=2, fontsize=10];" << endl;
    dotFile << "    router2 -> router3 [label=\"TTL=62, RTT=" << rtt3 << " ms, Packet Loss=0%\", color=yellow, penwidth=2, fontsize=10];" << endl;
    dotFile << "    router3 -> cloud [label=\"TTL=61, RTT=" << rtt4 << " ms, Packet Loss=0%\", color=orange, penwidth=2, fontsize=10];" << endl;
    dotFile << "    cloud -> destination [label=\"TTL=60, RTT=" << rtt4 + 10 << " ms, Packet Loss=0%\", color=red, penwidth=2, fontsize=10];" << endl;
    dotFile << endl;

    dotFile << "    // Optional: Show Latency on each path (more detailed latency)" << endl;
    dotFile << "    router1 -> router2 [label=\"Latency=20ms\", color=blue, style=dotted, fontsize=8];" << endl;
    dotFile << "    router2 -> router3 [label=\"Latency=25ms\", color=green, style=dotted, fontsize=8];" << endl;
    dotFile << "    router3 -> cloud [label=\"Latency=30ms\", color=yellow, style=dotted, fontsize=8];" << endl;
    dotFile << "    cloud -> destination [label=\"Latency=35ms\", color=red, style=dotted, fontsize=8];" << endl;

    dotFile << "    // Display distance as additional info" << endl;
    dotFile << "    distance [label=\"Distance to Target: " << distance_m << " meters\\n(" << distance_m / 1000 << " kilometers)\\n(" << distance_m * 3.28084 << " feet)\", shape=plaintext, color=black, fontsize=10];" << endl;
    dotFile << "    source -> distance [style=dashed];" << endl;
    dotFile << "}" << endl;

    dotFile.close();
}

void ping(const string &host, int retries, double signal_speed) {
    struct sockaddr_in dest_addr;
    struct icmp_header icmp_hdr;
    struct timespec start_time, end_time;

    // Resolve the host to an IP address
    struct hostent *he = gethostbyname(host.c_str());
    if (he == NULL) {
        cerr << "Host resolution failed for: " << host << endl;
        return;
    }

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = 0;
    dest_addr.sin_addr = *(struct in_addr *)he->h_addr_list[0];

    icmp_hdr.type = ICMP_ECHO;
    icmp_hdr.code = 0;
    icmp_hdr.id = getpid();
    icmp_hdr.seq = 0;
    icmp_hdr.checksum = 0;

    // Start retry loop
    double rtt_values[4] = {0};
    double distance_meters = 0;

    for (int attempt = 1; attempt <= retries; ++attempt) {
        int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (sockfd < 0) {
            perror("Socket creation failed");
            return;
        }

        icmp_hdr.seq++;
        icmp_hdr.checksum = 0;
        icmp_hdr.checksum = checksum(&icmp_hdr, sizeof(icmp_hdr));

        clock_gettime(CLOCK_MONOTONIC, &start_time);

        if (sendto(sockfd, &icmp_hdr, sizeof(icmp_hdr), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
            perror("Send failed");
            close(sockfd);
            return;
        }

        // Wait for reply
        char buffer[1024];
        struct sockaddr_in from_addr;
        socklen_t addr_len = sizeof(from_addr);

        int len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&from_addr, &addr_len);
        clock_gettime(CLOCK_MONOTONIC, &end_time);

        if (len < 0) {
            cerr << "Ping timeout for " << host << endl;
            close(sockfd);
            continue;
        }

        // RTT calculation
        double rtt = (end_time.tv_sec - start_time.tv_sec) * 1000.0;
        rtt += (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;

        // Calculate one-way distance (meters)
        double one_way_time = rtt / 2.0;
        distance_meters = one_way_time * signal_speed; // in meters

        rtt_values[attempt - 1] = rtt;

        close(sockfd);
    }

    // Print results
    cout << "Ping statistics for " << host << ":" << endl;
    cout << "Round-Trip Time (RTT): " << rtt_values[0] << " ms" << endl;
    cout << "One-Way Time: " << (rtt_values[0] / 2.0) << " ms" << endl;
    cout << "Distance to Target (in kilometers): " << distance_meters / 1000 << " km" << endl;
    cout << "Distance to Target (in meters): " << distance_meters << " m" << endl;
    cout << "Distance to Target (in feet): " << distance_meters * 3.28084 << " ft" << endl;
    cout << "Distance to Target (in miles): " << distance_meters / 1609.34 << " miles" << endl;

    // Generate a detailed Graphviz DOT file
    generateDetailedDotFile(host, rtt_values[0], rtt_values[1], rtt_values[2], rtt_values[3], distance_meters);
}

int main() {
    string host = "google.com";
    int retries = 4;
    double signal_speed = 180000.0; // Speed of light in fiber-optic cables (km/s)

    cout << "Enter the host to ping (default is google.com): ";
    getline(cin, host);

    ping(host, retries, signal_speed);

    return 0;
}
