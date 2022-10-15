#include <iostream>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "include/utils.h"

/**
 * @brief Continues parsing the line given from stdin, sending a
 *   "subscribe" message to the server.
 * 
 * @param tcp_socket the socket to send on 
 * @return int - the error code
 */
int send_subscribe_msg(const int tcp_socket) {
    // Grab the topic
    char *topic = strtok(NULL, WHITESPACE);
    if (topic == NULL || strlen(topic) > MAX_TOPIC_LEN) {
        fprintf(stderr, "Incorrect / no given topic.\n");
        return 0;
    }

    // Grab the sf
    char *sf = strtok(NULL, WHITESPACE);
    if (sf == NULL || (strcmp(sf, "0") != 0 && strcmp(sf, "1") != 0)) {
        fprintf(stderr, "Incorrect / no given SF.\n");
        return 0;
    }

    // Make sure there are no more parameters
    char *junk = strtok(NULL, WHITESPACE);
    if (junk != NULL) {
        fprintf(stderr, "Extra command arguments given.\n");
        return 0;
    }

    // Construct the message and send it
    client_to_server_msg msg;
    memset(&msg, 0, sizeof(client_to_server_msg));
    memcpy(msg.client_sub.command, SUB_CMD, strlen(SUB_CMD));
    memcpy(msg.client_sub.topic, topic, strlen(topic));
    memcpy(msg.client_sub.sf, sf, 1);

    msg.len = htons(sizeof(msg.client_sub) + 2);

    int n = send(tcp_socket, &msg, ntohs(msg.len), 0);
    if (n < 0) {
        fprintf(stderr, "Error subscribing to topic.\n");
        return -2;
    }

    fprintf(stdout, "Subscribed to topic.\n");


    return 0;
}

/**
 * Continues parsing the line given from stdin, sending an
 *   "unsubscribe" message to the server.
 * 
 * @param tcp_socket the socket to send on 
 * @return int - the error code
 */
int send_unsubscribe_msg(const int tcp_socket) {
    // Grab the topic
    char *topic = strtok(NULL, WHITESPACE);
    if (topic == NULL || strlen(topic) > MAX_TOPIC_LEN) {
        fprintf(stderr, "Incorrect / no given topic.\n");
        return 0;
    }

    // Make sure there are no more parameters
    char *junk = strtok(NULL, WHITESPACE);
    if (junk != NULL) {
        fprintf(stderr, "Extra command arguments given.\n");
        return 0;
    }

    // Construct the message and send it
    client_to_server_msg msg;
    memset(&msg, 0, sizeof(client_to_server_msg));
    memcpy(msg.client_unsub.command, UNSUB_CMD, strlen(UNSUB_CMD));
    memcpy(msg.client_unsub.topic, topic, strlen(topic));

    msg.len = htons(sizeof(msg.client_unsub) + 2);

    int n = send(tcp_socket, &msg, ntohs(msg.len), 0);
    if (n < 0) {
        fprintf(stderr, "Error unsubscribing from topic.\n");
        return -2;
    }

    fprintf(stdout, "Unsubscribed from topic.\n");

    return 0;
}

/**
 * @brief Handles input from stdin.
 * 
 * @param tcp_socket the socket towards the server
 * @return int - the error code
 */
int handle_stdin(const int tcp_socket) {
    // Read the message from stdin
    char buffer[BUFLEN + 1];
    memset(buffer, 0, BUFLEN + 1);
    fgets(buffer, BUFLEN, stdin);

    // Remove the final '\n'
    buffer[strlen(buffer) - 1] = '\0';

    // Parse the message
    char *cmd = strtok(buffer, WHITESPACE);
    if (cmd == NULL) {
        return 0;
    }

    // If the message is "exit", close the server
    if (strcmp(cmd, EXIT_CMD) == 0) {
        return -1;
    }

    // If the message is "subscribe", subscribe to the topic
    if (strcmp(cmd, SUB_CMD) == 0 || strcmp(cmd, SH_SUB_CMD) == 0 ) {
        send_subscribe_msg(tcp_socket);
        return 0;
    }

    // If the message is "unsubscribe", unsubscribe from the topic
    if (strcmp(cmd, UNSUB_CMD) == 0 || strcmp(cmd, SH_UNSUB_CMD) == 0) {
        send_unsubscribe_msg(tcp_socket);
        return 0;
    }

    // Otherwise, do nothing
    return 0;
}

/**
 * @brief Used for parsing an UDP message INT.
 * 
 * @param msg the UDP message received from the server
 * @param res the result
 */
void parse_int(const server_to_client_msg &msg, char *res) {
    // Convert the integer
    uint32_t integer = ntohl(msg.content.udp_int.data);

    // Write the integer
    if (msg.content.udp_int.sign == 0) {
        sprintf(res, "%u", integer);
    } else {
        sprintf(res, "-%u", integer);
    }
}

/**
 * @brief Used for parsing an UDP message SHORT_REAL.
 * 
 * @param msg the UDP message received from the server
 * @param res the result
 */
void parse_short_real(const server_to_client_msg &msg, char *res) {
    // Convert the short real
    uint16_t sh = ntohs(msg.content.udp_short_real.data);

    // Write the short real
    sprintf(res, "%.2f", (float)sh / 100.0f);
}

/**
 * @brief Used for parsing an UDP message FLOAT.
 * 
 * @param msg the UDP message received from the server
 * @param res the result
 */
void parse_float(const server_to_client_msg &msg, char *res) {
    // Convert the integer
    uint32_t integer = ntohl(msg.content.udp_float.data);

    // Get the power of 10 byte
    uint8_t pow_of_10 = msg.content.udp_float.pow_10;

    // Create the number
    float power = 1.0f;
    for (uint8_t i = 1; i <= pow_of_10; ++i) {
        power *= 10.0f;
    }

    float num = 1.0f * integer / power;

    // Write the float
    if (msg.content.udp_float.sign == 0) {
        sprintf(res, "%f", num);
    } else {
        sprintf(res, "-%f", num);
    }
}

/**
 * @brief Handles a single UDP message received from the server, parsing
 *   and then printing it to stdout.
 * 
 * @param msg the UDP message received from the server
 * @return int - the error code
 */
int handle_message(const server_to_client_msg &msg) {
    // Declare variables to be printed
    char *ip = inet_ntoa((in_addr){msg.ip});
    uint16_t port = ntohs(msg.port);

    char topic[MAX_TOPIC_LEN + 1];
    memset(topic, 0, MAX_TOPIC_LEN + 1);
    memcpy(topic, msg.topic, MAX_TOPIC_LEN);

    char data_type[12];
    memset(data_type, 0, 12);

    char content[MAX_CONTENT_LEN + 1];
    memset(content, 0, MAX_CONTENT_LEN + 1);

    // Create the message based on its data type
    switch (msg.data_type) {
        case UDP_INT:
            parse_int(msg, content);
            strcpy(data_type, UDP_INT_STR);
            break;

        case UDP_SHORT_REAL:
            parse_short_real(msg, content);
            strcpy(data_type, UDP_SHORT_REAL_STR);
            break;

        case UDP_FLOAT:
            parse_float(msg, content);
            strcpy(data_type, UDP_FLOAT_STR);
            break;

        case UDP_STRING:
            strncpy(content, msg.content.udp_string, MAX_CONTENT_LEN);
            strcpy(data_type, UDP_STRING_STR);
            break;
    }

    // Print the message to stdout
    fprintf(stdout, "%s:%hu - %s - %s - %s\n",
            ip, port, msg.topic, data_type, content);

    return 0;
}

/**
 * @brief Receives all UDP messages received from the server.
 * 
 * @param tcp_socket the socket towards the server
 * @return int - the error code
 */
int handle_tcp_socket(const int tcp_socket) {
    // Receive all the messages from the server
    std::vector<char *> messages;
    int err = recv_messages(tcp_socket, messages);
    if (err <= 0) {
        return -1;
    }

    // Handle each server message
    for (auto &msg : messages) {
        handle_message(*((server_to_client_msg *)msg));
        delete[] msg;
    }

    return 0;
}

int main(int argc, char **argv) {
    // Deactivate stdout buffer
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    // Extract the info from the command line arguments
    if (argc != 4) {
        fprintf(stderr, "Incorrect command arguments.\n");
        fprintf(stderr, "Usage: %s <ID_CLIENT> <SERVER_IP> <SERVER_PORT>\n",
            argv[0]);
        return -1;
    }

    if (strlen(argv[1]) > MAX_ID_LEN) {
        fprintf(stderr, "Client ID must be at most %d characters long.\n",
            MAX_ID_LEN);
        return -1;
    }

    if (!is_number(argv[3], strlen(argv[3]))) {
        fprintf(stderr, "Incorrect port %s.\n", argv[3]);
        fprintf(stderr, "Must be a positive number between 0 and 65535.\n");
        return -1;
    }

    char id[MAX_ID_LEN + 1];
    memset(id, 0, MAX_ID_LEN + 1);
    strcpy(id, argv[1]);

    uint16_t server_port = atoi(argv[3]);

    in_addr server_ip;
    int err = inet_aton(argv[2], &server_ip);
    if (err == 0) {
        fprintf(stderr, "Incorrect IP address.\n");
        return -1;
    }

    // Set the server address
    sockaddr_in server_address;
    memset((uint8_t *)&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = server_ip.s_addr;
    server_address.sin_port = htons(server_port);

    // Open the TCP socket
	const int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket == -1) {
        fprintf(stderr, "Error opening TCP socket.\n");
        return -1;
    }

    // Connect to the server
    err = connect(tcp_socket, (sockaddr *)&server_address,
        sizeof(server_address));
    if (err < 0) {
        fprintf(stderr, "Error connecting client to server.\n");
        return -1;
    }

    // Disable Nagle
    int enable = 0;
    if (setsockopt(tcp_socket, IPPROTO_TCP,
            TCP_NODELAY, &enable, sizeof(int)) < 0) {
        fprintf(stderr, "Error disabling Nagle.\n");
        return -1;
    }

    // Send the client ID to the server
    client_to_server_msg msg;
    memset(&msg, 0, sizeof(client_to_server_msg));

    memcpy(msg.client_id.id, id, MAX_ID_LEN);
    msg.len = htons(sizeof(msg.client_id) + 2);

    int n = send(tcp_socket, &msg, ntohs(msg.len), 0);
    if (n < 0) {
        fprintf(stderr, "Error sending ID to server.\n");
        return -1;
    }

    // Create and clear the read file descriptors
    fd_set read_fds;
    fd_set tmp_read_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_read_fds);

    // Add the TCP descriptor
    FD_SET(tcp_socket, &read_fds);

    // Add STDIN fd to read descriptors
    FD_SET(STDIN_FILENO, &read_fds);

    // Begin an infinite loop, holding the logic of the client
    while (true) {
        // Store the read fds in a temporary variable
        tmp_read_fds = read_fds;

        // Detect new changes to the read fds
        err = select(tcp_socket + 1, &tmp_read_fds, NULL, NULL, NULL);
        if (err < 0) {
            fprintf(stderr, "Error selecting the read file descriptors.\n");
            return -1;
        }

        // Save a variable which decides if the server should close
        bool should_close = false;

        // Go through each descriptor and check if it's set
        for (int fd = 0; fd <= tcp_socket; ++fd) {
            if (FD_ISSET(fd, &tmp_read_fds)) {
                // Check which descriptor is set
                if (fd == STDIN_FILENO) {
                    err = handle_stdin(tcp_socket);
                } else if (fd == tcp_socket) {
                    err = handle_tcp_socket(tcp_socket);
                }

                if (err < 0) {
                    // Close the client
                    should_close = true;
                    break;
                }
            }
        }

        // Check if the client should close
        if (should_close) {
            break;
        }
    }

    // Close the TCP socket
    close(tcp_socket);

    return 0;
}
