#include "orderbook.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static bool number(const std::string& text, std::uint64_t& value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}
int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: orderbook_replay FILE\n"; return 2; }
    std::ifstream input(argv[1]);
    if (!input) { std::cerr << "Cannot open input\n"; return 2; }
    Orderbook book;
    book.SetTradeCallback([](void*, const Trade& t) noexcept {
        std::cout << "TRADE " << t.maker << ' ' << t.taker << ' ' << t.execution_price << ' ' << t.size << '\n';
    });
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = line.substr(0, line.find('#'));
        std::istringstream stream(line);
        std::vector<std::string> fields;
        for (std::string field; stream >> field;) fields.push_back(field);
        if (fields.empty()) continue;
        bool ok = false;
        id i = 0; price p = 0; quantity q = 0;
        if (fields.size() == 2 && fields[0] == "CANCEL" && number(fields[1], i)) {
            ok = book.CancelOrder(i);
        } else if ((fields.size() == 5 || fields.size() == 6) && number(fields[1], i) &&
                   number(fields[3], p) && number(fields[4], q) &&
                   (fields[2] == "BID" || fields[2] == "ASK")) {
            order o{i, p, q, fields[2] == "BID" ? side::BID : side::ASK};
            if (fields[0] == "ADD" && fields.size() == 5) ok = book.AddOrder(o);
            else if (fields[0] == "MODIFY" && fields.size() == 5) ok = book.ModifyOrder(o);
            else if (fields[0] == "EXECUTE" || fields[0] == "MARKET") {
                auto policy = fields.size() == 6 ? fields[5] : (fields[0] == "MARKET" ? "IOC" : "GTC");
                if (policy == "GTC" || policy == "IOC" || policy == "FOK")
                    ok = book.ExecuteOrder(o, policy == "IOC" ? TimeInForce::IOC :
                        policy == "FOK" ? TimeInForce::FOK : TimeInForce::GTC, fields[0] == "MARKET");
            }
        }
        if (!ok) { std::cerr << "Rejected line " << line_number << '\n'; return 1; }
    }
    if (input.bad()) { std::cerr << "Input read failure\n"; return 2; }
    for (const auto& [i, p, q, s] : book.Snapshot())
        std::cout << "ORDER " << i << ' ' << (s == side::BID ? "BID" : "ASK") << ' ' << p << ' ' << q << '\n';
}
