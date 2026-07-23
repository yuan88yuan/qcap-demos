#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

const unsigned kCounters = 4;
const uint64_t kU32Modulus = UINT64_C(1) << 32;

volatile sig_atomic_t g_stop = 0;

void stop_handler(int) {
    g_stop = 1;
}

struct Sample {
    uint32_t ticks[kCounters];
    uint64_t timestamp_ns;
};

struct CounterStats {
    uint64_t samples;
    uint64_t total_ticks;
    long double mean;
    long double m2;
    long double minimum;
    long double maximum;

    CounterStats()
        : samples(0), total_ticks(0), mean(0), m2(0),
          minimum(std::numeric_limits<long double>::infinity()),
          maximum(-std::numeric_limits<long double>::infinity()) {}

    void add(uint32_t ticks, long double seconds) {
        const long double rate = static_cast<long double>(ticks) / seconds;
        ++samples;
        total_ticks += ticks;
        const long double delta = rate - mean;
        mean += delta / static_cast<long double>(samples);
        m2 += delta * (rate - mean);
        if (rate < minimum) minimum = rate;
        if (rate > maximum) maximum = rate;
    }

    long double standard_deviation() const {
        if (samples < 2) return 0;
        return std::sqrt(m2 / static_cast<long double>(samples - 1));
    }
};

bool read_clock_ns(uint64_t* result) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        std::cerr << "clock_gettime(CLOCK_MONOTONIC): " << std::strerror(errno) << "\n";
        return false;
    }
    *result = static_cast<uint64_t>(ts.tv_sec) * UINT64_C(1000000000) +
              static_cast<uint64_t>(ts.tv_nsec);
    return true;
}

bool counter_files_exist(const std::string& directory) {
    for (unsigned i = 0; i < kCounters; ++i) {
        std::ostringstream path;
        path << directory << "/zdev_ticks" << i;
        if (access(path.str().c_str(), R_OK) != 0) return false;
    }
    return true;
}

bool parse_counter(const std::string& text, uint32_t* result) {
    const char* begin = text.c_str();
    while (*begin == ' ' || *begin == '\t' || *begin == '\r' || *begin == '\n') ++begin;
    if (*begin == '-' || *begin == '\0') return false;

    char* end = NULL;
    errno = 0;
    const unsigned long long value = std::strtoull(begin, &end, 0);
    if (errno == ERANGE || end == begin || value >= kU32Modulus) return false;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0') return false;

    *result = static_cast<uint32_t>(value);
    return true;
}

bool read_sample(const std::string& directory, Sample* sample) {
    uint64_t before;
    uint64_t after;
    uint32_t values[kCounters];

    if (!read_clock_ns(&before)) return false;
    for (unsigned i = 0; i < kCounters; ++i) {
        std::ostringstream path;
        path << directory << "/zdev_ticks" << i;
        std::ifstream file(path.str().c_str());
        std::string text;
        if (!file || !std::getline(file, text) || !parse_counter(text, &values[i])) {
            std::cerr << "Unable to read a 32-bit value from " << path.str() << "\n";
            return false;
        }
    }
    if (!read_clock_ns(&after)) return false;

    // Attribute the register reads to their midpoint.  This removes most of
    // the small, but measurable, sysfs read latency from the sample interval.
    sample->timestamp_ns = before + (after - before) / 2;
    for (unsigned i = 0; i < kCounters; ++i) sample->ticks[i] = values[i];
    return true;
}

bool find_in_tree(const std::string& root, std::string* result) {
    DIR* dir = opendir(root.c_str());
    if (!dir) return false;

    struct dirent* entry;
    while (!g_stop && (entry = readdir(dir)) != NULL) {
        if (std::strcmp(entry->d_name, ".") == 0 ||
            std::strcmp(entry->d_name, "..") == 0) continue;

        const std::string path = root + "/" + entry->d_name;
        struct stat info;
        if (lstat(path.c_str(), &info) != 0) continue;
        if (S_ISREG(info.st_mode) && std::strcmp(entry->d_name, "zdev_ticks0") == 0) {
            const std::string directory = root;
            if (counter_files_exist(directory)) {
                *result = directory;
                closedir(dir);
                return true;
            }
        }
        // Do not follow symlinks: sysfs contains links back into the tree.
        if (S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode) &&
            find_in_tree(path, result)) {
            closedir(dir);
            return true;
        }
    }
    closedir(dir);
    return false;
}

std::string find_sysfs_directory(const std::string& requested) {
    if (!requested.empty()) {
        if (counter_files_exist(requested)) return requested;
        return std::string();
    }

    const std::string default_root = "/sys/devices/platform/amba_pl@0";
    std::string result;
    if (find_in_tree(default_root, &result)) return result;
    if (find_in_tree("/sys/devices", &result)) return result;
    return std::string();
}

bool parse_double(const char* text, double* result) {
    char* end = NULL;
    errno = 0;
    const double value = std::strtod(text, &end);
    if (errno == ERANGE || end == text || *end != '\0' || !std::isfinite(value) || value <= 0) {
        return false;
    }
    *result = value;
    return true;
}

bool parse_count(const char* text, uint64_t* result) {
    char* end = NULL;
    errno = 0;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') return false;
    *result = static_cast<uint64_t>(value);
    return true;
}

void print_usage(const char* name) {
    std::cerr << "Usage: " << name << " [interval_seconds] [samples] [sysfs_directory]\n"
              << "  interval_seconds: requested interval, default 1.0\n"
              << "  samples: number of intervals, default 0 (until Ctrl+C)\n"
              << "  sysfs_directory: optional directory containing zdev_ticks0..3\n";
}

bool sleep_until(uint64_t deadline_ns) {
    struct timespec deadline;
    deadline.tv_sec = static_cast<time_t>(deadline_ns / UINT64_C(1000000000));
    deadline.tv_nsec = static_cast<long>(deadline_ns % UINT64_C(1000000000));
    for (;;) {
        const int error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
        if (error == 0 || g_stop) return error == 0;
        if (error != EINTR) {
            std::cerr << "clock_nanosleep: " << std::strerror(error) << "\n";
            return false;
        }
    }
}

void print_rate(long double rate) {
    std::cout << std::fixed << std::setprecision(3) << static_cast<double>(rate);
}

} // namespace

int main(int argc, char** argv) {
    double interval_seconds = 1.0;
    uint64_t requested_samples = 0;
    std::string requested_directory;

    if (argc > 1 && !parse_double(argv[1], &interval_seconds)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc > 2 && !parse_count(argv[2], &requested_samples)) {
        print_usage(argv[0]);
        return 2;
    }
    if (argc > 3) requested_directory = argv[3];
    if (argc > 4) {
        print_usage(argv[0]);
        return 2;
    }

    const long double interval_ns_ld = static_cast<long double>(interval_seconds) * 1000000000.0L;
    if (interval_ns_ld < 1.0L || interval_ns_ld > static_cast<long double>(UINT64_MAX)) {
        std::cerr << "interval_seconds is outside the supported range\n";
        return 2;
    }
    const uint64_t interval_ns = static_cast<uint64_t>(interval_ns_ld + 0.5L);

    const std::string directory = find_sysfs_directory(requested_directory);
    if (directory.empty()) {
        std::cerr << "Error: could not find zdev_ticks0..3 sysfs attributes.\n"
                  << "Is the zzlab-env kernel module loaded?\n";
        return 1;
    }

    std::signal(SIGINT, stop_handler);
    std::signal(SIGTERM, stop_handler);

    Sample previous;
    if (!read_sample(directory, &previous)) return 1;

    std::cout << "Monitoring zdev_ticks via " << directory << "\n"
              << "requested interval: " << std::fixed << std::setprecision(6)
              << interval_seconds << "s (timestamps use CLOCK_MONOTONIC)\n"
              << "sample  elapsed(s)";
    for (unsigned i = 0; i < kCounters; ++i) std::cout << "  tick" << i << "(ticks/s)";
    std::cout << "\n";

    CounterStats stats[kCounters];
    uint64_t total_elapsed_ns = 0;
    uint64_t deadline_ns = previous.timestamp_ns + interval_ns;
    uint64_t sample_number = 0;

    while (!g_stop && (requested_samples == 0 || sample_number < requested_samples)) {
        if (!sleep_until(deadline_ns)) break;

        Sample current;
        if (!read_sample(directory, &current)) return 1;
        const uint64_t elapsed_ns = current.timestamp_ns - previous.timestamp_ns;
        if (elapsed_ns == 0) {
            std::cerr << "Skipping sample with zero elapsed time\n";
            previous = current;
            deadline_ns = current.timestamp_ns + interval_ns;
            continue;
        }

        const long double elapsed_seconds = static_cast<long double>(elapsed_ns) / 1000000000.0L;
        ++sample_number;
        total_elapsed_ns += elapsed_ns;
        std::cout << std::dec << sample_number << "  " << std::fixed << std::setprecision(9)
                  << static_cast<double>(elapsed_seconds);
        for (unsigned i = 0; i < kCounters; ++i) {
            // Unsigned arithmetic intentionally implements modulo 2^32 wrap-around.
            const uint32_t delta = current.ticks[i] - previous.ticks[i];
            stats[i].add(delta, elapsed_seconds);
            std::cout << "  ";
            print_rate(static_cast<long double>(delta) / elapsed_seconds);
        }
        std::cout << "\n" << std::flush;

        previous = current;
        // Keep the requested cadence without allowing late samples to cause a
        // burst of immediate reads after scheduler delays.
        deadline_ns += interval_ns;
        if (deadline_ns <= current.timestamp_ns) deadline_ns = current.timestamp_ns + interval_ns;
    }

    if (sample_number != 0) {
        const long double total_seconds = static_cast<long double>(total_elapsed_ns) / 1000000000.0L;
        std::cout << "\nStatistics (" << sample_number << " samples, "
                  << std::fixed << std::setprecision(9) << static_cast<double>(total_seconds)
                  << "s):\n";
        std::cout << "counter       average(ticks/s)       min       max       stddev\n";
        for (unsigned i = 0; i < kCounters; ++i) {
            const long double overall = static_cast<long double>(stats[i].total_ticks) / total_seconds;
            std::cout << "tick" << i << "         ";
            print_rate(overall);
            std::cout << "       ";
            print_rate(stats[i].minimum);
            std::cout << "  ";
            print_rate(stats[i].maximum);
            std::cout << "  ";
            print_rate(stats[i].standard_deviation());
            std::cout << "\n";
        }
    }

    return 0;
}
