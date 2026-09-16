#include <esp_log.h>
#include <lwip/sockets.h>

#include "config/config.h"


static int sock = -1;
static struct sockaddr_in server_addr;
static const char *TAG = "UDP_LOGGER";



// Initializes the UDP socket
static void udp_init()
{
    if (sock >= 0) return;  // Prevent re-initialization

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        esp_log_write(ESP_LOG_ERROR, TAG, "Failed to create UDP socket\n");
        return;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(LOG_UDP_PORT);
    if (inet_pton(AF_INET, LOG_UDP_IP, &server_addr.sin_addr) != 1) {
        esp_log_write(ESP_LOG_ERROR, TAG, "Invalid UDP log IP address\n");
        close(sock);
        sock = -1;
        return;
    }
}

// Sends a log message over UDP
static void udp_send_log(const char *message, size_t length)
{
    if (sock < 0) return;  // Socket not initialized

    sendto(sock, message, length, 0,
           (struct sockaddr *)&server_addr, sizeof(server_addr));
}

// Custom logging function for both UDP and local output
static int udp_log_vprintf(const char *fmt, va_list args)
{
    char log_buf[256];
    va_list args_copy;

    va_copy(args_copy, args);  // Copy va_list to preserve it for local logging
    int len = vsnprintf(log_buf, sizeof(log_buf), fmt, args);

    if (len > 0) {
        udp_send_log(log_buf, strnlen(log_buf, sizeof(log_buf)));  // Send log to UDP
        vprintf(fmt, args_copy);     // Print log locally
    }

    va_end(args_copy);
    return len;
}

// Initializes UDP logging and redirects logs
void udp_logging()
{
    udp_init();
    esp_log_set_vprintf(udp_log_vprintf);
}