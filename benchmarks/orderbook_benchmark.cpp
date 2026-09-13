#include "orderbook.hpp"
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string_view>
using Clock = std::chrono::steady_clock;
static void require(bool ok) { if (!ok) std::abort(); }
template<class F> void measure(std::string_view name, std::size_t n, F operation) {
    auto start=Clock::now();
    for (id i=1;i<=n;++i) operation(i);
    const double ns=std::chrono::duration<double,std::nano>(Clock::now()-start).count();
    std::cout << name << ": " << ns/n << " ns/op, " << n*1e9/ns << " ops/s\n";
}
int main() {
    constexpr std::size_t n=100000;
    Orderbook b(n);
    std::cout << "100000 operations per case; batch mean, steady_clock; 100 price levels\n";
    measure("add cold",n,[&](id i){require(b.AddOrder({i,100+i%100,10,side::ASK}));});
    std::uint64_t checksum=0;
    measure("best ask",n,[&](id){std::atomic_signal_fence(std::memory_order_seq_cst); checksum+=std::get<0>(b.BestAsk());});
    measure("modify increase",n,[&](id i){require(b.ModifyOrder({i,100+i%100,20,side::ASK}));});
    measure("cancel",n,[&](id i){require(b.CancelOrder(i));});
    measure("add pooled",n,[&](id i){require(b.AddOrder({i,100+i%100,20,side::ASK}));});
    measure("match partial",n,[&](id i){require(b.ExecuteOrder({n+i,199,1,side::BID},TimeInForce::IOC));});
    const auto resting=b.Size();
    const auto start=Clock::now();
    require(b.ExecuteOrder({2*n+1,199,2*n*20,side::BID},TimeInForce::IOC));
    const auto ns=std::chrono::duration<double,std::nano>(Clock::now()-start).count();
    require(b.Size()==0);
    std::cout << "sweep: " << ns/resting << " ns/resting order; checksum=" << checksum << '\n';
}
