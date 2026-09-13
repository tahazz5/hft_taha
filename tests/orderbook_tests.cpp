#include "orderbook.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "Failed line " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::exit(1); } } while (false)

static void regression() {
    Orderbook b;
    CHECK(std::isnan(b.Spread()));
    CHECK(std::isnan(b.MidPrice()));
    CHECK(!b.AddOrder({0,100,10,side::ASK}));
    CHECK(!b.AddOrder({1,0,10,side::ASK}));
    CHECK(!b.AddOrder({1,100,0,side::ASK}));
    CHECK(!b.AddOrder({1,100,10,static_cast<side>(9)}));
    CHECK(b.AddOrder({1,100,10,side::ASK}));
    CHECK(b.AddOrder({2,100,20,side::ASK}));
    const auto before = b.Snapshot();
    CHECK(!b.ExecuteOrder({1,100,10,side::BID}));
    CHECK(!b.ExecuteOrder({3,0,10,side::ASK}));
    CHECK(!b.ModifyOrder({1,100,10,static_cast<side>(9)}));
    CHECK(!b.ExecuteOrder({3,100,31,side::BID},TimeInForce::FOK));
    CHECK(b.Snapshot() == before);
    CHECK(b.ModifyOrder({1,100,5,side::ASK}));
    CHECK(std::get<0>(b.BestAsk()) == 1);
    CHECK(b.ModifyOrder({1,100,15,side::ASK}));
    CHECK(std::get<0>(b.BestAsk()) == 2);
    std::vector<Trade> trades;
    trades.reserve(10);
    b.SetTradeCallback([](void* context, const Trade& t) noexcept {
        static_cast<std::vector<Trade>*>(context)->push_back(t);
    }, &trades);
    CHECK(b.ExecuteOrder({3,100,25,side::BID},TimeInForce::FOK));
    CHECK(trades.size() == 2 && trades[0].maker == 2 && trades[0].size == 20);
    CHECK(trades[1].maker == 1 && trades[1].size == 5 && trades[1].execution_price == 100);
    CHECK(std::get<2>(b.BestAsk()) == 10);
    CHECK(b.ExecuteOrder({4,99,50,side::BID},TimeInForce::IOC));
    CHECK(!b.FindOrder(4));
    CHECK(b.ExecuteOrder({5,0,50,side::BID},TimeInForce::IOC,true));
    CHECK(b.Size() == 0);
    CHECK(b.AddOrder({6,100,30,side::BID}));
    CHECK(b.ExecuteOrder({7,100,50,side::ASK}));
    CHECK(b.BestBid() == order{});
    CHECK(b.BestAsk() == order{7,100,20,side::ASK});
    CHECK(b.ModifyOrder({7,90,20,side::BID}));
    CHECK(b.BestAsk() == order{});
    CHECK(b.CancelOrder(7));
    CHECK(!b.CancelOrder(7));
    CHECK(b.AddOrder({8,100,10,side::BID}));
    CHECK(b.AddOrder({9,110,10,side::ASK}));
    CHECK(b.Spread() == 10 && b.MidPrice() == 105);
    Orderbook limited(8, Limits{10,1000,1});
    CHECK(!limited.AddOrder({1,101,10,side::ASK}));
    CHECK(!limited.AddOrder({1,1,11,side::ASK}));
    CHECK(limited.AddOrder({1,100,10,side::ASK}));
    CHECK(!limited.AddOrder({2,100,1,side::ASK}));
    CHECK(!limited.ExecuteOrder({2,100,1,side::BID}));
    CHECK(limited.ExecuteOrder({2,100,1,side::BID},TimeInForce::IOC));
}

// Intentionally simple vector oracle, independent of the production containers.
static void randomized() {
    std::mt19937_64 rng(20260913);
    Orderbook b;
    std::vector<order> ref;
    auto sorted = [&] {
        auto result = ref;
        std::stable_sort(result.begin(), result.end(), [](const order& a, const order& c) {
            if (std::get<3>(a) != std::get<3>(c)) return std::get<3>(a) == side::BID;
            return std::get<3>(a) == side::BID ? std::get<1>(a) > std::get<1>(c) : std::get<1>(a) < std::get<1>(c);
        });
        return result;
    };
    for (int n = 0; n < 30000; ++n) {
        id i = 1 + rng()%200;
        order o{i,90+rng()%20,1+rng()%30,rng()%2 ? side::ASK : side::BID};
        auto it = std::find_if(ref.begin(),ref.end(),[&](const order& x){return std::get<0>(x)==i;});
        const bool found = it != ref.end();
        switch (rng()%4) {
        case 0:
            CHECK(b.AddOrder(o) == !found);
            if (!found) ref.push_back(o);
            break;
        case 1:
            CHECK(b.CancelOrder(i) == found);
            if (found) ref.erase(it);
            break;
        case 2:
            CHECK(b.ModifyOrder(o) == found);
            if (found) {
                if (std::get<1>(*it)==std::get<1>(o) && std::get<3>(*it)==std::get<3>(o) && std::get<2>(o)<=std::get<2>(*it)) *it=o;
                else { ref.erase(it); ref.push_back(o); }
            }
            break;
        default:
            CHECK(b.ExecuteOrder(o) == !found);
            if (!found) {
                auto remaining=std::get<2>(o);
                while (remaining) {
                    auto candidate=ref.end();
                    for (auto r=ref.begin();r!=ref.end();++r) {
                        if (std::get<3>(*r)==std::get<3>(o)) continue;
                        const bool buy=std::get<3>(o)==side::BID;
                        auto p=std::get<1>(*r);
                        if (buy ? p>std::get<1>(o) : p<std::get<1>(o)) continue;
                        if (candidate==ref.end() || (buy ? p<std::get<1>(*candidate) : p>std::get<1>(*candidate))) candidate=r;
                    }
                    if (candidate==ref.end()) break;
                    auto fill=std::min(remaining,std::get<2>(*candidate));
                    remaining-=fill;
                    std::get<2>(*candidate)-=fill;
                    if (!std::get<2>(*candidate)) ref.erase(candidate);
                }
                if (remaining) {std::get<2>(o)=remaining;ref.push_back(o);}
            }
        }
        CHECK(b.Snapshot()==sorted());
        CHECK(b.Size()==ref.size());
        for (const auto& x:ref) CHECK(b.FindOrder(std::get<0>(x))==x);
    }
}
static void stress() {
    Orderbook b(100000);
    for (id i=1;i<=100000;++i) CHECK(b.AddOrder({i,100+i%100,10,side::ASK}));
    CHECK(b.Size()==100000);
    CHECK(b.ExecuteOrder({100001,199,1000000,side::BID},TimeInForce::FOK));
    CHECK(b.Size()==0 && b.BestAsk()==order{});
}
int main() { regression(); randomized(); stress(); std::cout << "All tests passed (30,000 randomized operations, 100,000-order sweep).\n"; }
