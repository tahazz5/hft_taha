
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include<stdlib.h>
#include <sys/types.h>
#include <tuple>
#include<chrono>
#include <unordered_map>


// order quantity , price  , id  , etat

enum class side{
    ASK,
    BID,
};
using id = u_int64_t ;

using quantity = u_int64_t;

using price = u_int64_t ;

using order = std::tuple<id , price , quantity , side> ;
using ask = order ;
using bid = order ;
class Orderbook {
public:
   bool AddOrder(order Order) {
      id Id = std::get<0>(Order);
      if (orders.count(Id)) {
         return false;
      }
      price p = std::get<1>(Order);
      if(std::get<2>(Order)==0 || std::get<1>(Order)==0 ) return false ;
      if(std::get<3>(Order)==side::ASK){
         
         asks[p].push_back(Order);
         orders[std::get<0>(Order)]= std::prev(asks[p].end());
         return true ;
      }
      else if(std::get<3>(Order)==side::BID){
         bids[p].push_back(Order);
         orders[std::get<0>(Order)]= std::prev(bids[p].end());
         return true;
      }
      else  {
        return false ;
      }

   }
   bool CancelOrder(id Id) {
      if(orders.count(Id)) {
         auto &o =orders[Id] ;
         price p = std::get<1>(*o);
         if(std::get<side>(*o)==side::ASK){
            asks[p].erase(o) ;

         }
         if(std::get<side>(*o)==side::BID){
            bids[p].erase(o) ;

         }
         orders.erase(Id);
         return true;
      }   
      return false;
   }
   bool ModifyOrder(order Order) {

    id Id = std::get<0>(Order);

    if(std::get<2>(Order) == 0 || std::get<1>(Order) == 0)
        return false;

    if(!orders.count(Id))
        return false;

    auto &o = orders[Id];

    side oldSide = std::get<3>(*o);
    side newSide = std::get<3>(Order);
    price oldp = std::get<1>(*o);
    price newp = std::get<1>(Order);

    // Même side
    if(oldSide == newSide) {

        // Même prix -> modification simple
        if(std::get<1>(*o) == std::get<1>(Order)) {
            *o = Order;
            return true;
        }

        // Prix changé -> nouvelle priorité FIFO
        if(oldSide == side::ASK) {
            asks[oldp].erase(o);
            asks[newp].push_back(Order);
            orders[Id] = std::prev(asks[newp].end());
        }
        else {
            bids[oldp].erase(o);
            bids[newp].push_back(Order);
            orders[Id] = std::prev(bids[newp].end());
        }

        return true;
    }

    // ASK -> BID
    if(oldSide == side::ASK) {

        asks[oldp].erase(o);
        bids[newp].push_back(Order);
        orders[Id] = std::prev(bids[newp].end());

        return true;
    }

    // BID -> ASK
    if(oldSide == side::BID) {

        bids[newp].erase(o);
        asks[oldp].push_back(Order);
        orders[Id] = std::prev(asks[newp].end());

        return true;
    }

    return false;
}

   void print_order(){
      for(auto a : orders){
         const auto r =a.second ;
         std::cout<<"id   "<< std::get<0>(*r) ;
         std::cout<<"price   "<< std::get<1>(*r) ;
         std::cout<<"amount  "<< std::get<2>(*r) ;
          if (std::get<3>(*r) == side::ASK)
                std::cout << "side     ASK\n";
          else {
             std::cout << "side     BID\n";
            }           
            std::cout << "------------------\n";
      }
   }
   bool ExecuteOrder(order Order) {

    id Id = std::get<0>(Order);
    price Price = std::get<1>(Order);
    quantity Quantity = std::get<2>(Order);
    side Side = std::get<3>(Order);

    // =========================
    // ASK
    // =========================

    if (Side == side::ASK) {

        while (Quantity > 0 && !bids.empty()) {

            order bestBid = BestBid();

            price bidPrice = std::get<1>(bestBid);
            id bidId = std::get<0>(bestBid);
            quantity bidQuantity = std::get<2>(bestBid);

            // No match
            if (Price > bidPrice) {
                break;
            }

            quantity executed =
                std::min(Quantity, bidQuantity);

            std::cout << "TRADE : "
                      << executed
                      << " @ "
                      << bidPrice
                      << '\n';

            // =========================
            // BID completely executed
            // =========================

            if (executed == bidQuantity) {

                CancelOrder(bidId);
            }

            // =========================
            // BID partially executed
            // =========================

            else {

                auto it = orders.find(bidId);

                if (it != orders.end()) {
                    std::get<2>(*it->second) -= executed;
                }
            }

            Quantity -= executed;
        }

        // Remaining ASK goes into book
        if (Quantity > 0) {

            std::get<2>(Order) = Quantity;

            AddOrder(Order);
        }

        return true;
    }


    // =========================
    // BID
    // =========================

    if (Side == side::BID) {

        while (Quantity > 0 && !asks.empty()) {

            order bestAsk = BestAsk();

            price askPrice = std::get<1>(bestAsk);
            id askId = std::get<0>(bestAsk);
            quantity askQuantity = std::get<2>(bestAsk);

            // No match
            if (Price < askPrice) {
                break;
            }

            quantity executed =
                std::min(Quantity, askQuantity);

            std::cout << "TRADE : "
                      << executed
                      << " @ "
                      << askPrice
                      << '\n';

            // =========================
            // ASK completely executed
            // =========================

            if (executed == askQuantity) {

                CancelOrder(askId);
            }

            // =========================
            // ASK partially executed
            // =========================

            else {

                auto it = orders.find(askId);

                if (it != orders.end()) {
                    std::get<2>(*it->second) -= executed;
                }
            }

            Quantity -= executed;
        }

        // Remaining BID goes into book
        if (Quantity > 0) {

            std::get<2>(Order) = Quantity;

            AddOrder(Order);
        }

        return true;
    }

    return false;
}
   order BestBid() {
      if(bids.empty()){
         return order {};
      }
      auto best =  bids.begin()->second ;

      return best.front() ;
   }
   order BestAsk() {
      if(asks.empty()){
         return order{};
      }
      auto best = asks.begin()->second;
     
      return best.front() ;

   }
   double Spread() {
      order ask = BestAsk();
      order bid = BestBid();
      return static_cast<double>(std::get<1>(ask))  - static_cast<double>(std::get<1>(bid));
   }
  
   double MidPrice() {
      order ask = BestAsk();
      order bid = BestBid();

      return (
        static_cast<double>(std::get<1>(ask)) +
        static_cast<double>(std::get<1>(bid))
      ) / 2.0;
   }
private:
   std::unordered_map<id,std::list<order>::iterator > orders ;
   std::map<price ,std::list<ask>> asks ;
   std::map<price,std::list<bid>,std::greater<price>>bids ;
  
};

int main() {

    Orderbook hft;

    // =========================
    // TEST 1 : AddOrder
    // =========================

    std::cout << "TEST 1 : AddOrder\n";

    bool r1 = hft.AddOrder(
        order{10, 200, 10, side::ASK}
    );

    bool r2 = hft.AddOrder(
        order{13, 200, 10, side::BID}
    );

    bool r3 = hft.AddOrder(
        order{15, 200, 10, side::ASK}
    );

    std::cout << "Order 10 : "
              << (r1 ? "OK" : "FAIL") << '\n';

    std::cout << "Order 13 : "
              << (r2 ? "OK" : "FAIL") << '\n';

    std::cout << "Order 15 : "
              << (r3 ? "OK" : "FAIL") << '\n';
    
    // =========================
    // TEST 2 : print_order
    // =========================

    std::cout << "\nTEST 2 : print_order\n";

    hft.print_order();


    // =========================
    // TEST 3 : ModifyOrder
    // =========================

    std::cout << "\nTEST 3 : ModifyOrder\n";

    bool r4 = hft.ModifyOrder(
        order{10, 250, 50, side::ASK}
    );
     std::cout << "Modify order 10 : "
              << (r4 ? "OK" : "FAIL") << '\n';

    hft.print_order();
    std::cout << "\nTEST 6 : Modify ASK -> BID\n";

      bool r7 = hft.ModifyOrder(
         order{10, 300, 100, side::BID}
      );

      std::cout << "Modify order 10 ASK -> BID : "
               << (r7 ? "OK" : "FAIL") << '\n';

      hft.print_order();


    // =========================
    // TEST 4 : CancelOrder
    // =========================

    std::cout << "\nTEST 4 : CancelOrder\n";

    bool r5 = hft.CancelOrder(13);

    std::cout << "Cancel order 13 : "
              << (r5 ? "OK" : "FAIL") << '\n';

    hft.print_order();
    // =========================
    // TEST 5 : Cancel order inexistant
    // =========================

    std::cout << "\nTEST 5 : Cancel inexistant order\n";

    bool r6 = hft.CancelOrder(999);

    std::cout << "Cancel order 999 : "
              << (r6 ? "OK" : "FAIL") << '\n';
    
   std::cout << "\nTEST 7 : Modify BID -> ASK\n";

   bool r8 = hft.ModifyOrder(
      order{10, 350, 200, side::ASK}
   );

   std::cout << "Modify order 10 BID -> ASK : "
            << (r8 ? "OK" : "FAIL") << '\n';

   hft.print_order();

   std::cout << "\nTEST 8 : Duplicate ID\n";

   bool r9 = hft.AddOrder(
      order{20, 100, 10, side::ASK}
   );

   bool r10 = hft.AddOrder(
      order{20, 105, 20, side::ASK}
   );

   std::cout << "First order 20 : "
            << (r9 ? "OK" : "FAIL") << '\n';

   std::cout << "Second order 20 : "
            << (r10 ? "OK" : "FAIL") << '\n';

   hft.print_order();

   std::cout << "\nTEST 9 : Cancel ASK\n";

   bool r11 = hft.CancelOrder(15);

   std::cout << "Cancel order 15 : "
            << (r11 ? "OK" : "FAIL") << '\n';

   hft.print_order();

   std::cout << "\nTEST 10 : Modify inexistant order\n";

   bool r12 = hft.ModifyOrder(
      order{999, 500, 100, side::ASK}
   );

   std::cout << "Modify order 999 : "
            << (r12 ? "OK" : "FAIL") << '\n';

      order bestAsk = hft.BestAsk();

   std::cout << "Best Ask ID       : " << std::get<0>(bestAsk) << '\n';
   std::cout << "Best Ask Price    : " << std::get<1>(bestAsk) << '\n';
   std::cout << "Best Ask Quantity : " << std::get<2>(bestAsk) << '\n';
   hft.AddOrder(order{30, 180, 20, side::BID});
   hft.AddOrder(order{31, 220, 50, side::BID});
   hft.AddOrder(order{32, 150, 30, side::BID});

   order bestBid = hft.BestBid();

   std::cout << "Best Bid ID       : "
            << std::get<0>(bestBid) << '\n';

   std::cout << "Best Bid Price    : "
            << std::get<1>(bestBid) << '\n';

   std::cout << "Best Bid Quantity : "
            << std::get<2>(bestBid) << '\n';
   std::cout << "\nTEST 12 : Spread\n";

   double spread = hft.Spread();

   std::cout << "Spread : "
            << spread << '\n';
   
            std::cout << "\nTEST 13 : MidPrice\n";

   double mid = hft.MidPrice();

   std::cout << "Mid Price : "
            << mid << '\n';

   
    // =====================================================
    // TEST 14 : ASK completely fills BID
    // =====================================================

    {
        std::cout << "\n==============================\n";
        std::cout << "TEST 14 : ASK -> FULL FILL BID\n";
        std::cout << "==============================\n";

        Orderbook hft;

        hft.AddOrder(
            order{1, 100, 30, side::BID}
        );

        std::cout << "\nBefore:\n";
        hft.print_order();

        bool result = hft.ExecuteOrder(
            order{2, 100, 30, side::ASK}
        );

        std::cout << "\nExecute result : "
                  << (result ? "OK" : "FAIL") << '\n';

        std::cout << "\nAfter:\n";
        hft.print_order();
    }


    // =====================================================
    // TEST 15 : ASK partially fills BID
    // =====================================================

    {
        std::cout << "\n==============================\n";
        std::cout << "TEST 15 : ASK -> PARTIAL FILL\n";
        std::cout << "==============================\n";

        Orderbook hft;

        hft.AddOrder(
            order{1, 100, 30, side::BID}
        );

        std::cout << "\nBefore:\n";
        hft.print_order();

        bool result = hft.ExecuteOrder(
            order{2, 100, 50, side::ASK}
        );

        std::cout << "\nExecute result : "
                  << (result ? "OK" : "FAIL") << '\n';

        std::cout << "\nAfter:\n";
        hft.print_order();
    }


    // =====================================================
    // TEST 16 : BID completely fills ASK
    // =====================================================

    {
        std::cout << "\n==============================\n";
        std::cout << "TEST 16 : BID -> FULL FILL ASK\n";
        std::cout << "==============================\n";

        Orderbook hft;

        hft.AddOrder(
            order{1, 100, 30, side::ASK}
        );

        std::cout << "\nBefore:\n";
        hft.print_order();

        bool result = hft.ExecuteOrder(
            order{2, 100, 30, side::BID}
        );

        std::cout << "\nExecute result : "
                  << (result ? "OK" : "FAIL") << '\n';

        std::cout << "\nAfter:\n";
        hft.print_order();
    }


    // =====================================================
    // TEST 17 : BID partially fills ASK
    // =====================================================

    {
        std::cout << "\n==============================\n";
        std::cout << "TEST 17 : BID -> PARTIAL FILL\n";
        std::cout << "==============================\n";

        Orderbook hft;

        hft.AddOrder(
            order{1, 100, 50, side::ASK}
        );

        std::cout << "\nBefore:\n";
        hft.print_order();

        bool result = hft.ExecuteOrder(
            order{2, 100, 30, side::BID}
        );

        std::cout << "\nExecute result : "
                  << (result ? "OK" : "FAIL") << '\n';

        std::cout << "\nAfter:\n";
        hft.print_order();
    }


    // =====================================================
    // TEST 18 : ASK does NOT match BID
    // =====================================================

    {
        std::cout << "\n==============================\n";
        std::cout << "TEST 18 : NO MATCH ASK\n";
        std::cout << "==============================\n";

        Orderbook hft;

        hft.AddOrder(
            order{1, 100, 30, side::BID}
        );

        std::cout << "\nBefore:\n";
        hft.print_order();

        bool result = hft.ExecuteOrder(
            order{2, 110, 50, side::ASK}
        );

        std::cout << "\nExecute result : "
                  << (result ? "OK" : "FAIL") << '\n';

        std::cout << "\nAfter:\n";
        hft.print_order();
    }


    // =====================================================
    // TEST 19 : BID does NOT match ASK
    // =====================================================

    {
        std::cout << "\n==============================\n";
        std::cout << "TEST 19 : NO MATCH BID\n";
        std::cout << "==============================\n";

        Orderbook hft;

        hft.AddOrder(
            order{1, 110, 30, side::ASK}
        );

        std::cout << "\nBefore:\n";
        hft.print_order();

        bool result = hft.ExecuteOrder(
            order{2, 100, 50, side::BID}
        );

        std::cout << "\nExecute result : "
                  << (result ? "OK" : "FAIL") << '\n';

        std::cout << "\nAfter:\n";
        hft.print_order();
    }


    // =====================================================
    // TEST 20 : Multiple ASK levels
    // =====================================================

    {
        std::cout << "\n==============================\n";
        std::cout << "TEST 20 : MULTIPLE ASK LEVELS\n";
        std::cout << "==============================\n";

        Orderbook hft;

        hft.AddOrder(
            order{1, 100, 20, side::ASK}
        );

        hft.AddOrder(
            order{2, 105, 30, side::ASK}
        );

        hft.AddOrder(
            order{3, 110, 40, side::ASK}
        );

        std::cout << "\nBefore:\n";
        hft.print_order();

        bool result = hft.ExecuteOrder(
            order{4, 110, 70, side::BID}
        );

        std::cout << "\nExecute result : "
                  << (result ? "OK" : "FAIL") << '\n';

        std::cout << "\nAfter:\n";
        hft.print_order();
    }

    {
    std::cout << "\n==============================\n";
    std::cout << "TEST 21 : FIFO SAME PRICE\n";
    std::cout << "==============================\n";

    Orderbook hft;

    hft.AddOrder(
        order{1, 100, 20, side::ASK}
    );

    hft.AddOrder(
        order{2, 100, 30, side::ASK}
    );

    hft.AddOrder(
        order{3, 100, 40, side::ASK}
    );

    std::cout << "\nBefore:\n";
    hft.print_order();

    bool result = hft.ExecuteOrder(
        order{4, 100, 35, side::BID}
    );

    std::cout << "\nExecute result : "
              << (result ? "OK" : "FAIL") << '\n';

    std::cout << "\nAfter:\n";
    hft.print_order();
}
   {
    std::cout << "\n==============================\n";
    std::cout << "TEST 22 : PRICE-TIME PRIORITY\n";
    std::cout << "==============================\n";

    Orderbook hft;

    hft.AddOrder(
        order{1, 105, 20, side::ASK}
    );

    hft.AddOrder(
        order{2, 100, 20, side::ASK}
    );

    hft.AddOrder(
        order{3, 100, 20, side::ASK}
    );

    std::cout << "\nBefore:\n";
    hft.print_order();

    bool result = hft.ExecuteOrder(
        order{4, 105, 25, side::BID}
    );

    std::cout << "\nExecute result : "
              << (result ? "OK" : "FAIL") << '\n';

    std::cout << "\nAfter:\n";
    hft.print_order();
}
   {
    std::cout << "\n==============================\n";
    std::cout << "TEST 23 : BID PRICE-TIME PRIORITY\n";
    std::cout << "==============================\n";

    Orderbook hft;

    hft.AddOrder(
        order{1, 95, 20, side::BID}
    );

    hft.AddOrder(
        order{2, 100, 20, side::BID}
    );

    hft.AddOrder(
        order{3, 100, 20, side::BID}
    );

    std::cout << "\nBefore:\n";
    hft.print_order();

    bool result = hft.ExecuteOrder(
        order{4, 95, 25, side::ASK}
    );

    std::cout << "\nExecute result : "
              << (result ? "OK" : "FAIL") << '\n';

    std::cout << "\nAfter:\n";
    hft.print_order();
}
  {
    std::cout << "\n==============================\n";
    std::cout << "TEST 24 : ZERO QUANTITY\n";
    std::cout << "==============================\n";

    Orderbook hft;

    bool result = hft.AddOrder(
        order{10, 100, 0, side::ASK}
    );

    std::cout << "Add zero quantity : "
              << (result ? "OK" : "FAIL") << '\n';

    hft.print_order();
}
{
    std::cout << "\n==============================\n";
    std::cout << "TEST 25 : ZERO PRICE\n";
    std::cout << "==============================\n";

    Orderbook hft;

    bool result = hft.AddOrder(
        order{10, 0, 100, side::ASK}
    );

    std::cout << "Add zero price : "
              << (result ? "OK" : "FAIL") << '\n';

    hft.print_order();
}

    {
    std::cout << "\n==============================\n";
    std::cout << "TEST 26 : MODIFY ZERO QUANTITY\n";
    std::cout << "==============================\n";

    Orderbook hft;

    hft.AddOrder(
        order{10, 100, 50, side::ASK}
    );

    std::cout << "\nBefore:\n";
    hft.print_order();

    bool result = hft.ModifyOrder(
        order{10, 100, 0, side::ASK}
    );

    std::cout << "\nModify quantity to zero : "
              << (result ? "OK" : "FAIL") << '\n';

    std::cout << "\nAfter:\n";
    hft.print_order();
}
{
    std::cout << "\n==============================\n";
    std::cout << "TEST 27 : MODIFY PRICE\n";
    std::cout << "==============================\n";

    Orderbook hft;

    hft.AddOrder(
        order{1, 100, 20, side::ASK}
    );

    hft.AddOrder(
        order{2, 100, 30, side::ASK}
    );

    std::cout << "\nBefore:\n";
    hft.print_order();

    bool result = hft.ModifyOrder(
        order{1, 105, 20, side::ASK}
    );

    std::cout << "\nModify result : "
              << (result ? "OK" : "FAIL") << '\n';

    std::cout << "\nAfter:\n";
    hft.print_order();
}
 {
    std::cout << "\n==============================\n";
    std::cout << "TEST 28 : BEST ASK AFTER MODIFY\n";
    std::cout << "==============================\n";

    Orderbook hft;

    hft.AddOrder(
        order{1, 100, 20, side::ASK}
    );

    hft.AddOrder(
        order{2, 105, 30, side::ASK}
    );

    hft.ModifyOrder(
        order{1, 110, 20, side::ASK}
    );

    order best = hft.BestAsk();

    std::cout << "Best Ask ID       : "
              << std::get<0>(best) << '\n';

    std::cout << "Best Ask Price    : "
              << std::get<1>(best) << '\n';

    std::cout << "Best Ask Quantity : "
              << std::get<2>(best) << '\n';
}
{
    std::cout << "\n==============================\n";
    std::cout << "TEST 29 : MODIFY + FIFO\n";
    std::cout << "==============================\n";

    Orderbook hft;

    hft.AddOrder(
        order{1, 100, 20, side::ASK}
    );

    hft.AddOrder(
        order{2, 100, 30, side::ASK}
    );

    std::cout << "\nInitial:\n";
    hft.print_order();

    hft.ModifyOrder(
        order{1, 105, 20, side::ASK}
    );

    std::cout << "\nAfter price 100 -> 105:\n";
    hft.print_order();

    hft.ModifyOrder(
        order{1, 100, 20, side::ASK}
    );

    std::cout << "\nAfter price 105 -> 100:\n";
    hft.print_order();

    std::cout << "\nExecute BID:\n";

    hft.ExecuteOrder(
        order{3, 100, 20, side::BID}
    );

    std::cout << "\nFinal:\n";
    hft.print_order();
    hft.ModifyOrder(
    order{1, 100, 20, side::ASK}
);

std::cout << "\nASK FIFO:\n";

}
{   
   std::cout << "\n==============================\n";
std::cout << "TEST 30 : MULTI-LEVEL MATCHING\n";
std::cout << "==============================\n";

Orderbook hft;

hft.AddOrder(
    order{1, 100, 20, side::ASK}
);

hft.AddOrder(
    order{2, 100, 30, side::ASK}
);

hft.AddOrder(
    order{3, 105, 40, side::ASK}
);

hft.AddOrder(
    order{4, 110, 50, side::ASK}
);

std::cout << "\nBefore:\n";
hft.print_order();

std::cout << "\nExecute BID 105 x 75:\n";

bool result = hft.ExecuteOrder(
    order{99, 105, 75, side::BID}
);

std::cout << "\nExecute result : "
          << (result ? "OK" : "FAIL")
          << '\n';

std::cout << "\nAfter:\n";
hft.print_order();
}
{
   std::cout << "\n==============================\n";
std::cout << "TEST 31 : MULTI-LEVEL BID MATCHING\n";
std::cout << "==============================\n";

Orderbook hft;

hft.AddOrder(
    order{1, 100, 20, side::BID}
);

hft.AddOrder(
    order{2, 100, 30, side::BID}
);

hft.AddOrder(
    order{3, 95, 40, side::BID}
);

hft.AddOrder(
    order{4, 90, 50, side::BID}
);

std::cout << "\nBefore:\n";
hft.print_order();

std::cout << "\nExecute ASK 95 x 75:\n";

bool result = hft.ExecuteOrder(
    order{99, 95, 75, side::ASK}
);

std::cout << "\nExecute result : "
          << (result ? "OK" : "FAIL")
          << '\n';

std::cout << "\nAfter:\n";
hft.print_order();
}
{
   std::cout << "\n==============================\n";
std::cout << "TEST 32 : EMPTY OPPOSITE BOOK\n";
std::cout << "==============================\n";

Orderbook hft;

std::cout << "\nExecute ASK with empty BID:\n";

bool result = hft.ExecuteOrder(
    order{1, 100, 50, side::ASK}
);

std::cout << "\nExecute result : "
          << (result ? "OK" : "FAIL")
          << '\n';

std::cout << "\nAfter:\n";
hft.print_order();
}
 {
   std::cout << "\n==============================\n";
std::cout << "TEST 33 : EMPTY ASK BOOK\n";
std::cout << "==============================\n";

Orderbook hft;

std::cout << "\nExecute BID with empty ASK:\n";

bool result = hft.ExecuteOrder(
    order{2, 100, 50, side::BID}
);

std::cout << "\nExecute result : "
          << (result ? "OK" : "FAIL")
          << '\n';

std::cout << "\nAfter:\n";

hft.print_order();
 }
 {

       // ==============================
    // TEST 34 : PRICE BOUNDARY
    // ==============================

    std::cout << "\n==============================\n";
    std::cout << "TEST 34 : PRICE BOUNDARY\n";
    std::cout << "==============================\n";


    // ==============================
    // CASE 1 : ASK TROP CHER
    // ==============================

    std::cout << "\nCASE 1 : ASK TROP CHER\n";
    std::cout << "------------------------------\n";

    Orderbook book1;

    book1.AddOrder(
        order{1, 100, 50, side::BID}
    );

    std::cout << "\nBefore:\n";
    book1.print_order();

    std::cout << "\nExecute ASK 101 x 30:\n";

    bool r1 = book1.ExecuteOrder(
        order{2, 101, 30, side::ASK}
    );

    std::cout << "\nExecute result : "
              << (r1 ? "OK" : "FAIL") << '\n';

    std::cout << "\nAfter:\n";
    book1.print_order();


    // ==============================
    // CASE 2 : BID TROP BAS
    // ==============================

    std::cout << "\nCASE 2 : BID TROP BAS\n";
    std::cout << "------------------------------\n";

    Orderbook book2;

    book2.AddOrder(
        order{1, 100, 50, side::ASK}
    );

    std::cout << "\nBefore:\n";
    book2.print_order();

    std::cout << "\nExecute BID 99 x 30:\n";

    bool r2 = book2.ExecuteOrder(
        order{2, 99, 30, side::BID}
    );

    std::cout << "\nExecute result : "
              << (r2 ? "OK" : "FAIL") << '\n';

    std::cout << "\nAfter:\n";
    book2.print_order();


    // ==============================
    // CASE 3 : PRIX EXACT
    // ==============================

    std::cout << "\nCASE 3 : PRIX EXACT\n";
    std::cout << "------------------------------\n";

    Orderbook book3;

    book3.AddOrder(
        order{1, 100, 50, side::BID}
    );

    std::cout << "\nBefore:\n";
    book3.print_order();

    std::cout << "\nExecute ASK 100 x 30:\n";

    bool r3 = book3.ExecuteOrder(
        order{2, 100, 30, side::ASK}
    );

    std::cout << "\nExecute result : "
              << (r3 ? "OK" : "FAIL") << '\n';

    std::cout << "\nAfter:\n";
    book3.print_order();


    // ==============================
    // EXPECTED
    // ==============================

    std::cout << "\n==============================\n";
    std::cout << "EXPECTED\n";
    std::cout << "==============================\n";

    std::cout << "ASK 101 vs BID 100 : NO MATCH\n";
    std::cout << "BID 99 vs ASK 100  : NO MATCH\n";
    std::cout << "ASK 100 vs BID 100 : MATCH\n";
 }
 {
        // ==============================
    // TEST 35 : ID / ITERATOR CONSISTENCY
    // ==============================

    std::cout << "\n==============================\n";
    std::cout << "TEST 35 : ID / ITERATOR CONSISTENCY\n";
    std::cout << "==============================\n";


    // ==============================
    // CASE 1 : ADD
    // ==============================

    std::cout << "\nCASE 1 : ADD\n";
    std::cout << "------------------------------\n";

    Orderbook book1;

    bool r1 = book1.AddOrder(
        order{1, 100, 20, side::ASK}
    );

    bool r2 = book1.AddOrder(
        order{2, 95, 30, side::BID}
    );

    bool r3 = book1.AddOrder(
        order{3, 105, 40, side::ASK}
    );

    std::cout << "ID 1 : "
              << (r1 ? "OK" : "FAIL") << '\n';

    std::cout << "ID 2 : "
              << (r2 ? "OK" : "FAIL") << '\n';

    std::cout << "ID 3 : "
              << (r3 ? "OK" : "FAIL") << '\n';

    std::cout << "\nBook:\n";
    book1.print_order();


    // ==============================
    // CASE 2 : CANCEL
    // ==============================

    std::cout << "\nCASE 2 : CANCEL\n";
    std::cout << "------------------------------\n";

    bool r4 = book1.CancelOrder(1);

    std::cout << "Cancel ID 1 : "
              << (r4 ? "OK" : "FAIL") << '\n';

    std::cout << "\nAfter cancel:\n";
    book1.print_order();


    // ==============================
    // CASE 3 : MODIFY
    // ==============================

    std::cout << "\nCASE 3 : MODIFY\n";
    std::cout << "------------------------------\n";

    bool r5 = book1.ModifyOrder(
        order{3, 110, 50, side::ASK}
    );

    std::cout << "Modify ID 3 : "
              << (r5 ? "OK" : "FAIL") << '\n';

    std::cout << "\nAfter modify:\n";
    book1.print_order();


    // ==============================
    // CASE 4 : MODIFY SIDE
    // ==============================

    std::cout << "\nCASE 4 : MODIFY SIDE\n";
    std::cout << "------------------------------\n";

    bool r6 = book1.ModifyOrder(
        order{3, 100, 50, side::BID}
    );

    std::cout << "Modify ID 3 ASK -> BID : "
              << (r6 ? "OK" : "FAIL") << '\n';

    std::cout << "\nAfter modify:\n";
    book1.print_order();


    // ==============================
    // CASE 5 : EXECUTE
    // ==============================

    std::cout << "\nCASE 5 : EXECUTE\n";
    std::cout << "------------------------------\n";

    Orderbook book2;

    book2.AddOrder(
        order{10, 100, 20, side::ASK}
    );

    book2.AddOrder(
        order{11, 100, 30, side::ASK}
    );

    std::cout << "\nBefore:\n";
    book2.print_order();

    std::cout << "\nExecute BID 100 x 40:\n";

    bool r7 = book2.ExecuteOrder(
        order{20, 100, 40, side::BID}
    );

    std::cout << "\nExecute result : "
              << (r7 ? "OK" : "FAIL") << '\n';

    std::cout << "\nAfter:\n";
    book2.print_order();


    // ==============================
    // FINAL
    // ==============================

    std::cout << "\n==============================\n";
    std::cout << "TEST 35 FINISHED\n";
    std::cout << "==============================\n";
 }

 {
    // ==============================
// TEST 36 : STRESS TEST
// ==============================

std::cout << "\n==============================\n";
std::cout << "TEST 36 : STRESS TEST\n";
std::cout << "==============================\n";

Orderbook book;

const int N = 100000;

auto start = std::chrono::high_resolution_clock::now();

for (id i = 1; i <= N; ++i) {

    side s;

    if (i % 2 == 0)
        s = side::BID;
    else
        s = side::ASK;

    book.AddOrder(
        order{
            i,
            100 + (i % 100),
            10 + (i % 50),
            s
        }
    );
}

auto end = std::chrono::high_resolution_clock::now();

auto duration =
    std::chrono::duration_cast<
        std::chrono::microseconds
    >(end - start);

std::cout << "Orders added : " << N << '\n';

std::cout << "Time : "
          << duration.count()
          << " us\n";

std::cout << "Average : "
          << static_cast<double>(duration.count()) / N
          << " us/order\n";

std::cout << "==============================\n";
 }
 {

    // ===============================
// TEST 37 : STRESS TEST MATCHING
// ===============================

std::cout << "\n==============================\n";
std::cout << "TEST 37 : STRESS TEST MATCHING\n";
std::cout << "==============================\n";

// =================================
// CASE 1 : 100000 ASK -> BID
// =================================

std::cout << "\nCASE 1 : 100000 ASK\n";
std::cout << "------------------------------\n";

{
    Orderbook book;

    const int N = 100000;

    auto start = std::chrono::steady_clock::now();

    for (int i = 1; i <= N; ++i) {
        book.AddOrder(
            order{
                static_cast<id>(i),
                100,
                1,
                side::ASK
            }
        );
    }

    auto end = std::chrono::steady_clock::now();

    auto add_time =
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - start
        ).count();

    std::cout << "Orders added : " << N << '\n';
    std::cout << "Add time     : " << add_time << " us\n";

    std::cout << "\nExecute BID 100 x 50000:\n";

    auto start_match = std::chrono::steady_clock::now();

    bool result = book.ExecuteOrder(
        order{200000, 100, 50000, side::BID}
    );

    auto end_match = std::chrono::steady_clock::now();

    auto match_time =
        std::chrono::duration_cast<std::chrono::microseconds>(
            end_match - start_match
        ).count();

    std::cout << "Execute result : "
              << (result ? "OK" : "FAIL") << '\n';

    std::cout << "Match time : "
              << match_time << " us\n";

    std::cout << "\nRemaining orders:\n";

    book.print_order();
}


// =================================
// CASE 2 : 100000 BID -> ASK
// =================================

std::cout << "\nCASE 2 : 100000 BID\n";
std::cout << "------------------------------\n";

{
    Orderbook book;

    const int N = 100000;

    auto start = std::chrono::steady_clock::now();

    for (int i = 1; i <= N; ++i) {
        book.AddOrder(
            order{
                static_cast<id>(i),
                100,
                1,
                side::BID
            }
        );
    }

    auto end = std::chrono::steady_clock::now();

    auto add_time =
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - start
        ).count();

    std::cout << "Orders added : " << N << '\n';
    std::cout << "Add time     : " << add_time << " us\n";

    std::cout << "\nExecute ASK 100 x 50000:\n";

    auto start_match = std::chrono::steady_clock::now();

    bool result = book.ExecuteOrder(
        order{200000, 100, 50000, side::ASK}
    );

    auto end_match = std::chrono::steady_clock::now();

    auto match_time =
        std::chrono::duration_cast<std::chrono::microseconds>(
            end_match - start_match
        ).count();

    std::cout << "Execute result : "
              << (result ? "OK" : "FAIL") << '\n';

    std::cout << "Match time : "
              << match_time << " us\n";

    std::cout << "\nRemaining orders:\n";

    book.print_order();
}


// =================================
// CASE 3 : FULL BOOK CONSUMPTION
// =================================

std::cout << "\nCASE 3 : FULL BOOK CONSUMPTION\n";
std::cout << "------------------------------\n";

{
    Orderbook book;

    const int N = 100000;

    for (int i = 1; i <= N; ++i) {
        book.AddOrder(
            order{
                static_cast<id>(i),
                100,
                1,
                side::ASK
            }
        );
    }

    std::cout << "Orders before : " << N << '\n';

    auto start = std::chrono::steady_clock::now();

    bool result = book.ExecuteOrder(
        order{
            200000,
            100,
            N,
            side::BID
        }
    );

    auto end = std::chrono::steady_clock::now();

    auto match_time =
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - start
        ).count();

    std::cout << "Execute result : "
              << (result ? "OK" : "FAIL") << '\n';

    std::cout << "Match time : "
              << match_time << " us\n";

    std::cout << "\nAfter execution:\n";

    book.print_order();
}


// =================================
// TEST FINISHED
// =================================

std::cout << "\n==============================\n";
std::cout << "TEST 37 FINISHED\n";
std::cout << "==============================\n";
 }
    return 0;
}

