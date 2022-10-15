#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <queue>
#include <vector>
#include <memory>
#include <unordered_map>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "include/utils.h"
#include "include/defines.h"

struct client {
    std::string id;
    int fd;
    std::queue<std::shared_ptr<server_to_client_msg>> messages_to_receive;
};

struct client_info {
    int fd;
    char ip[MAX_IP_LEN + 1];
    uint16_t port;
};

struct subscription {
    client *subbed_client;
    bool sf;
};

struct topic {
    std::unordered_map<std::string, subscription> subscriptions;
};

class Server {
    // Create a map from a file descriptor to a client
    std::unordered_map<int, client *> fd_to_client;

    // Create a map from a client ID to the client
    std::unordered_map<std::string, client *> id_to_client;

    // Create a set of descriptors that need to be initialized
    std::unordered_map<int, client_info *> uninitialized_fds;

    // Create a map from a topic name to the actual topic
    std::unordered_map<std::string, topic> name_to_topic;

    /**
     * @brief Sends a given message to the client.
     * 
     * @param client_fd the client to send to
     * @param msg the message to send
     * @return int - the error code
     */
    int send_to_client(const int client_fd, const server_to_client_msg *msg) {
        // Send the message to the client
        int err = send(client_fd, (char *)msg, ntohs(msg->len), 0);
        if (err < 0) {
            fprintf(stderr, "Error sending message to client.\n");
            return -1;
        }

        return 0;
    }

    /**
     * @brief Subscribes the client to the given topic with the given sf.
     * 
     * @param client_fd the client to subscribe
     * @param topic_name the topic to subscribe to
     * @param sf the store & forward value
     * @return int - the error code
     */
    int subscribe_client(const int client_fd,
            const std::string &topic_name, const bool sf) {
        // Get the client ID
        std::string &client_id = fd_to_client[client_fd]->id;

        // Attempt to find the client
        auto &topic_subs = name_to_topic[topic_name].subscriptions;
        if (topic_subs.find(client_id) != topic_subs.end()) {
            // The client is already subscribed to the topic, update sf value
            topic_subs[client_id].sf = sf;

            return 0;
        }

        // If the client is not already subscribed, subscribe him
        topic_subs[client_id] = {fd_to_client[client_fd], sf};
        return 0;
    }

    /**
     * @brief Unsubscribes the client from the given topic.
     * 
     * @param client_fd the client to unsubscribe
     * @param topic_name the topic to unsubscribe from
     * @return int - the error code
     */
    int unsubscribe_client(const int client_fd,
            const std::string &topic_name) {
        // Get the client ID
        std::string &client_id = fd_to_client[client_fd]->id;

        // Attempt to find the client
        auto &topic_subs = name_to_topic[topic_name].subscriptions;
        if (topic_subs.find(client_id) == topic_subs.end()) {
            // The client is NOT subscribed to the topic, do nothing
            return -1;
        }
        
        // Otherwise, delete him
        topic_subs.erase(client_id);

        return 0;
    }

    /**
     * @brief Initializes the client.
     * 
     * @param client_fd the client's file descriptor
     * @param client_id the client's ID
     * @return client* - a pointer to the client
     */
    client *initialize_client(const int client_fd,
            const std::string &client_id) {
        // Try to find if the client ID already exists
        if (id_to_client.find(client_id) != id_to_client.end()) {
            // Set the file descriptor
            client *cl = id_to_client[client_id];
            cl->fd = client_fd;

            // Send all the messages in the queue
            while (!cl->messages_to_receive.empty()) {
                // Get the message at the front of the queue and send it
                auto &&msg = cl->messages_to_receive.front();
                send_to_client(client_fd, msg.get());

                // Pop the message from the queue
                cl->messages_to_receive.pop();
            }

            return id_to_client[client_id];
        }

        // Otherwise, create a new client
        client *new_client = new client;
        new_client->id = std::string(client_id);
        new_client->fd = client_fd;

        id_to_client[client_id] = new_client;
        return new_client;
    }

    /**
     * @brief Disconnects the client.
     * 
     * @param client_to_disconnect - the client to disconnect
     */
    void disconnect_client(client *client_to_disconnect) {
        client_to_disconnect->fd = -1;
    }

    /**
     * @brief Handles input given in the standard input.
     * 
     * @return int - the error code
     */
    int handle_stdin() {
        // Read the message from stdin
        char buffer[BUFLEN + 1];
        fgets(buffer, BUFLEN, stdin);

        // Remove the final '\n'
        buffer[strlen(buffer) - 1] = '\0';

        // If the message is "exit", close the server
        if (strcmp(buffer, EXIT_CMD) == 0) {
            return -1;
        }

        // Otherwise, do nothing
        return 0;
    }

    /**
     * @brief Handles everything received on the TCP socket (clients wanting
     *   to connect to the server).
     * 
     * @param tcp_fd the tcp socket
     * @return client_info* - information about the client wanting to connect
     */
    client_info* handle_tcp_socket(const int tcp_fd) {
        // Declare variables to store client information
        sockaddr_in client_address;
        socklen_t client_length = sizeof(client_address);

        // Accept the client
        int client_socket = accept(tcp_fd, (struct sockaddr *)&client_address,
            &client_length);
        if (client_socket < 0) {
            fprintf(stderr, "Error accepting client.\n");
            return NULL;
        }

        // Construct new client info
        client_info *info = (client_info *)malloc(1 * sizeof(client_info));
        if (!info) {
            fprintf(stderr, "Error allocating memory for client info.\n");
            close(client_socket);
            return NULL;
        }

        info->fd = client_socket;

        char *client_addr = inet_ntoa(client_address.sin_addr);
        strcpy(info->ip, client_addr);

        info->port = ntohs(client_address.sin_port);

        return info;
    }

    /**
     * @brief Handles everything received on the UDP socket
     *   (messages received from the UDP clients).
     * 
     * @param udp_fd the UDP socket
     * @return int - the error code
     */
    int handle_udp_socket(const int udp_fd) {
        // Declare client information
        sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);

        // Receive a message from the UDP clients
        udp_to_server_msg received_msg;
        memset(&received_msg, 0, sizeof(received_msg));
        int n = recvfrom(udp_fd, &received_msg, sizeof(received_msg), 0,
            (sockaddr *)&client_address, &client_len);
        if (n < 0) {
            return -1;
        }

        // Create the message to send to the client
        std::shared_ptr<server_to_client_msg> msg_to_send(
            new server_to_client_msg
        );
        memset(msg_to_send.get(), 0, sizeof(*msg_to_send));
        memcpy(&msg_to_send->ip, (char *)&client_address.sin_addr, 4);
        memcpy(&msg_to_send->port, (char *)&client_address.sin_port, 2);

        // Extract the message info
        memcpy(msg_to_send->topic, received_msg.topic, MAX_TOPIC_LEN);
        msg_to_send->data_type = received_msg.data_type;
        memcpy(msg_to_send->content.udp_string,
            received_msg.content, MAX_CONTENT_LEN);

        // Calculate the message length
        int content_len = 0;
        switch (msg_to_send->data_type) {
            case UDP_INT:
                content_len = sizeof(msg_to_send->content.udp_int);
                break;

            case UDP_SHORT_REAL:
                content_len = sizeof(msg_to_send->content.udp_short_real);
                break;

            case UDP_FLOAT:
                content_len = sizeof(msg_to_send->content.udp_float);
                break;

            case UDP_STRING:
                content_len = strnlen(received_msg.content,
                    MAX_CONTENT_LEN - 1) + 1;
                break;
        }

        // Update the message's length
        msg_to_send->len = htons(UDP_HDR_LEN + content_len);

        // Search for the topic and go through all subscribers
        for (auto& subscription_entry :
                name_to_topic[std::string(msg_to_send->topic)].subscriptions) {
            // Get a reference to the subscription
            auto &sub = subscription_entry.second;

            // Get the client's fd
            int client_fd = sub.subbed_client->fd;

            // Check if the client is connected
            if (client_fd != -1) {
                // Send the message and go to the next subscriber
                send_to_client(client_fd, msg_to_send.get());
                continue;
            }

            // Otherwise, check the SF flag
            // If it's 1, add the message to the client's queue
            if (sub.sf == 1) {
                sub.subbed_client->messages_to_receive.push(msg_to_send);
            }
        }

        return 0;
    }

    /**
     * @brief Handles a single message received from the given client.
     * 
     * @param msg the message received from the client
     * @param read_fds the read descriptors
     * @param client_fd the client's descriptor
     * @return int - the error code
     */
    int handle_client_message(const client_to_server_msg* msg,
            fd_set *read_fds, const int client_fd) {
        // Check if the file descriptor is uninitialized
        if (uninitialized_fds.find(client_fd) != uninitialized_fds.end()) {
            // Save the client ID in a string
            std::string client_id(msg->client_id.id);

            // Check if a client with the same ID is already connected
            if (id_to_client.find(client_id) != id_to_client.end() &&
                    id_to_client[client_id]->fd != -1) {
                // Write a message to stdout
                fprintf(stdout, "Client %s already connected.\n",
                    client_id.c_str());

                // Disconnect the current client
                client_info *client = uninitialized_fds[client_fd];
                free(client);
                uninitialized_fds.erase(client_fd);

                close(client_fd);
                FD_CLR(client_fd, read_fds);
                return -1;
            }

            // Otherwise, display a connection successful message
            client_info *client = uninitialized_fds[client_fd];
            fprintf(stdout, "New client %s connected from %s:%hu.\n",
                msg->client_id.id, client->ip, client->port);

            // Initialize the client
            fd_to_client[client_fd] = initialize_client(client_fd, client_id);
            free(client);

            uninitialized_fds.erase(client_fd);
            return 0;
        }

        // Check if the client wants to subscribe to / unsubscribe from a topic
        if (strncmp(msg->client_sub.command,
                SUB_CMD, strlen(SUB_CMD)) == 0) {
            // Extract the topic and the SF flag
            std::string topic(msg->client_sub.topic);
            bool sf = atoi(msg->client_sub.sf);

            // Subscribe the client to the topic
            subscribe_client(client_fd, topic, sf);
        } else if (strncmp(msg->client_unsub.command,
                UNSUB_CMD, strlen(UNSUB_CMD)) == 0) {
            // Extract the topic
            std::string topic(msg->client_unsub.topic);

            // Unsubscribe the client from the topic
            unsubscribe_client(client_fd, topic);
        }

        return 0;
    }

    /**
     * @brief Handles all messages received from the given client.
     * 
     * @param read_fds - the read descriptors
     * @param client_fd - the client's descriptor
     * @return int - the error code
     */
    int handle_client(fd_set *read_fds, const int client_fd) {
        // Receive potentially multiple messages from the client
        std::vector<char *> messages;
        int n = recv_messages(client_fd, messages); 
        if (n < 0) {
            return -2;
        }

        if (n == 0) {
            // If nothing was received, disconnect the client
            if (fd_to_client.find(client_fd) == fd_to_client.end() ||
                    fd_to_client[client_fd] == NULL) {
                // The client was somehow not found
                fprintf(stderr, "Client not found in the clients list.\n");
                return -2;
            }

            // The client was found, disconnect him
            fprintf(stdout, "Client %s disconnected.\n",
                fd_to_client[client_fd]->id.c_str());

            disconnect_client(fd_to_client[client_fd]);
            fd_to_client.erase(client_fd);
            close(client_fd);
            FD_CLR(client_fd, read_fds);
            return -1;
        }

        // Handle each client message
        for (char *msg : messages) {
            handle_client_message((client_to_server_msg *)msg,
                read_fds, client_fd);
            delete[] msg;
        }

        return 0;
    }

    /**
     * @brief Checks if the descriptor is either
     *   for STDIN, TCP, UDP or clients.
     * 
     * @param fd the descriptor to check
     * @param tcp_socket the tcp socket
     * @param udp_socket the udp socket
     * @param fd_max the maximum descriptor, given as a reference
     * @param read_fds the read descriptors
     * @return int - the error code
     */
    int check_fd(const int fd, const int tcp_socket,
            const int udp_socket, int &fd_max, fd_set *read_fds) {
        // Declare a variable for return values
        int err;

        // Check for stdin
        if (fd == STDIN_FILENO) {
            err = handle_stdin();
            if (err < 0) {
                // Close the server
                return -2;
            }

            return 0;
        }
        
        // Check for the TCP socket
        if (fd == tcp_socket) {
            // Grab the client information
            client_info *new_client_info = handle_tcp_socket(tcp_socket);
            if (new_client_info == NULL) {
                // If the client could not be accepted, do nothing
                return -1;
            }

            // Mark the client as uninitialized
            uninitialized_fds[new_client_info->fd] = new_client_info;

            // Disable Nagle for the client's descriptor
            int enable = 0;
            if (setsockopt(new_client_info->fd, IPPROTO_TCP,
                    TCP_NODELAY, &enable, sizeof(int)) < 0) {
                fprintf(stderr, "Error disabling Nagle on Client.\n");
                return -1;
            }

            // Add it to the read descriptors and update the maximum descriptor
            FD_SET(new_client_info->fd, read_fds);
            fd_max = std::max(fd_max, new_client_info->fd);

            return 0;
        }
        
        // Check for the UDP socket
        if (fd == udp_socket) {
            handle_udp_socket(udp_socket);
            return 0;
        }
        
        // Check for client messages
        if (fd > STDERR_FILENO) {
            handle_client(read_fds, fd);
        }

        return 0;
    }

public:
    /**
     * @brief Initializes the server.
     * 
     * @param server_port the port on which to initialize
     * @return int - the error code
     */
    int init(const uint16_t server_port) {
        // Set the server address
        sockaddr_in server_address;
        memset((uint8_t *)&server_address, 0, sizeof(server_address));
        server_address.sin_family = AF_INET;
        server_address.sin_addr.s_addr = INADDR_ANY;
        server_address.sin_port = htons(server_port);

        // Open the TCP socket
        const int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_socket == -1) {
            fprintf(stderr, "Error opening TCP socket.\n");
            return -1;
        }

        // Set it as reusable
        int enable = 1;
        if (setsockopt(tcp_socket,
                SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
            fprintf(stderr, "Error setting TCP socket as reusable.\n");
            return -1;
        }

        // Disable Nagle
        enable = 0;
        if (setsockopt(tcp_socket,
                IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0) {
            fprintf(stderr, "Error disabling Nagle on TCP socket.\n");
            return -1;
        }

        // Open the UDP socket
        const int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_socket == -1) {
            fprintf(stderr, "Error opening UDP socket.\n");
            return -1;
        }

        // Bind the two sockets to the given port
        int err = bind(tcp_socket,
            (sockaddr *)&server_address, sizeof(sockaddr));
        if (err < 0) {
            fprintf(stderr, "Error binding TCP socket.\n");
            return -1;
        }

        err = bind(udp_socket,
            (sockaddr *)&server_address, sizeof(sockaddr));
        if (err < 0) {
            fprintf(stderr, "Error binding UDP socket.\n");
            return -1;
        }

        // Listen for the TCP clients
        err = listen(tcp_socket, MAX_PENDING_CLIENTS);
        if (err < 0) {
            fprintf(stderr, "Error listening on the TCP socket.\n");
            return -1;
        }

        // Create and clear the read file descriptors
        fd_set read_fds;
        fd_set tmp_read_fds;
        int fd_max = std::max(tcp_socket, udp_socket);
        FD_ZERO(&read_fds);

        // Add the TCP and UDP descriptors for accepting connections
        FD_SET(tcp_socket, &read_fds);
        FD_SET(udp_socket, &read_fds);

        // Add STDIN fd to read descriptors
        FD_SET(STDIN_FILENO, &read_fds);

        // Begin an infinite loop, holding the logic of the server
        while (true) {
            // Store the read fds in a temporary variable
            tmp_read_fds = read_fds;

            // Detect new changes to the read fds
            err = select(fd_max + 1, &tmp_read_fds, NULL, NULL, NULL);
            if (err < 0) {
                fprintf(stderr, "Error selecting the "
                    "read file descriptors.\n");
                return -1;
            }

            // Save a variable which decides if the server should close
            bool should_close = false;

            // Go through each descriptor and check if it's set
            for (int fd = 0; fd <= fd_max; ++fd) {
                if (FD_ISSET(fd, &tmp_read_fds)) {
                    err = check_fd(fd, tcp_socket,
                        udp_socket, fd_max, &read_fds);
                    if (err == -2) {
                        should_close = true;
                        break;
                    }
                }
            }

            // Check if the server should close
            if (should_close) {
                break;
            }
        }

        // Close all connections with clients
        for (auto &client_entry : fd_to_client) {
            close(client_entry.first);
        }

        // Close the TCP and UDP sockets
        close(tcp_socket);
        close(udp_socket);

        // Free the memory of all the clients
        for (auto client_entry : id_to_client) {
            delete client_entry.second;
        }

        return 0;
    }
};

int main(int argc, char **argv) {
    // Deactivate stdout buffer
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // Extract the port from the command line arguments
    if (argc != 2) {
        fprintf(stderr, "Incorrect command arguments.\n");
        fprintf(stderr, "Usage: %s <SERVER_PORT>\n", argv[0]);
        return -1;
    }

    if (!is_number(argv[1], strlen(argv[1]))) {
        fprintf(stderr, "Incorrect port %s.\n", argv[1]);
        fprintf(stderr, "Must be a positive number between 0 and 65535.\n");
        return -1;
    }

    uint16_t server_port = atoi(argv[1]);

    // Create a new server
    Server *server = new Server;
    if (!server) {
        fprintf(stderr, "Memory allocation for the server failed.\n");
        return -1;
    }

    // Initialize the server
    server->init(server_port);

    // Deallocate the server
    delete server;

    return 0;
}
