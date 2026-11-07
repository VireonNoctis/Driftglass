#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

int create_socket(int type) {
    const int descriptor = ::socket(AF_INET, type, 0);
    if (descriptor < 0) throw std::runtime_error("socket creation failed");
    return descriptor;
}

unsigned short bind_loopback(int descriptor, bool listen_socket) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        throw std::runtime_error("loopback socket bind failed");
    }
    if (listen_socket && ::listen(descriptor, 4) != 0) {
        throw std::runtime_error("loopback TCP listen failed");
    }
    socklen_t size = sizeof(address);
    if (::getsockname(descriptor, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
        throw std::runtime_error("getsockname failed");
    }
    return ntohs(address.sin_port);
}

} // namespace

int main() {
    int tcp = -1;
    int udp = -1;
    try {
        tcp = create_socket(SOCK_STREAM);
        udp = create_socket(SOCK_DGRAM);
        const auto tcp_port = bind_loopback(tcp, true);
        const auto udp_port = bind_loopback(udp, false);
        std::cout << tcp_port << " " << udp_port << std::endl;
        for (;;) ::pause();
    } catch (const std::exception& error) {
        std::cerr << "loopback fixture: " << error.what() << "\n";
        if (tcp >= 0) ::close(tcp);
        if (udp >= 0) ::close(udp);
        return EXIT_FAILURE;
    }
}
