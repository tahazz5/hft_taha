#include "market_depth.hpp"
#include <cstdlib>
#include <iostream>
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "Failed line " << __LINE__ << '\n'; std::exit(1); } } while(false)
int main() {
    MarketDepth book;
    const auto now = MarketDepth::Clock::now();
    const std::array<MarketDepth::Level, 2> bids{{{100, 2}, {99, 3}}};
    const std::array<MarketDepth::Level, 1> asks{{{101, 4}}};
    CHECK(!book.Fresh(now) && !book.BestBid(now));
    CHECK(book.Apply(10, bids, asks, now));
    CHECK(book.BestBid(now)->quantity_lots == 2);
    CHECK(book.BestAsk(now)->price_ticks == 101);
    CHECK(!book.Fresh(now + std::chrono::seconds(3)));
    CHECK(!book.BestBid(now + std::chrono::seconds(3)));
    CHECK(book.Apply(10, bids, asks, now));
    CHECK(!book.Apply(9, bids, asks, now));
    CHECK(!book.Fresh(now));
    CHECK(book.Apply(1, bids, asks, now)); // new connection after invalidation
    CHECK(book.Apply(999, {bids.data(), 1}, asks, now)); // complete snapshot, gaps allowed
    CHECK(!book.Apply(999, bids, asks, now)); // same sequence cannot change contents
    CHECK(!book.Apply(1000, asks, bids, now)); // crossed
    CHECK(!book.Apply(1000, asks, asks, now)); // locked
    const std::array<MarketDepth::Level, 2> duplicate{{{100, 1}, {100, 2}}};
    CHECK(!book.Apply(1000, duplicate, asks, now));
    const std::array<MarketDepth::Level, 1> zero{{{100, 0}}};
    CHECK(!book.Apply(1000, zero, asks, now));
    CHECK(book.Apply(1000, {}, {}, now));
    CHECK(book.Fresh(now) && !book.BestAsk(now));
    book.Invalidate();
    CHECK(!book.Fresh(now));
    std::cout << "Market depth tests passed\n";
}
