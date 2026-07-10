# Product Specification: High-Performance Limit Order Book (LOB)


## 1. Executive Summary

This document specifies the functional and non-functional requirements for a core financial market-infrastructure component: a **Deterministic Limit Order Book (LOB)** implemented in C++.

The goal of this system is to match buy and sell orders according to standard price-time priority rules. The emphasis of this specification is on system correctness, strict behavioral deterministic outcomes, operational robustness, and production-grade edge-case handling, rather than algorithmic complexity analysis.


## 2. System Overview

The order book is a real-time data structure that tracks outstanding orders for a single financial instrument. It acts as a deterministic state machine: it accepts an incoming stream of transaction requests (commands), mutates its internal state, and emits a stream of execution events (responses).


## 3. Functional Requirements

### 3.1 Order Types & Core State

The system must support the following core order properties and types:

* **Order Properties:** Every order must retain a unique `OrderID`, `Side` (Buy/Sell), `Price` (fixed-point integer), and `InitialQuantity`. It must track its `RemainingQuantity`.
* **Limit Order:** An order to buy or sell a specified quantity at a specified price or better.
* **Market Order:** An order to buy or sell a specified quantity immediately at the best available market prices. If unfilled or partially filled due to lack of liquidity, the remaining quantity is immediately canceled.

### 3.2 Order Lifecycle Operations

The matching engine must expose an interface supporting four primitive operations:

1. **Add Order:** Insert a new order into the book.
2. **Cancel Order:** Explicitly remove an active order from the book via its `OrderID`. No fills are generated.
3. **Modify Order (Amend):** Alter the `Quantity` or `Price` of an existing order.
* *Volume Reduction:* Reducing quantity must maintain the order’s original time priority.
* *Price Change / Volume Increase:* Any change to price, or an increase in quantity, must cause the order to lose priority and be re-queued at the back of that price level.


4. **Top-of-Book Query:** Instantaneous lookup of the current Best Bid and Best Ask (price and aggregate volume).

### 3.3 Matching & Execution Rules

* **Priority Allocation:** Orders must be matched based on **Price-Time Priority**.
* *Price:* Highest buy orders and lowest sell orders take precedence.
* *Time:* For orders at the same price level, the order that arrived earliest takes precedence.


* **Crossing the Book:** When an incoming order matches an existing resting order, an execution (`Trade`) occurs.
* **Price Improvement:** Incoming aggressive orders must always match against resting orders at the *resting* order's specified price, giving the aggressive order price improvement if available.

### 3.4 Dissemination & Events

The system must emit structured, strongly-typed events for every state change. Every incoming command must result in one or more downstream notifications:

* `OrderAccepted`: Confirms an order is resting.
* `OrderRejected`: Details why a command failed (e.g., invalid price, duplicate ID, cancel for non-existent order).
* `OrderCanceled`: Confirms successful removal.
* `OrderExecuted (Trade)`: Reports a full or partial match, detailing `MatchPrice`, `QuantityFilled`, `MakerOrderID`, and `TakerOrderID`.


## 4. Non-Functional Requirements

### 4.1 Determinism & Predictability

* **State Machine Invariance:** Given the exact same sequence of incoming inputs starting from an empty book, the internal state of the order book and its emitted event sequence must be 100% identical across identical compilations.
* **No Undefined Behavior:** The implementation must enforce strict memory safety. Buffer overflows, dangling pointers, or race conditions are unacceptable.
* **Floating-Point Avoidance:** To prevent rounding errors and non-deterministic behavior across different CPU architectures, all monetary values (prices) must be handled using **fixed-point integer arithmetic** (e.g., prices scaled by $10^4$ or $10^8$).

### 4.2 Error Handling & Robustness

* **Graceful Degradation:** Invalid inputs (e.g., a cancel request for a non-existent order) must be rejected via an event notification without crashing the engine or corrupting the book's internal state.
* **Self-Correction & Overfill Prevention:** The engine must guarantee that an order can never be filled for more than its remaining quantity, even if multiple execution events hit simultaneously.

### 4.3 Data Lifecycle & Auditability

* **Historical Traceability:** The book must support a mechanism to snapshot its state or sequentially log its inputs so that the exact state of the market at any nanosecond in history can be fully reconstructed.
* **Clean Teardown:** The engine must cleanly release all resources, flush outstanding events, and serialize its final state upon an explicit shutdown command.


## 5. Verification & Acceptance Criteria

### 5.1 Correctness Scenarios

The system must pass a suite of behavior-driven test cases covering edge behaviors:

* **Iceberg Tracking Simulation:** Verifying that partial fills exhaustively deplete time priority in the correct order.
* **Self-Trade Prevention:** (Optional/Extensible) Handling scenarios where a participant's buy order matches their own sell order.
* **Market Sweep:** A large market order sweeping across multiple price levels, asserting that the remaining unexecuted portion is canceled exactly when liquidity runs out.

### 5.2 Compilation Constraints

* Must compile under standard modern C++ flags (e.g., `-Wall -Wextra -Werror -pedantic`).
* Must not rely on external third-party proprietary heavy frameworks for its core matching logic; standard library containers or custom structures only.