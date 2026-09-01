#define main orderbook_main
#include "../order.cpp"
#undef main

#include <chrono>
#include <iostream>

int main() {
    Orderbook book;

    const int N = 100000;

    auto startAdd = std::chrono::steady_clock::now();
    for (id i = 1; i <= N; ++i) {
        const side s = (i % 2 == 0) ? side::BID : side::ASK;
        book.AddOrder(order{i, 100 + (i % 100), 10 + (i % 50), s});
    }
    auto endAdd = std::chrono::steady_clock::now();

    auto startMatch = std::chrono::steady_clock::now();
    for (id i = 1; i <= N / 2; ++i) {
        book.ExecuteOrder(order{N + i, 100, 5 + (i % 10), side::BID});
    }
    auto endMatch = std::chrono::steady_clock::now();

    const auto addUs = std::chrono::duration_cast<std::chrono::microseconds>(endAdd - startAdd).count();
    const auto matchUs = std::chrono::duration_cast<std::chrono::microseconds>(endMatch - startMatch).count();

    std::cout << "Orders added: " << N << '\n';
    std::cout << "Add time: " << addUs << " us\n";
    std::cout << "Matching time: " << matchUs << " us\n";
    std::cout << "Average add time: " << static_cast<double>(addUs) / N << " us/order\n";

    return 0;
}
