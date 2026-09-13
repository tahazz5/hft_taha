#include "market_depth.hpp"
#include <charconv>
#include <cerrno>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
bool integer(const std::string& s, std::uint64_t& value) {
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
    return error == std::errc{} && end == s.data() + s.size();
}
bool decimal(const std::string& s) {
    bool dot = false, nonzero = false;
    if (s.empty() || s.size() > 49 || s.front() == '.' || s.back() == '.') return false;
    for (char c : s) {
        if (c == '.' && !dot) dot = true;
        else if (c < '0' || c > '9') return false;
        else if (c != '0') nonzero = true;
    }
    return nonzero;
}
struct Consumer {
    MarketDepth depth;
    std::string symbol;
    bool stale = true;
    void invalidate() {
        depth.Invalidate();
        if (!stale) std::cout << "{\"type\":\"stale\"}" << std::endl;
        stale = true;
    }
    bool line(const std::string& value) {
        std::istringstream input(value);
        std::vector<std::string> f;
        for (std::string word; input >> word;) f.push_back(word);
        if (f.size() == 1 && f[0] == "STALE") { invalidate(); return true; }
        if (f.size() == 4 && f[0] == "META") {
            if (!symbol.empty() || f[1].size() < 2 || f[1].size() > 30 ||
                f[1].find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != std::string::npos ||
                !decimal(f[2]) || !decimal(f[3])) return false;
            symbol = f[1];
            std::cout << "{\"type\":\"metadata\",\"symbol\":\"" << symbol
                      << "\",\"tick\":\"" << f[2] << "\",\"step\":\"" << f[3]
                      << "\",\"environment\":\"spot-testnet\"}" << std::endl;
            return true;
        }
        if (symbol.empty() || f.size() < 4 || f[0] != "SNAPSHOT") return false;
        std::uint64_t sequence, nb, na;
        if (!integer(f[1], sequence) || !integer(f[2], nb) || !integer(f[3], na) ||
            nb > 20 || na > 20 || f.size() != 4 + 2 * (nb + na)) return false;
        std::array<MarketDepth::Level, 40> levels{};
        for (std::size_t i = 0; i < nb + na; ++i) {
            if (!integer(f[4 + i * 2], levels[i].price_ticks) ||
                !integer(f[5 + i * 2], levels[i].quantity_lots)) return false;
        }
        if (!depth.Apply(sequence, {levels.data(), static_cast<std::size_t>(nb)},
                         {levels.data() + nb, static_cast<std::size_t>(na)})) return false;
        stale = false;
        const auto bid = depth.BestBid(), ask = depth.BestAsk();
        const auto print_level = [](auto level) {
            if (level) std::cout << '[' << level->price_ticks << ',' << level->quantity_lots << ']';
            else std::cout << "null";
        };
        std::cout << "{\"type\":\"quote\",\"symbol\":\"" << symbol << "\",\"sequence\":" << sequence << ",\"bid\":";
        print_level(bid);
        std::cout << ",\"ask\":";
        print_level(ask);
        std::cout << '}' << std::endl;
        return bool(std::cout);
    }
};
}
int main() {
    Consumer consumer;
    std::string pending;
    char buffer[4096];
    while (true) {
        pollfd fd{STDIN_FILENO, POLLIN, 0};
        const int ready = poll(&fd, 1, 100);
        if (ready < 0) { if (errno == EINTR) continue; consumer.invalidate(); return 1; }
        if (!consumer.depth.Fresh()) consumer.invalidate();
        if (!ready) continue;
        if (fd.revents & (POLLERR | POLLNVAL)) { consumer.invalidate(); return 1; }
        const auto bytes = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (bytes < 0) { if (errno == EINTR) continue; consumer.invalidate(); return 1; }
        if (bytes == 0) { consumer.invalidate(); return pending.empty() ? 0 : 1; }
        pending.append(buffer, static_cast<std::size_t>(bytes));
        std::size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos) {
            if (newline > 8192 || !consumer.line(pending.substr(0, newline))) {
                consumer.invalidate();
                std::cerr << "Invalid market-data frame\n";
                return 1;
            }
            pending.erase(0, newline + 1);
        }
        if (pending.size() > 8192) { consumer.invalidate(); return 1; }
    }
}
