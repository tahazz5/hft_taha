#define main orderbook_main
#include "../order.cpp"
#undef main

#include <cassert>
#include <iostream>

namespace {

void test_add_and_cancel() {
    Orderbook book;
    assert(book.AddOrder(order{1, 100, 10, side::ASK}));
    assert(book.AddOrder(order{2, 105, 20, side::BID}));
    assert(book.CancelOrder(1));
    assert(!book.CancelOrder(1));
    assert(std::get<0>(book.BestBid()) != 0);
}

void test_modify_order() {
    Orderbook book;
    assert(book.AddOrder(order{10, 100, 30, side::ASK}));
    assert(book.ModifyOrder(order{10, 110, 40, side::ASK}));
    order ask = book.BestAsk();
    assert(std::get<0>(ask) == 10);
    assert(std::get<1>(ask) == 110);
    assert(std::get<2>(ask) == 40);
}

void test_execute_partial_match() {
    Orderbook book;
    assert(book.AddOrder(order{1, 100, 30, side::BID}));
    assert(book.ExecuteOrder(order{2, 100, 50, side::ASK}));
    auto bestBid = book.BestBid();
    assert(std::get<2>(bestBid) == 10);
}

void test_execute_full_match() {
    Orderbook book;
    assert(book.AddOrder(order{1, 100, 30, side::BID}));
    assert(book.ExecuteOrder(order{2, 100, 30, side::ASK}));
    assert(std::get<0>(book.BestBid()) == 0);
    assert(std::get<0>(book.BestAsk()) == 0);
}

void test_100k_orders_stress() {
    Orderbook book;

    for (id i = 1; i <= 100000; ++i) {
        const side s = (i % 2 == 0) ? side::BID : side::ASK;
        const bool ok = book.AddOrder(order{i, 100 + (i % 100), 10 + (i % 50), s});
        assert(ok);
    }

    assert(book.BestAsk().price > 0);
    assert(book.BestBid().price > 0);
}

}  // namespace

int main() {
    test_add_and_cancel();
    test_modify_order();
    test_execute_partial_match();
    test_execute_full_match();
    test_100k_orders_stress();

    std::cout << "All orderbook tests passed." << std::endl;
    return 0;
}
