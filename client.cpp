// client.cpp
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <sstream>
#include <fstream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;
using namespace chrono;

const char* MULTICAST_ADDR = "239.255.0.1";
const int MULTICAST_PORT = 50000;
const int RESPONSE_PORT = 50001;

string get_local_ip() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in serv{};
    serv.sin_family = AF_INET;
    serv.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &serv.sin_addr);
    connect(sock, (sockaddr*)&serv, sizeof(serv));
    sockaddr_in name{};
    socklen_t name_len = sizeof(name);
    getsockname(sock, (sockaddr*)&name, &name_len);
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &name.sin_addr, buf, sizeof(buf));
    close(sock);
    return string(buf);
}

void listener_thread() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(MULTICAST_PORT);
    local_addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (sockaddr*)&local_addr, sizeof(local_addr));
    ip_mreq mreq{};
    inet_pton(AF_INET, MULTICAST_ADDR, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    char buf[1024];
    sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    while (true) {
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&sender, &sender_len);
        if (n <= 0) continue;
        buf[n] = '\0';
        istringstream iss(buf);
        string tag;
        int seq;
        if (!(iss >> tag >> seq) || tag != "DISCOVER") continue;
        unsigned long rx_bytes, tx_bytes;
        ifstream f_rx("/sys/class/net/eth0/statistics/rx_bytes"); f_rx >> rx_bytes; f_rx.close();
        ifstream f_tx("/sys/class/net/eth0/statistics/tx_bytes"); f_tx >> tx_bytes; f_tx.close();
        ostringstream ss;
        ss << "RESPONSE " << get_local_ip() << " ";
        char hbuf[64]; gethostname(hbuf, sizeof(hbuf));
        ss << hbuf << " " << rx_bytes << " " << tx_bytes;
        string resp = ss.str();
        sender.sin_port = htons(RESPONSE_PORT);
        sendto(sock, resp.c_str(), resp.size(), 0, (sockaddr*)&sender, sender_len);
    }
    close(sock);
}

int main() {
    cout << "Client IP: " << get_local_ip() << endl;
    cout << "Hostname    : ";
    char hbuf[64];
    gethostname(hbuf, sizeof(hbuf));
    cout << hbuf << endl;
    listener_thread();
    return 0;
}
