#pragma once

#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "types.hpp"

class OrderBook {
public:
        std::vector<Event> create_order(
                OrderId id,
                std::optional<Price> price,
                Quantity quantity,
                Side side,
                Type type);

        std::vector<Event> cancel_order(OrderId id);

        std::vector<Event> update_order(
                OrderId id,
                std::optional<Price> price,
                Quantity quantity);

        [[nodiscard]] std::optional<TopOfBook> best_bid() const;
        [[nodiscard]] std::optional<TopOfBook> best_ask() const;
        [[nodiscard]] std::optional<Order> read_buy() const;
        [[nodiscard]] std::optional<Order> read_sell() const;
        [[nodiscard]] bool contains(OrderId id) const;
        [[nodiscard]] std::size_t active_order_count() const;

private:
        struct PriceLevel {
                std::list<OrderId> orders;
                Quantity aggregate_quantity{};
        };

        using BuyBook = std::map<Price, PriceLevel, std::greater<Price>>;
        using SellBook = std::map<Price, PriceLevel, std::less<Price>>;

        struct BookEntry {
                Order order;
                std::list<OrderId>::iterator queue_iterator;
        };

        SequenceNumber sequence_number_ = 0;
        BuyBook buy_orders_;
        SellBook sell_orders_;
        std::unordered_map<OrderId, BookEntry> active_orders_;
        std::unordered_set<OrderId> known_order_ids_;

        [[nodiscard]] std::optional<OrderRejected> validate_new_order(
                OrderId id,
                std::optional<Price> price,
                Quantity quantity,
                Type type) const;

        std::vector<Event> submit(Order& order);
        std::vector<Event> match_order(Order& order);

        template <typename Book>
        void match_against(Book& opposite_book, Order& order, std::vector<Event>& events);

        template <typename Book>
        void remove_from_queue(
                Book& book,
                Price price,
                Quantity quantity,
                std::list<OrderId>::iterator it) {
                auto price_it = book.find(price);
                if (price_it == book.end()) { return; }

                PriceLevel& level = price_it->second;
                level.aggregate_quantity -= quantity;
                level.orders.erase(it);
                if (level.orders.empty()) { book.erase(price_it); }
        }

        void rest_order(const Order& order);
        void erase_active_order(OrderId id);
        void reduce_resting_quantity(const Order& order, Quantity reduction);
        [[nodiscard]] bool would_cross(const Order& order) const;

        static OrderRejected rejected(OrderId id, std::string reason);
        static OrderAccepted accepted(const Order& order);
        static OrderCanceled canceled(const Order& order);
        static OrderExecuted executed(
                OrderId maker_id,
                OrderId taker_id,
                Price match_price,
                Quantity quantity,
                Quantity taker_remaining);
};
