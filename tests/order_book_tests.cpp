#include "order-book/order_book.hpp"

#include <cassert>
#include <optional>
#include <vector>

namespace {

template <typename T>
const T& only_event(const std::vector<Event>& events) {
        assert(events.size() == 1);
        return std::get<T>(events[0]);
}

template <typename T>
const T& event_as(const Event& event) {
        return std::get<T>(event);
}

void rejects_duplicate_ids() {
        OrderBook book;

        auto events = book.create_order(1, 100, 10, Side::Buy, Type::Limit);
        only_event<OrderAccepted>(events);

        events = book.create_order(1, 101, 10, Side::Buy, Type::Limit);
        only_event<OrderRejected>(events);
        assert(book.active_order_count() == 1);
}

void top_of_book_aggregates_best_price() {
        OrderBook book;

        book.create_order(1, 100, 10, Side::Buy, Type::Limit);
        book.create_order(2, 100, 15, Side::Buy, Type::Limit);
        book.create_order(3, 99, 20, Side::Buy, Type::Limit);
        book.create_order(4, 105, 7, Side::Sell, Type::Limit);

        auto bid = book.best_bid();
        auto ask = book.best_ask();

        assert(bid.has_value());
        assert(bid->price == 100);
        assert(bid->aggregate_quantity == 25);
        assert(ask.has_value());
        assert(ask->price == 105);
        assert(ask->aggregate_quantity == 7);
}

void top_of_book_aggregate_tracks_mutations() {
        OrderBook book;

        book.create_order(1, 100, 10, Side::Sell, Type::Limit);
        book.create_order(2, 100, 15, Side::Sell, Type::Limit);

        auto ask = book.best_ask();
        assert(ask.has_value());
        assert(ask->aggregate_quantity == 25);

        book.update_order(1, 100, 4);
        ask = book.best_ask();
        assert(ask.has_value());
        assert(ask->aggregate_quantity == 19);

        book.cancel_order(2);
        ask = book.best_ask();
        assert(ask.has_value());
        assert(ask->aggregate_quantity == 4);

        book.create_order(3, 100, 3, Side::Buy, Type::Limit);
        ask = book.best_ask();
        assert(ask.has_value());
        assert(ask->aggregate_quantity == 1);
}

void limit_order_matches_resting_price() {
        OrderBook book;

        book.create_order(1, 95, 10, Side::Sell, Type::Limit);
        auto events = book.create_order(2, 100, 4, Side::Buy, Type::Limit);

        const OrderExecuted& trade = only_event<OrderExecuted>(events);
        assert(trade.maker_order_id == 1);
        assert(trade.taker_order_id == 2);
        assert(trade.match_price == 95);
        assert(trade.quantity == 4);

        auto ask = book.best_ask();
        assert(ask.has_value());
        assert(ask->price == 95);
        assert(ask->aggregate_quantity == 6);
        assert(!book.contains(2));
}

void fifo_priority_at_same_price() {
        OrderBook book;

        book.create_order(1, 100, 5, Side::Sell, Type::Limit);
        book.create_order(2, 100, 5, Side::Sell, Type::Limit);
        auto events = book.create_order(3, 100, 7, Side::Buy, Type::Limit);

        assert(events.size() == 2);
        const OrderExecuted& first_trade = event_as<OrderExecuted>(events[0]);
        const OrderExecuted& second_trade = event_as<OrderExecuted>(events[1]);
        assert(first_trade.maker_order_id == 1);
        assert(first_trade.quantity == 5);
        assert(second_trade.maker_order_id == 2);
        assert(second_trade.quantity == 2);

        auto ask = book.read_sell();
        assert(ask.has_value());
        assert(ask->id == 2);
        assert(ask->remaining_quantity == 3);
}

void market_order_sweeps_and_cancels_remainder() {
        OrderBook book;

        book.create_order(1, 100, 5, Side::Sell, Type::Limit);
        book.create_order(2, 101, 6, Side::Sell, Type::Limit);
        auto events = book.create_order(3, std::nullopt, 20, Side::Buy, Type::Market);

        assert(events.size() == 3);
        const OrderExecuted& first_trade = event_as<OrderExecuted>(events[0]);
        const OrderExecuted& second_trade = event_as<OrderExecuted>(events[1]);
        const OrderCanceled& cancellation = event_as<OrderCanceled>(events[2]);
        assert(first_trade.maker_order_id == 1);
        assert(first_trade.match_price == 100);
        assert(first_trade.quantity == 5);
        assert(second_trade.maker_order_id == 2);
        assert(second_trade.match_price == 101);
        assert(second_trade.quantity == 6);
        assert(cancellation.order_id == 3);
        assert(cancellation.quantity == 9);
        assert(book.active_order_count() == 0);
        assert(!book.contains(3));
}

void cancel_removes_order() {
        OrderBook book;

        book.create_order(1, 100, 5, Side::Buy, Type::Limit);
        auto events = book.cancel_order(1);

        only_event<OrderCanceled>(events);
        assert(!book.best_bid().has_value());
        assert(!book.contains(1));
}

void reducing_quantity_keeps_priority() {
        OrderBook book;

        book.create_order(1, 100, 10, Side::Sell, Type::Limit);
        book.create_order(2, 100, 10, Side::Sell, Type::Limit);
        auto update_events = book.update_order(1, 100, 4);
        only_event<OrderAccepted>(update_events);

        auto events = book.create_order(3, 100, 5, Side::Buy, Type::Limit);
        assert(events.size() == 2);
        const OrderExecuted& first_trade = event_as<OrderExecuted>(events[0]);
        const OrderExecuted& second_trade = event_as<OrderExecuted>(events[1]);
        assert(first_trade.maker_order_id == 1);
        assert(first_trade.quantity == 4);
        assert(second_trade.maker_order_id == 2);
        assert(second_trade.quantity == 1);
}

void price_change_loses_priority() {
        OrderBook book;

        book.create_order(1, 100, 10, Side::Sell, Type::Limit);
        book.create_order(2, 100, 10, Side::Sell, Type::Limit);
        auto update_events = book.update_order(1, 101, 10);
        only_event<OrderAccepted>(update_events);

        auto best = book.read_sell();
        assert(best.has_value());
        assert(best->id == 2);
}

}  // namespace

int main() {
        rejects_duplicate_ids();
        top_of_book_aggregates_best_price();
        top_of_book_aggregate_tracks_mutations();
        limit_order_matches_resting_price();
        fifo_priority_at_same_price();
        market_order_sweeps_and_cancels_remainder();
        cancel_removes_order();
        reducing_quantity_keeps_priority();
        price_change_loses_priority();
}
