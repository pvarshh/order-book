#include "order-book/order_book.hpp"

#include <algorithm>
#include <utility>

std::vector<Event> OrderBook::create_order(
        OrderId id,
        std::optional<Price> price,
        Quantity quantity,
        Side side,
        Type type) {
        if (auto rejection = validate_new_order(id, price, quantity, type)) {
                return {*rejection};
        }

        known_order_ids_.insert(id);

        Order order{id, ++sequence_number_, price, quantity, quantity, side, type};
        return submit(order);
}

std::vector<Event> OrderBook::cancel_order(OrderId id) {
        auto order_it = active_orders_.find(id);
        if (order_it == active_orders_.end()) {
                return {rejected(id, "order id not found")};
        }

        Event event = canceled(order_it->second.order);
        erase_active_order(id);
        return {event};
}

std::vector<Event> OrderBook::update_order(
        OrderId id,
        std::optional<Price> price,
        Quantity quantity) {
        auto order_it = active_orders_.find(id);
        if (order_it == active_orders_.end()) {
                return {rejected(id, "order id not found")};
        }
        if (quantity == 0) {
                return {rejected(id, "quantity must be greater than zero")};
        }

        Order order = order_it->second.order;
        if (order.type != Type::Limit) {
                return {rejected(id, "only resting limit orders can be amended")};
        }
        if (!price.has_value()) {
                return {rejected(id, "amended limit order requires a price")};
        }
        if (quantity < order.remaining_quantity) {
                const Quantity reduction = order.remaining_quantity - quantity;
                order.initial_quantity -= reduction;
                order.remaining_quantity = quantity;
                active_orders_.at(id).order = order;
                reduce_resting_quantity(order, reduction);
                return {accepted(order)};
        }

        const bool loses_priority = price != order.price || quantity > order.remaining_quantity;
        if (!loses_priority) {
                return {accepted(order)};
        }

        erase_active_order(id);
        order.sequence_number = ++sequence_number_;
        order.price = price;
        order.initial_quantity = quantity;
        order.remaining_quantity = quantity;

        return submit(order);
}

std::optional<TopOfBook> OrderBook::best_bid() const {
        if (buy_orders_.empty()) {
                return std::nullopt;
        }

        const auto& [price, level] = *buy_orders_.begin();
        return TopOfBook{price, level.aggregate_quantity};
}

std::optional<TopOfBook> OrderBook::best_ask() const {
        if (sell_orders_.empty()) {
                return std::nullopt;
        }

        const auto& [price, level] = *sell_orders_.begin();
        return TopOfBook{price, level.aggregate_quantity};
}

std::optional<Order> OrderBook::read_buy() const {
        if (buy_orders_.empty()) {
                return std::nullopt;
        }

        return active_orders_.at(buy_orders_.begin()->second.orders.front()).order;
}

std::optional<Order> OrderBook::read_sell() const {
        if (sell_orders_.empty()) {
                return std::nullopt;
        }

        return active_orders_.at(sell_orders_.begin()->second.orders.front()).order;
}

bool OrderBook::contains(OrderId id) const {
        return active_orders_.contains(id);
}

std::size_t OrderBook::active_order_count() const {
        return active_orders_.size();
}

std::optional<OrderRejected> OrderBook::validate_new_order(
        OrderId id,
        std::optional<Price> price,
        Quantity quantity,
        Type type) const {
        if (known_order_ids_.contains(id)) {
                return rejected(id, "duplicate order id");
        }
        if (quantity == 0) {
                return rejected(id, "quantity must be greater than zero");
        }
        if (type == Type::Limit && !price.has_value()) {
                return rejected(id, "limit order requires a price");
        }
        if (type == Type::Market && price.has_value()) {
                return rejected(id, "market order cannot have a price");
        }

        return std::nullopt;
}

std::vector<Event> OrderBook::submit(Order& order) {
        auto events = match_order(order);
        if (order.remaining_quantity == 0) {
                return events;
        }

        if (order.type == Type::Limit) {
                rest_order(order);
                events.push_back(accepted(order));
        } else {
                events.push_back(canceled(order));
        }

        return events;
}

std::vector<Event> OrderBook::match_order(Order& order) {
        std::vector<Event> events;

        if (order.side == Side::Buy) {
                match_against(sell_orders_, order, events);
        } else {
                match_against(buy_orders_, order, events);
        }

        return events;
}

template <typename Book>
void OrderBook::match_against(Book& opposite_book, Order& order, std::vector<Event>& events) {
        while (order.remaining_quantity > 0 && would_cross(order)) {
                auto price_it = opposite_book.begin();
                PriceLevel& level = price_it->second;
                OrderId maker_id = level.orders.front();
                auto maker_it = active_orders_.find(maker_id);

                Order& maker = maker_it->second.order;
                const Quantity fill_quantity =
                        std::min(order.remaining_quantity, maker.remaining_quantity);
                const Price match_price = maker.price.value();

                order.remaining_quantity -= fill_quantity;
                maker.remaining_quantity -= fill_quantity;
                level.aggregate_quantity -= fill_quantity;

                events.push_back(executed(
                        maker.id,
                        order.id,
                        match_price,
                        fill_quantity,
                        order.remaining_quantity));

                if (maker.remaining_quantity == 0) {
                        active_orders_.erase(maker_it);
                        level.orders.pop_front();
                        if (level.orders.empty()) {
                                opposite_book.erase(price_it);
                        }
                }
        }
}

void OrderBook::rest_order(const Order& order) {
        const Price price = order.price.value();

        if (order.side == Side::Buy) {
                PriceLevel& level = buy_orders_[price];
                level.orders.push_back(order.id);
                level.aggregate_quantity += order.remaining_quantity;
                active_orders_.emplace(
                        order.id,
                        BookEntry{order, std::prev(level.orders.end())});
                return;
        }

        PriceLevel& level = sell_orders_[price];
        level.orders.push_back(order.id);
        level.aggregate_quantity += order.remaining_quantity;
        active_orders_.emplace(order.id, BookEntry{order, std::prev(level.orders.end())});
}

void OrderBook::erase_active_order(OrderId id) {
        auto order_it = active_orders_.find(id);
        if (order_it == active_orders_.end()) {
                return;
        }

        const BookEntry& entry = order_it->second;
        const Price price = entry.order.price.value();
        if (entry.order.side == Side::Buy) {
                remove_from_queue(
                        buy_orders_,
                        price,
                        entry.order.remaining_quantity,
                        entry.queue_iterator);
        } else {
                remove_from_queue(
                        sell_orders_,
                        price,
                        entry.order.remaining_quantity,
                        entry.queue_iterator);
        }

        active_orders_.erase(order_it);
}

void OrderBook::reduce_resting_quantity(const Order& order, Quantity reduction) {
        const Price price = order.price.value();

        if (order.side == Side::Buy) {
                buy_orders_.at(price).aggregate_quantity -= reduction;
                return;
        }

        sell_orders_.at(price).aggregate_quantity -= reduction;
}

bool OrderBook::would_cross(const Order& order) const {
        if (order.side == Side::Buy) {
                if (sell_orders_.empty()) {
                        return false;
                }
                return order.type == Type::Market || order.price.value() >= sell_orders_.begin()->first;
        }

        if (buy_orders_.empty()) {
                return false;
        }

        return order.type == Type::Market || order.price.value() <= buy_orders_.begin()->first;
}

OrderRejected OrderBook::rejected(OrderId id, std::string reason) {
        return OrderRejected{id, std::move(reason)};
}

OrderAccepted OrderBook::accepted(const Order& order) {
        return OrderAccepted{
                order.id,
                order.price,
                order.initial_quantity,
                order.remaining_quantity};
}

OrderCanceled OrderBook::canceled(const Order& order) {
        return OrderCanceled{order.id, order.price, order.remaining_quantity};
}

OrderExecuted OrderBook::executed(
        OrderId maker_id,
        OrderId taker_id,
        Price match_price,
        Quantity quantity,
        Quantity taker_remaining) {
        return OrderExecuted{maker_id, taker_id, match_price, quantity, taker_remaining};
}
