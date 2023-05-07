#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <iterator>
#include <string.h>
// #include <bits/stdc++.h>

using namespace std;

#define BUFFER_SIZE 1024
#define EPOLL_SIZE 100
#define RESPONSE "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\nHello, world!\r\n"



const int PORT = 8000;
const std::string SERVER_IP = "127.0.0.1";
const int SERVER_PORT = 4000;

class Server{
    public:
        int server_socket;
        sockaddr_in server_address;
};

unordered_map<int, Server> servers;


int create_socket()
{
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    return server_socket;
}

void set_socket_options(int server_socket)
{
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
}

void bind_socket(int server_socket, const struct sockaddr_in *server_address)
{
    if (bind(server_socket, (struct sockaddr *)server_address, sizeof(*server_address)) == -1)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }
}

void listen_socket(int server_socket)
{
    if (listen(server_socket, 5) == -1)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }
}

void set_non_blocking(int socket)
{
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1)
    {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }
    if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }
}

void add_epoll_event(int epoll_fd, int socket_fd)
{
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = socket_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &event) == -1)
    {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }
}

bool is_server_up(Server server)
{
    fd_set read_fds, write_fds, except_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&except_fds);
    FD_SET(server.server_socket, &read_fds);
    FD_SET(server.server_socket, &write_fds);
    FD_SET(server.server_socket, &except_fds);
    struct timeval timeout = {0, 0};

    if (select(server.server_socket, &read_fds, &write_fds, &except_fds, &timeout) > 0)
    {
        cout << "server is available..." << endl;
        return true;
    }
    else
    {
        cout << "server unavailable" << endl;
        return false;
    }
}

void handle_request(int client_socket)
{
    char buffer[BUFFER_SIZE];
    if (read(client_socket, buffer, BUFFER_SIZE) < 0)
    {
        perror("read");
        close(client_socket);
        return;
    }

    std::string request(buffer);
    std::string request_line = request.substr(0, request.find("\r\n"));
    std::istringstream iss(request_line);
    std::vector<std::string> tokens;
    std::copy(std::istream_iterator<std::string>(iss),
              std::istream_iterator<std::string>(),
              std::back_inserter(tokens));
    cout << 1 << request_line <<": " << tokens[1] << endl;
    std::string path = tokens[1];

    if (path == "/")
    {
        const char *response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!\n";
        if (write(client_socket, response, strlen(response)) < 0)
        {
            perror("write");
        }
    }
    else if(path.rfind("/number", 0) == 0){
        size_t num_start = path.find('=');
        size_t num_end = path.find('&', num_start + 1);
        cout << num_start << " " << path.length() << " "<< stoi(path.substr(num_start + 1, path.length())) << endl;
    }
    else if(path == "/test"){

        if (is_server_up(servers[4000]))
        {
            const char *request = "GET / HTTP/1.1\r\nHost: 127.0.0.1:4000\r\n\r\n";
            int request_len = strlen(request);
            int bytes_sent = send(servers[4000].server_socket, request, request_len, 0);
            if (bytes_sent != request_len)
            {
                cerr << "Failed to send request to server\n";
                perror("write");
            }

            char response_buffer[BUFFER_SIZE];
            int bytes_received = 0;
            int total_bytes_received = 0;
            while ((bytes_received = recv(servers[4000].server_socket, response_buffer, BUFFER_SIZE - 1, 0)) > 0)
            {
                total_bytes_received += bytes_received;
                response_buffer[bytes_received] = '\0';
                cout << "received: " << response_buffer;
            }
        }
        else{
            cout << "request failed" << endl;
            perror("write");
        }
    }
    else
    {
        const char *response = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
        if (write(client_socket, response, strlen(response)) < 0)
        {
            perror("write");
        }
    }
    close(client_socket);
}


void run_server()
{
    int server_socket = create_socket();
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    set_socket_options(server_socket);
    bind_socket(server_socket, &server_address);
    listen_socket(server_socket);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
    {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    add_epoll_event(epoll_fd, server_socket);

    struct epoll_event events[EPOLL_SIZE];
    int event_count;

    while (true)
    {
        event_count = epoll_wait(epoll_fd, events, EPOLL_SIZE, 1000);
        if (event_count == -1)
        {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < event_count; i++)
        {
            if (events[i].data.fd == server_socket)
            {
                struct sockaddr_in client_address;
                socklen_t client_address_size = sizeof(client_address);
                int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_size);
                if (client_socket == -1)
                {
                    perror("accept");
                }
                else
                {
                    set_non_blocking(client_socket);
                    add_epoll_event(epoll_fd, client_socket);
                }
            }
            else
            {
                handle_request(events[i].data.fd);
            }
        }

        // if (is_server_up())
        // {
        //     std::cout << "Other server is up" << std::endl;
        // }
        // else
        // {
        //     std::cout << "Other server is down" << std::endl;
        // }
    }

    close(server_socket);
}

int main()
{
    int server_socket = create_socket();
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    server_address.sin_addr.s_addr = inet_addr(SERVER_IP.c_str());

    Server s1;
    s1.server_socket = server_socket;
    s1.server_address = server_address;
    servers[4000] = s1;
    connect(s1.server_socket, (struct sockaddr *)&s1.server_address, sizeof(s1.server_address));
    run_server();
    return 0;
}