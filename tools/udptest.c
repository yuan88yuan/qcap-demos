/*=============================================================================
    udptest.c - Standalone UDP loss/reorder verification between two boards.

    Transports a sequence-tagged UDP stream and measures loss, duplicates and
    late (out-of-order) arrivals.  It deliberately avoids zstreamer/qcap so
    that NIC/wire/kernel behaviour can be verified independently.

    Roles:
      udptest tx <dest-ip> <port> <pps> <pktsize> <seconds>
      udptest rx <bind-ip> <port> <seconds>

    Header (12 bytes) at the start of every datagram:
      [0..3]   little-endian uint32 sequence number
      [4..11]  little-endian uint64 monotonic TX timestamp (nanoseconds)
=============================================================================*/
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_PAYLOAD 4096

static uint64_t mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static double now_sec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static uint16_t port_of(const char* s)
{
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end || v < 1 || v > 65535) {
        fprintf(stderr, "bad port: %s\n", s);
        exit(2);
    }
    return (uint16_t)v;
}

static double rate_of(const char* s)
{
    char* end = NULL;
    double v = strtod(s, &end);
    if (!end || *end || v <= 0) {
        fprintf(stderr, "bad rate: %s\n", s);
        exit(2);
    }
    return v;
}

static int size_of(const char* s)
{
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end || v < 12 || v > MAX_PAYLOAD) {
        fprintf(stderr, "bad packet size (12..%d): %s\n", MAX_PAYLOAD, s);
        exit(2);
    }
    return (int)v;
}

static int seconds_of(const char* s)
{
    char* end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end || v < 1) {
        fprintf(stderr, "bad seconds: %s\n", s);
        exit(2);
    }
    return (int)v;
}

static void usage(void)
{
    fprintf(stderr,
        "usage:\n"
        "  udptest tx <dest-ip> <port> <pps> <pktsize> <seconds>\n"
        "  udptest rx <bind-ip> <port> <seconds>\n");
    exit(2);
}

/*----------------------------------------------------------------------- TX */
static int run_tx(const char* dest, uint16_t port, double pps, int pktsize,
                  int seconds)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    if (inet_pton(AF_INET, dest, &dst.sin_addr) != 1) {
        fprintf(stderr, "bad dest ip: %s\n", dest);
        return 1;
    }
    if (connect(fd, (struct sockaddr*)&dst, sizeof(dst)) < 0) {
        perror("connect"); return 1;
    }

    uint8_t pkt[MAX_PAYLOAD];
    memset(pkt, 0xab, sizeof(pkt));
    double interval_ns = 1e9 / pps;
    uint64_t deadline = mono_ns();
    uint64_t sent = 0;
    uint64_t send_errors = 0;
    double start = now_sec();

    printf("TX -> %s:%u  %u pps x %d B  for %d s\n",
           dest, port, (unsigned)pps, pktsize, seconds);
    while (now_sec() - start < seconds) {
        uint32_t seq = (uint32_t)(sent & 0xffffffffu);
        memcpy(pkt, &seq, 4);
        uint64_t tnow = mono_ns();
        memcpy(pkt + 4, &tnow, 8);

        ssize_t n = send(fd, pkt, (size_t)pktsize, 0);
        if (n != (ssize_t)pktsize) send_errors++;
        sent++;

        deadline += (uint64_t)interval_ns;
        struct timespec req = { .tv_sec = (time_t)(deadline / 1000000000ull),
                                .tv_nsec = (long)(deadline % 1000000000ull) };
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, NULL);
    }

    double elapsed = now_sec() - start;
    printf("TX done: %" PRIu64 " pkts in %.2f s (%.0f pps), %.2f MB, "
           "%.2f Mbps, send_errors=%" PRIu64 "\n",
           sent, elapsed, sent / elapsed,
           sent * (double)pktsize / (1024.0 * 1024.0),
           sent * (double)pktsize * 8.0 / (elapsed * 1e6), send_errors);
    close(fd);
    return 0;
}

/*----------------------------------------------------------------------- RX */
static int run_rx(const char* bind_ip, uint16_t port, int seconds)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in local = {0};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    if (inet_pton(AF_INET, bind_ip, &local.sin_addr) != 1) {
        fprintf(stderr, "bad bind ip: %s\n", bind_ip);
        return 1;
    }
    if (bind(fd, (struct sockaddr*)&local, sizeof(local)) < 0) {
        perror("bind"); return 1;
    }

    uint8_t pkt[MAX_PAYLOAD];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    double start = now_sec();
    double deadline = start + seconds;
    uint64_t received = 0;
    uint64_t bytes = 0;
    uint64_t lost = 0;
    uint64_t dup = 0;
    uint64_t late = 0;
    uint64_t prev_seq = 0;
    int have_prev = 0;
    uint32_t max_gap = 0;
    uint32_t last_print = start;

    printf("RX on %s:%u  for %d s\n", bind_ip, port, seconds);
    while (1) {
        double remaining = deadline - now_sec();
        if (remaining <= 0) break;
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pr = poll(&pfd, 1, 200);
        if (pr <= 0) continue;

        ssize_t n = recvfrom(fd, pkt, sizeof(pkt), 0,
                             (struct sockaddr*)&from, &fromlen);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom"); break;
        }
        if (n < 12) continue;
        uint32_t seq;
        memcpy(&seq, pkt, 4);
        received++;
        bytes += (uint64_t)n;

        if (have_prev) {
            uint32_t delta = seq - prev_seq;
            if (delta == 0) {
                dup++;
            } else if (delta > 0) {
                lost += (delta - 1);
                if (delta - 1 > max_gap) max_gap = delta - 1;
            } else {
                late++;
            }
        }
        prev_seq = seq;
        have_prev = 1;

        if ((uint32_t)now_sec() > last_print) {
            last_print = (uint32_t)now_sec();
            double el = now_sec() - start;
            printf("  %6.1fs  rx=%" PRIu64 " (%.0f pps, %.2f Mbps) "
                   "lost=%" PRIu64 " dup=%" PRIu64 " late=%" PRIu64
                   " maxgap=%u\n",
                   el, received, received / el,
                   bytes * 8.0 / (el * 1e6),
                   lost, dup, late, max_gap);
        }
    }

    double elapsed = now_sec() - start;
    double loss_pct = (received + lost) > 0
        ? 100.0 * (double)lost / (double)(received + lost) : 0.0;
    printf("RX done: rx=%" PRIu64 " lost=%" PRIu64 " (%.4f%%) "
           "dup=%" PRIu64 " late=%" PRIu64 " maxgap=%u\n"
           "  throughput: %.2f Mbps, %.2f MB in %.2f s\n",
           received, lost, loss_pct, dup, late, max_gap,
           bytes * 8.0 / (elapsed * 1e6),
           bytes / (1024.0 * 1024.0), elapsed);
    close(fd);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) usage();
    if (strcmp(argv[1], "tx") == 0 && argc == 7) {
        return run_tx(argv[2], port_of(argv[3]), rate_of(argv[4]),
                      size_of(argv[5]), seconds_of(argv[6]));
    }
    if (strcmp(argv[1], "rx") == 0 && argc == 5) {
        return run_rx(argv[2], port_of(argv[3]), seconds_of(argv[4]));
    }
    usage();
    return 2;
}