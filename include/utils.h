#ifndef __UTILS_H_
#define __UTILS_H_

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <vector>
#include "defines.h"

/**
 * @brief Structure used to send UDP client -> server messages.
 * 
 */
struct udp_to_server_msg {
    char topic[MAX_TOPIC_LEN];
    uint8_t data_type;
    char content[MAX_CONTENT_LEN];
} __attribute__((packed));

/**
 * @brief Structure used to send server -> TCP client messages.
 * 
 */
struct server_to_client_msg {
    uint16_t len;

    uint32_t ip;
    uint16_t port;

    char topic[MAX_TOPIC_LEN];
    uint8_t data_type;
    union {
        struct {
            uint8_t sign;
            uint32_t data;
        } __attribute__((packed)) udp_int;

        struct {
            uint16_t data;
        } __attribute__((packed)) udp_short_real;

        struct {
            uint8_t sign;
            uint32_t data;
            uint8_t pow_10;
        } __attribute__((packed)) udp_float;

        char udp_string[MAX_CONTENT_LEN];
    } content;
} __attribute__((packed));

/**
 * @brief Structure used to send TCP client -> server messages.
 * 
 */
struct client_to_server_msg {
    uint16_t len;

    union {
        struct {
            char id[MAX_ID_LEN + 1];
        } __attribute__((packed)) client_id;

        struct {
            char command[MAX_COMM_LEN + 1];
            char topic[MAX_TOPIC_LEN + 1];
            char sf[2];
        } __attribute__((packed)) client_sub;

        struct {
            char command[MAX_COMM_LEN + 1];
            char topic[MAX_TOPIC_LEN + 1];
        } __attribute__((packed)) client_unsub;
    };
} __attribute__((packed));

/**
 * @brief Checks if the string is a number or nor (contains only digits).
 * 
 * @param str the string to check
 * @param len the length of the string
 * @return true, if the string is a number
 */
bool is_number(const char *str, const int len);

/**
 * @brief Receives messages from the given socket, be they concatenated
 *   or truncated, until no more messages are received.
 * 
 * @param tcp_socket the socket to receive the messages from
 * @param messages a vector of char arrays, storing dynamically allocated
 *   messages received from the socket
 * @return int - the error code
 */
int recv_messages(const int tcp_socket, std::vector<char *> &messages);

#endif
