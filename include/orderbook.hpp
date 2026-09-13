#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <memory_resource>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

enum class side { ASK, BID };
using id = std::uint64_t;
using quantity = std::uint64_t;
using price = std::uint64_t;
using order = std::tuple<id, price, quantity, side>;
enum class TimeInForce { GTC, IOC, FOK };
struct Trade { id maker; id taker; price execution_price; quantity size; side aggressor; };
struct Limits {
    quantity max_quantity = std::numeric_limits<quantity>::max();
    std::uint64_t max_notional = std::numeric_limits<std::uint64_t>::max();
    std::size_t max_orders = 1'000'000;
};

// Single writer. The callback must not reenter this book and must not throw.
class Orderbook {
    using Queue = std::pmr::list<order>;
    using Levels = std::pmr::map<price, Queue>;
    struct Location { Levels::iterator level; Queue::iterator item; side direction; };
    std::pmr::unsynchronized_pool_resource pool_;
    Levels asks_{&pool_}, bids_{&pool_};
    std::pmr::unordered_map<id, Location> orders_{&pool_};
    Limits limits_;
    using Callback = void (*)(void*, const Trade&) noexcept;
    Callback callback_ = nullptr;
    void* context_ = nullptr;

    static bool valid_side(side s) { return s == side::ASK || s == side::BID; }
    bool valid(const order& o, bool market = false) const {
        auto [i, p, q, s] = o;
        return i != 0 && valid_side(s) && q != 0 && q <= limits_.max_quantity &&
               (market || p != 0) && (market || p <= limits_.max_notional / q);
    }
    Levels& levels(side s) { return s == side::ASK ? asks_ : bids_; }
    static Levels::iterator best(Levels& l, side s) {
        return s == side::ASK ? l.begin() : std::prev(l.end());
    }
    bool enough(const order& o, bool market) const {
        auto [i, p, remaining, s] = o;
        (void)i;
        const auto consume = [&](const auto& entry) {
            for (const auto& resting : entry.second) {
                auto q = std::get<2>(resting);
                if (q >= remaining) { remaining = 0; return; }
                remaining -= q;
            }
        };
        if (s == side::BID) {
            for (const auto& entry : asks_) {
                if (!market && entry.first > p) break;
                consume(entry);
                if (!remaining) return true;
            }
        } else {
            for (auto it = bids_.rbegin(); it != bids_.rend(); ++it) {
                if (!market && it->first < p) break;
                consume(*it);
                if (!remaining) return true;
            }
        }
        return false;
    }
public:
    explicit Orderbook(std::size_t reserve = 4096, Limits limits = {}) : limits_(limits) {
        orders_.reserve(std::min(reserve, limits.max_orders));
    }
    Orderbook(const Orderbook&) = delete;
    Orderbook& operator=(const Orderbook&) = delete;
    void SetTradeCallback(Callback cb, void* context = nullptr) noexcept {
        callback_ = cb; context_ = context;
    }
    std::size_t Size() const noexcept { return orders_.size(); }
    std::optional<order> FindOrder(id i) const {
        auto it = orders_.find(i);
        if (it == orders_.end()) return std::nullopt;
        return *it->second.item;
    }
    bool AddOrder(order o) {
        auto [i, p, q, s] = o;
        (void)q;
        if (!valid(o) || orders_.contains(i) || Size() >= limits_.max_orders) return false;
        auto& l = levels(s);
        auto [level, created] = l.try_emplace(p);
        try { level->second.push_back(o); }
        catch (...) { if (created) l.erase(level); throw; }
        try { orders_.emplace(i, Location{level, std::prev(level->second.end()), s}); }
        catch (...) {
            level->second.pop_back();
            if (created) l.erase(level);
            throw;
        }
        return true;
    }
    bool CancelOrder(id i) {
        auto it = orders_.find(i);
        if (it == orders_.end()) return false;
        const auto loc = it->second;
        loc.level->second.erase(loc.item);
        if (loc.level->second.empty()) levels(loc.direction).erase(loc.level);
        orders_.erase(it);
        return true;
    }
    // Raw book modification; quantity increases and price/side changes lose FIFO priority.
    bool ModifyOrder(order o) {
        if (!valid(o)) return false;
        auto it = orders_.find(std::get<0>(o));
        if (it == orders_.end()) return false;
        auto& loc = it->second;
        const auto [i, p, q, s] = o;
        (void)i;
        if (s == loc.direction && p == loc.level->first) {
            if (q > std::get<2>(*loc.item))
                loc.level->second.splice(loc.level->second.end(), loc.level->second, loc.item);
        } else {
            auto target = levels(s).try_emplace(p).first;
            target->second.splice(target->second.end(), loc.level->second, loc.item);
            if (loc.level->second.empty()) levels(loc.direction).erase(loc.level);
            loc.level = target;
            loc.direction = s;
        }
        *loc.item = o;
        return true;
    }
    bool ExecuteOrder(order o, TimeInForce tif = TimeInForce::GTC, bool market = false) {
        if (!valid(o, market) || orders_.contains(std::get<0>(o)) ||
            (tif != TimeInForce::GTC && tif != TimeInForce::IOC && tif != TimeInForce::FOK) ||
            (market && tif == TimeInForce::GTC)) return false;
        // Conservative admission: reserve capacity before any GTC executions.
        if (tif == TimeInForce::GTC && Size() >= limits_.max_orders) return false;
        if (tif == TimeInForce::FOK && !enough(o, market)) return false;
        auto [i, p, remaining, s] = o;
        auto opposite = s == side::BID ? side::ASK : side::BID;
        auto& l = levels(opposite);
        while (remaining && !l.empty()) {
            auto level = best(l, opposite);
            if (!market && (s == side::BID ? level->first > p : level->first < p)) break;
            auto& maker = level->second.front();
            const auto filled = std::min(remaining, std::get<2>(maker));
            Trade trade{std::get<0>(maker), i, level->first, filled, s};
            remaining -= filled;
            std::get<2>(maker) -= filled;
            if (!std::get<2>(maker)) {
                orders_.erase(trade.maker);
                level->second.pop_front();
                if (level->second.empty()) l.erase(level);
            }
            if (callback_) callback_(context_, trade);
        }
        if (remaining && tif == TimeInForce::GTC) {
            std::get<2>(o) = remaining;
            return AddOrder(o);
        }
        return true;
    }
    order BestBid() const { return bids_.empty() ? order{} : bids_.rbegin()->second.front(); }
    order BestAsk() const { return asks_.empty() ? order{} : asks_.begin()->second.front(); }
    double Spread() const {
        if (asks_.empty() || bids_.empty()) return std::numeric_limits<double>::quiet_NaN();
        return double(asks_.begin()->first) - double(bids_.rbegin()->first);
    }
    double MidPrice() const {
        if (asks_.empty() || bids_.empty()) return std::numeric_limits<double>::quiet_NaN();
        return double(asks_.begin()->first) / 2 + double(bids_.rbegin()->first) / 2;
    }
    // Deterministic price/FIFO snapshot: bids descending, then asks ascending.
    std::vector<order> Snapshot() const {
        std::vector<order> result;
        result.reserve(Size());
        for (auto it = bids_.rbegin(); it != bids_.rend(); ++it)
            result.insert(result.end(), it->second.begin(), it->second.end());
        for (const auto& [p, queue] : asks_) result.insert(result.end(), queue.begin(), queue.end());
        return result;
    }
};
