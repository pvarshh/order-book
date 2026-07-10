#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

enum class Side : std::uint8_t { Buy, Sell };
enum class Type : std::uint8_t { Limit, Market };
using OrderId = std::uint64_t;
using Price = std::uint64_t;
using Quantity = std::uint64_t;
using SequenceNumber = std::uint64_t;

struct Order {
        OrderId id{};
        SequenceNumber sequence_number{};
        std::optional<Price> price;
        Quantity initial_quantity{};
        Quantity remaining_quantity{};
        Side side{};
        Type type{};
};

struct TopOfBook {
        Price price{};
        Quantity aggregate_quantity{};
};

struct OrderAccepted {
        OrderId order_id{};
        std::optional<Price> order_price;
        Quantity quantity{};
        Quantity remaining_quantity{};
};

struct OrderRejected {
        OrderId order_id{};
        std::string reason;
};

struct OrderCanceled {
        OrderId order_id{};
        std::optional<Price> order_price;
        Quantity quantity{};
};

struct OrderExecuted {
        OrderId maker_order_id{};
        OrderId taker_order_id{};
        Price match_price{};
        Quantity quantity{};
        Quantity taker_remaining_quantity{};
};

using Event = std::variant<OrderAccepted, OrderRejected, OrderCanceled, OrderExecuted>;
