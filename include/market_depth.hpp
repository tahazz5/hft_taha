#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

// Binance publishes aggregated levels, not individual FIFO orders.
// Single owner; never use this view as the local matching engine's resting orders.
class MarketDepth {
public:
    struct Level {
        std::uint64_t price_ticks{}, quantity_lots{};
        bool operator==(const Level&) const = default;
    };
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t max_levels = 20;
private:
    std::array<Level, max_levels> bids_{}, asks_{};
    std::size_t bid_count_{}, ask_count_{};
    std::uint64_t sequence_{};
    Clock::time_point received_{};
    bool live_ = false;

    static bool valid(std::span<const Level> levels, bool bids) {
        if (levels.size() > max_levels) return false;
        for (std::size_t i = 0; i < levels.size(); ++i) {
            if (!levels[i].price_ticks || !levels[i].quantity_lots) return false;
            if (i && (bids ? levels[i-1].price_ticks <= levels[i].price_ticks
                          : levels[i-1].price_ticks >= levels[i].price_ticks)) return false;
        }
        return true;
    }
public:
    void Invalidate() noexcept { live_ = false; bid_count_ = ask_count_ = 0; }
    bool Apply(std::uint64_t sequence, std::span<const Level> bids,
               std::span<const Level> asks, Clock::time_point now = Clock::now()) {
        bool consistent = !live_ || sequence >= sequence_;
        if (live_ && sequence == sequence_) {
            consistent = bids.size() == bid_count_ && asks.size() == ask_count_;
            for (std::size_t i = 0; consistent && i < bids.size(); ++i) consistent = bids[i] == bids_[i];
            for (std::size_t i = 0; consistent && i < asks.size(); ++i) consistent = asks[i] == asks_[i];
        }
        if (!consistent || !valid(bids, true) || !valid(asks, false) ||
            (!bids.empty() && !asks.empty() && bids.front().price_ticks >= asks.front().price_ticks)) {
            Invalidate();
            return false;
        }
        bid_count_ = bids.size(); ask_count_ = asks.size();
        for (std::size_t i = 0; i < bids.size(); ++i) bids_[i] = bids[i];
        for (std::size_t i = 0; i < asks.size(); ++i) asks_[i] = asks[i];
        sequence_ = sequence; received_ = now; live_ = true;
        return true;
    }
    bool Fresh(Clock::time_point now = Clock::now()) const noexcept {
        return live_ && now >= received_ && now - received_ < std::chrono::seconds(3);
    }
    std::optional<Level> BestBid(Clock::time_point now = Clock::now()) const noexcept {
        return Fresh(now) && bid_count_ ? std::optional(bids_[0]) : std::nullopt;
    }
    std::optional<Level> BestAsk(Clock::time_point now = Clock::now()) const noexcept {
        return Fresh(now) && ask_count_ ? std::optional(asks_[0]) : std::nullopt;
    }
    std::uint64_t Sequence() const noexcept { return sequence_; }
};
