// server.cpp
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;
using namespace std::chrono;

struct ClientInfo {
    int id = 0;
    string ip;
    string clientname;
    unsigned long net_rx = 0;
    unsigned long net_tx = 0;
    steady_clock::time_point last_seen;
};

map<string, ClientInfo> clients;
mutex client_mutex;

const char* MULTICAST_ADDR = "239.255.0.1";
const int DISCOVER_PORT = 50000;
const int RESPONSE_PORT = 50001;
const int CLIENT_TIMEOUT_S = 15;

int sock_send;
sockaddr_in mcast_addr;
string server_ip;
steady_clock::time_point server_start;
atomic<int> seq_counter{0};
atomic<bool> reset_timer(false);
atomic<int> next_client_id{1};

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

void send_discover() {
    int s = seq_counter.fetch_add(1);
    string msg = "DISCOVER " + to_string(s);
    sendto(sock_send, msg.c_str(), msg.size(), 0,
           (const sockaddr*)&mcast_addr, sizeof(mcast_addr));
}

void receive_loop(int recv_sockfd) {
    char buf[1024];
    sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    while (true) {
        ssize_t len = recvfrom(recv_sockfd, buf, sizeof(buf) - 1, 0,
                               (sockaddr*)&sender, &sender_len);
        if (len <= 0) continue;
        buf[len] = '\0';
        istringstream iss(buf);
        string tag, client_ip, container_id;
        unsigned long net_rx_bytes, net_tx_bytes;
        if (!(iss >> tag
                  >> client_ip
                  >> container_id
                  >> net_rx_bytes
                  >> net_tx_bytes))
            continue;
        if (tag != "RESPONSE") continue;
        {
            lock_guard<mutex> lock(client_mutex);
            auto& ci = clients[container_id];
            if (ci.id == 0) ci.id = next_client_id.fetch_add(1);
            ci.ip = client_ip;
            ci.clientname = container_id;
            ci.net_rx = net_rx_bytes;
            ci.net_tx = net_tx_bytes;
            ci.last_seen = steady_clock::now();
        }
    }
}

void print_clients_loop() {
    while (true) {
        this_thread::sleep_for(seconds(1));
        system("clear");
        vector<pair<string, ClientInfo>> all;
        {
            lock_guard<mutex> lock(client_mutex);
            for (auto& kv : clients)
                all.emplace_back(kv.first, kv.second);
        }
        auto now = steady_clock::now();
        vector<pair<string, ClientInfo>> active, inactive;
        for (auto& p : all) {
            auto age = duration_cast<seconds>(now - p.second.last_seen).count();
            if (age <= CLIENT_TIMEOUT_S) active.push_back(p);
            else inactive.push_back(p);
        }
        int online = active.size();
        int total = all.size();
        auto uptime_s = duration_cast<seconds>(now - server_start).count();
        int hrs = uptime_s / 3600;
        int mins = (uptime_s % 3600) / 60;
        int secs = uptime_s % 60;
        ostringstream uptime_ss;
        uptime_ss << setfill('0') << setw(2) << hrs << ":"
                  << setw(2) << mins << ":" << setw(2) << secs;
        cout << left << "Online: " << online << "/" << total
             << "    Server IP: " << server_ip
             << "    Uptime: " << uptime_ss.str() << "\n\n";
        cout << left
             << setw(6) << "ID"
             << setw(16) << "IP"
             << setw(14) << "last_seen"
             << setw(16) << "HostName"
             << setw(12) << "RX(bytes)"
             << setw(12) << "TX(bytes)"
             << "\n";
        cout << string(6 + 16 + 14 + 16 + 12 + 12, '-') << "\n";
        auto print_block = [&](auto& vec, const char* color) {
            for (auto& p : vec) {
                const auto& ci = p.second;
                int age = duration_cast<seconds>(now - ci.last_seen).count();
                cout << color << left
                     << setw(6) << ci.id
                     << setw(16) << ci.ip
                     << setw(14) << to_string(age)
                     << setw(16) << ci.clientname
                     << setw(12) << ci.net_rx
                     << setw(12) << ci.net_tx
                     << "\033[0m\n";
            }
        };
        print_block(active, "\033[32m");
        print_block(inactive, "\033[37m");
    }
}

int main() {
    server_start = steady_clock::now();
    server_ip = get_local_ip();
    sock_send = socket(AF_INET, SOCK_DGRAM, 0);
    unsigned char loop = 1;
    setsockopt(sock_send, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    unsigned char ttl = 1;
    setsockopt(sock_send, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_port = htons(DISCOVER_PORT);
    inet_pton(AF_INET, MULTICAST_ADDR, &mcast_addr.sin_addr);
    int sock_recv = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in recv_addr{};
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(RESPONSE_PORT);
    recv_addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock_recv, (sockaddr*)&recv_addr, sizeof(recv_addr));
    thread recv_thr(receive_loop, sock_recv);
    thread print_thr(print_clients_loop);
    while (true) {
        send_discover();
        int waited = 0;
        while (waited < 10000) {
            this_thread::sleep_for(milliseconds(100));
            waited += 100;
            if (reset_timer.exchange(false)) break;
        }
    }
    recv_thr.join();
    print_thr.join();
    close(sock_send);
    return 0;
}
