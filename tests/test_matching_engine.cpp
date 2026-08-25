#include <gtest/gtest.h>
#include "base_lob_engine.h" 
#include "market_state.h"
#include "events.h" 
#include <deque>
#include <cmath>


class MatchingEngineTest : public ::testing::Test {
protected:
    Engine<EngineMode::Simulation> engine ; // 1. Simulates exchange
    Engine<EngineMode::Parser> parser_engine ; // 2. Tracks LobState via ITCH
    
    std::deque<Event> feed;
    uint16_t test_stock = static_cast<uint16_t>(Symbol::AAPL);
    uint64_t seq_num = 1; // already defined in lob engine as global variable. it should directly be used.

    void SetUp() override {
        feed.clear();
        // set tick size = 1 ; // precision (2)
        engine.books[test_stock].TICK_SIZE = 1 ;
        parser_engine.books[test_stock].TICK_SIZE = 1 ; 
        seq_num = 1 ;
    }
    
    void process_feed_to_parser(){
    
      while (!feed.empty()){
        auto event = feed.front() ;
        feed.pop_front() ;
        if (event.event_type == EventType::ITCH){
          reconstruct_market_state(parser_engine, event) ;
        }
      }
    }

    // Helper to generate inbound OUCH EnterOrder events
    Event create_enter_order(uint64_t order_id, uint32_t price, uint32_t shares, char side) {
        Event ev;
        ev.timestamp = engine.books[test_stock].clock; 
        ev.sequence_num = seq_num++;
        ev.event_type = EventType::OUCH;
        ev.msg_type = MsgType::EnterOrder;
        ev.stock_locate = test_stock;
        
        EnterOrder req;
        req.order_id = order_id;
        req.price = price;
        req.shares = shares;
        req.side = side;
        req.time_in_force = 0; // DAY
        ev.p.order_req = req;
        
        return ev;
    }
    
    // Helper to generate inbound OUCH CancelReq events
    Event create_cancel_order(uint64_t order_id, uint32_t max_shares) {
        Event ev;
        ev.timestamp = engine.books[test_stock].clock; 
        ev.sequence_num = seq_num++;
        ev.event_type = EventType::OUCH;
        ev.msg_type = MsgType::CancelReq;
        ev.stock_locate = test_stock;
        
        CancelReq req;
        req.order_id = order_id;
        req.max_shares = max_shares;
        ev.p.cancel_req = req;
        return ev;
    }

    // Helper to generate inbound OUCH ReplaceReq events
    Event create_replace_order(uint64_t old_id, uint64_t new_id, uint32_t price, uint32_t shares) {
        Event ev;
        ev.timestamp = engine.books[test_stock].clock; 
        ev.sequence_num = seq_num++;
        ev.event_type = EventType::OUCH;
        ev.msg_type = MsgType::ReplaceReq;
        ev.stock_locate = test_stock;
        
        ReplaceReq req;
        req.old_id = old_id;
        req.new_id = new_id;
        req.price = price;
        req.shares = shares;
        ev.p.replace_req = req;
        return ev;
    }
};

// 1. Test a simple passive limit order addition
TEST_F(MatchingEngineTest, PassiveLimitOrder_RestsInBook) {
    // Action: Send Buy order for 100 shares at $50.00
    Event ev = create_enter_order(1001, 5000, 100, 'B');
    engine.process_ouch_request(ev, feed , seq_num);

    // Assert LOB State
    ASSERT_TRUE(engine.orders_by_id.contains(1001));
    auto& bid_map = engine.books[test_stock].bid_map;
    EXPECT_EQ(bid_map[5000].total_volume, 100);

    // Assert Feed Generation (Should be 2 messages: S_OUCH Resting [0], ITCH Add [1])
    ASSERT_EQ(feed.size(), 2);
    
    EXPECT_EQ(feed[0].msg_type, MsgType::OrderResting);
    EXPECT_EQ(feed[0].p.order_resting.order_id, 1001);
    
    EXPECT_EQ(feed[1].msg_type, MsgType::OrderAdd);
    EXPECT_EQ(feed[1].p.itch_add.order_id, 1001);
}

// 2. Test an exact fill (Aggressive hits Passive perfectly)
TEST_F(MatchingEngineTest, AggressiveOrder_ExactFill) {
    // Setup: Resting Buy order for 100 shares at $50.00
    Event ev1 = create_enter_order(1001, 5000, 100, 'B');
    engine.process_ouch_request(ev1, feed, seq_num);
    feed.clear(); // Clear setup events from feed

    // Action: Send aggressive Sell order for 100 shares at $50.00
    Event ev2 = create_enter_order(1002, 5000, 100, 'S');
    engine.process_ouch_request(ev2, feed, seq_num);

    // Assert LOB State
    EXPECT_FALSE(engine.orders_by_id.contains(1001)); // Passive should be deleted
    EXPECT_FALSE(engine.orders_by_id.contains(1002)); // Aggressive never rests

    // Assert Feed: 1 Fill S_OUCH [0], 1 execution ITCH [1]
    ASSERT_EQ(feed.size(), 2);
    
    EXPECT_EQ(feed[0].msg_type, MsgType::Fill);
    EXPECT_EQ(feed[0].p.fill.agg_order_id, 1002);
    EXPECT_EQ(feed[0].p.fill.pass_order_id, 1001);
    EXPECT_EQ(feed[0].p.fill.fill_shares, 100);

    EXPECT_EQ(feed[1].msg_type, MsgType::OrderExec);
    EXPECT_EQ(feed[1].p.itch_execute.order_id, 1001); // Execution refers to resting order
    EXPECT_EQ(feed[1].p.itch_execute.executed_shares, 100);
}

// 3. Test Partial Fill where Aggressive order survives and rests
TEST_F(MatchingEngineTest, PartialFill_AggressiveSurvivesAndRests) {
    // Setup: Resting Buy order for 50 shares at $50.00
    Event ev1 = create_enter_order(1001, 5000, 50, 'B');
    engine.process_ouch_request(ev1, feed, seq_num);
    feed.clear();

    // Action: Send aggressive Sell order for 100 shares at $50.00
    Event ev2 = create_enter_order(1002, 5000, 100, 'S');
    engine.process_ouch_request(ev2, feed, seq_num);

    // Assert LOB State
    EXPECT_FALSE(engine.orders_by_id.contains(1001)); // First order fully filled
    ASSERT_TRUE(engine.orders_by_id.contains(1002));  // Second order rests

    auto& ask_map = engine.books[test_stock].ask_map;
    EXPECT_EQ(ask_map[5000].total_volume, 50); // 50 shares should remain on the ask

    // Assert Feed: Fill S_OUCH [0], Exec ITCH [1], Resting S_OUCH [2], Add ITCH [3]
    ASSERT_EQ(feed.size(), 4);
    
    EXPECT_EQ(feed[1].msg_type, MsgType::OrderExec);
    EXPECT_EQ(feed[1].p.itch_execute.executed_shares, 50);
    
    EXPECT_EQ(feed[3].msg_type, MsgType::OrderAdd);
    EXPECT_EQ(feed[3].p.itch_add.order_id, 1002);
    EXPECT_EQ(feed[3].p.itch_add.shares, 50); // Adds only the remaining 50
}

// 4. Test Partial Fill where Passive order survives
TEST_F(MatchingEngineTest, PartialFill_PassiveSurvives) {
    // Setup: Resting Buy order for 100 shares at $50.00
    Event ev1 = create_enter_order(1001, 5000, 100, 'B');
    engine.process_ouch_request(ev1, feed, seq_num);
    feed.clear();

    // Action: Send aggressive Sell order for 50 shares at $50.00
    Event ev2 = create_enter_order(1002, 5000, 50, 'S');
    engine.process_ouch_request(ev2, feed, seq_num);

    // Assert LOB State
    ASSERT_TRUE(engine.orders_by_id.contains(1001));  // Passive survives
    EXPECT_FALSE(engine.orders_by_id.contains(1002)); // Aggressive fully filled

    uint32_t pool_index = engine.orders_by_id[1001];
    EXPECT_EQ(engine.pool[pool_index].shares, 50);    // 50 shares remain

    // Assert Feed (Fill is [0], Exec is [1])
    ASSERT_EQ(feed.size(), 2);
    EXPECT_EQ(feed[1].msg_type, MsgType::OrderExec);
    EXPECT_EQ(feed[1].p.itch_execute.executed_shares, 50);
}

// 5. Test Market Order exhausting the book and rejecting remainder
TEST_F(MatchingEngineTest, MarketOrder_ExhaustsBookAndRejectsRemainder) {
    // Setup: Two resting Buy orders at different prices
    // 50 shares at $50.00, 50 shares at $49.00
    auto ev1 = create_enter_order(1001, 5000, 50, 'B') ;
    auto ev2 = create_enter_order(1002, 4900, 50, 'B') ;
    engine.process_ouch_request(ev1, feed, seq_num);
    engine.process_ouch_request(ev2, feed, seq_num);
    feed.clear();

    // Action: Send Market Sell order for 150 shares
    Event ev3 = create_enter_order(1003, UINT32_MAX, 150, 'S'); 
    engine.process_ouch_request(ev3, feed, seq_num);

    // Assert LOB State
    EXPECT_FALSE(engine.orders_by_id.contains(1001));
    EXPECT_FALSE(engine.orders_by_id.contains(1002));
    EXPECT_FALSE(engine.orders_by_id.contains(1003)); // Market order should not rest
    
    EXPECT_TRUE(engine.books[test_stock].bid_map.empty()); // Book should be empty

    // Assert Feed: 2 Fill S_OUCH, 2 Exec ITCH, 1 Reject S_OUCH
    ASSERT_EQ(feed.size(), 5);
    
    // First fill against $50 level (Fill is 0, Exec is 1)
    EXPECT_EQ(feed[1].msg_type, MsgType::OrderExec);
    EXPECT_EQ(feed[1].p.itch_execute.order_id, 1001);
    
    // Second fill against $49 level (Fill is 2, Exec is 3)
    EXPECT_EQ(feed[3].msg_type, MsgType::OrderExec);
    EXPECT_EQ(feed[3].p.itch_execute.order_id, 1002);
    
    // Final rejection for the remaining 50 shares
    EXPECT_EQ(feed[4].msg_type, MsgType::OrderRej);
    EXPECT_EQ(feed[4].p.order_rej.order_id, 1003);
    EXPECT_EQ(feed[4].p.order_rej.reason, Reason::book_empty);
}

// 6. Test Full Cancel and Clock Timing
TEST_F(MatchingEngineTest, CancelOrder_FullDelete_WithClockTiming) {
    // Initial clock should be 0.
    EXPECT_EQ(engine.books[test_stock].clock, 0);

    // Setup: Add order
    auto ev1 = create_enter_order(1001, 5000, 100, 'B') ;
    engine.process_ouch_request(ev1, feed, seq_num);
    // Clock after Add: PT_BASE (5000) + PT_ADD_ORDER (250) = 5250
    EXPECT_EQ(engine.books[test_stock].clock, 5250);
    feed.clear();

    // Action: Full Cancel (max_shares = 0)
    auto ev2 = create_cancel_order(1001, 0) ;
    engine.process_ouch_request(ev2, feed, seq_num);

    // Clock after Cancel: 5250 + PT_BASE (5000) + PT_CANCEL (100) = 10350
    EXPECT_EQ(engine.books[test_stock].clock, 10350);

    // Assert State
    EXPECT_FALSE(engine.orders_by_id.contains(1001));

    // Assert Feed
    ASSERT_EQ(feed.size(), 2);
    
    EXPECT_EQ(feed[0].msg_type, MsgType::CancelSucss);
    EXPECT_EQ(feed[0].timestamp, 10350); // Verify OUCH timestamp matches lob clock
    EXPECT_EQ(feed[0].p.cancal_sucss.remaining_shares, 0);

    EXPECT_EQ(feed[1].msg_type, MsgType::OrderCancel);
    EXPECT_EQ(feed[1].timestamp, 10350); // Verify ITCH timestamp matches lob clock
    EXPECT_EQ(feed[1].p.itch_cancel.cancelled_shares, 100);
}

// 7. Test Partial Cancel
TEST_F(MatchingEngineTest, CancelOrder_PartialReduce) {
    auto ev1 = create_enter_order(1001, 5000, 100, 'B') ;
    engine.process_ouch_request(ev1, feed, seq_num);
    feed.clear();
    // Action: Partial Cancel (target max_shares = 60, meaning cancel 40)
    auto ev2 = create_cancel_order(1001, 60) ;
    engine.process_ouch_request(ev2, feed, seq_num);

    // Assert State
    ASSERT_TRUE(engine.orders_by_id.contains(1001));
    uint32_t pool_index = engine.orders_by_id[1001];
    EXPECT_EQ(engine.pool[pool_index].shares, 60);

    // Assert Feed
    ASSERT_EQ(feed.size(), 2);
    
    EXPECT_EQ(feed[0].msg_type, MsgType::CancelSucss);
    EXPECT_EQ(feed[0].p.cancal_sucss.remaining_shares, 60);

    EXPECT_EQ(feed[1].msg_type, MsgType::OrderCancel);
    EXPECT_EQ(feed[1].p.itch_cancel.cancelled_shares, 40);
}

// 8. Test Replace Order and Clock Timing
TEST_F(MatchingEngineTest, ReplaceOrder_Success_WithClockTiming) {
    auto ev1 = create_enter_order(1001, 5000, 100, 'B') ;
    engine.process_ouch_request(ev1, feed, seq_num);
    
    uint64_t current_clock = engine.books[test_stock].clock; // Should be 5250
    feed.clear();

    // Action: Replace order 1001 with 1002, new price 5005, new shares 200
    auto ev2 = create_replace_order(1001, 1002, 5005, 200) ;
    engine.process_ouch_request(ev2, feed, seq_num);

    // Replace Clock: Current + PT_BASE (5000) + PT_CANCEL (100) + PT_ADD_ORDER (250) = 10600
    EXPECT_EQ(engine.books[test_stock].clock, current_clock + 5000 + 100 + 250);

    // Assert State
    EXPECT_FALSE(engine.orders_by_id.contains(1001));
    ASSERT_TRUE(engine.orders_by_id.contains(1002));
    EXPECT_EQ(engine.pool[engine.orders_by_id[1002]].price, 5005);
    EXPECT_EQ(engine.pool[engine.orders_by_id[1002]].shares, 200);

    // Assert Feed
    ASSERT_EQ(feed.size(), 2);
    
    EXPECT_EQ(feed[0].msg_type, MsgType::ReplaceSucss);
    EXPECT_EQ(feed[0].p.replace_sucss.cancelled_shares, 100);

    EXPECT_EQ(feed[1].msg_type, MsgType::OrderReplace);
    EXPECT_EQ(feed[1].p.itch_replace.old_id, 1001);
    EXPECT_EQ(feed[1].p.itch_replace.new_id, 1002);
}

// 9. Test Rejection Paths (Invalid inputs)
TEST_F(MatchingEngineTest, RejectionPaths_InvalidInputs) {
    // 9a. EnterOrder Rejection (0 shares)
    Event ev1 = create_enter_order(1001, 5000, 0, 'B');
    engine.process_ouch_request(ev1, feed, seq_num);
    
    ASSERT_EQ(feed.size(), 1);
    EXPECT_EQ(feed[0].msg_type, MsgType::OrderRej);
    EXPECT_EQ(feed[0].p.order_rej.reason, Reason::invalid_order);
    EXPECT_FALSE(engine.orders_by_id.contains(1001));
    feed.clear();

    // 9b. CancelReq Rejection (Order ID not found)
    Event ev2 = create_cancel_order(9999, 0); // 9999 doesn't exist
    engine.process_ouch_request(ev2, feed, seq_num);

    ASSERT_EQ(feed.size(), 1);
    EXPECT_EQ(feed[0].msg_type, MsgType::CancelRej);
    EXPECT_EQ(feed[0].p.cancel_rej.reason, Reason::order_id_not_found);
    feed.clear();

    // 9c. ReplaceReq Rejection (Invalid Price per TICK_SIZE, assuming TICK_SIZE = 1 this might pass, 
    // but we can test size 0 instead to guarantee rejection)
    auto ev4 = create_enter_order(1001, 5000, 100, 'B') ;
    engine.process_ouch_request(ev4, feed, seq_num); // Add valid order
    feed.clear();

    Event ev3 = create_replace_order(1001, 1002, 5000, 0); // 0 shares invalid
    engine.process_ouch_request(ev3, feed, seq_num);

    ASSERT_EQ(feed.size(), 1);
    EXPECT_EQ(feed[0].msg_type, MsgType::ReplaceRej);
    EXPECT_EQ(feed[0].p.replace_rej.reason, Reason::invalid_order);
}

// 10. The Ultimate Clock Test: Walking the Book
TEST_F(MatchingEngineTest, LevelWalk_ClockTiming_Complex) {
    // Setup 3 orders over 2 price levels
    auto ev1 = create_enter_order(1001, 5000, 50, 'B') ;
    engine.process_ouch_request(ev1, feed, seq_num);
    auto ev2 = create_enter_order(1002, 5000, 50, 'B') ;
    engine.process_ouch_request(ev2, feed, seq_num);
    auto ev3 = create_enter_order(1003, 4900, 50, 'B') ;
    engine.process_ouch_request(ev3, feed, seq_num);
    feed.clear();

    uint64_t start_clock = engine.books[test_stock].clock;

    // Action: Aggressive sell that wipes 5000, walks to 4900, and fills fully.
    auto ev4 = create_enter_order(1004, 4900, 150, 'S') ;
    engine.process_ouch_request(ev4, feed, seq_num);

    // Expected Clock math:
    // Base: + 5000
    // Fill 1001: + 10 (PT_ORDER_FILL)
    // Fill 1002: + 10 (PT_ORDER_FILL)
    // Exhaust level 5000 -> walk: + 100 (PT_LEVEL_WALK)
    // Fill 1003: + 10 (PT_ORDER_FILL)
    uint64_t expected_clock = start_clock + 5000 + 10 + 10 + 100 + 10;
    
    EXPECT_EQ(engine.books[test_stock].clock, expected_clock);

    // Verify exactly when events fired
    ASSERT_EQ(feed.size(), 6); // 3 pairs of (Fill, Exec)

    // First fill timestamp (after Base + Fill 1) -> ITCH Exec is at index 1
    EXPECT_EQ(feed[1].timestamp, start_clock + 5000 + 10);
    EXPECT_EQ(feed[1].p.itch_execute.order_id, 1001);

    // Second fill timestamp (after Fill 2) -> ITCH Exec is at index 3
    EXPECT_EQ(feed[3].timestamp, start_clock + 5000 + 20); 
    EXPECT_EQ(feed[3].p.itch_execute.order_id, 1002);

    // Third fill timestamp (after Level Walk + Fill 3) -> ITCH Exec is at index 5
    EXPECT_EQ(feed[5].timestamp, expected_clock); 
    EXPECT_EQ(feed[5].p.itch_execute.order_id, 1003);
}


// =====================================================================
// NEW LOBSTATE INTEGRATION TESTS (11 - 13)
// =====================================================================

// 11. Test that adding Buy and Sell limit orders updates best bid/ask, mid price, spread, and shares
TEST_F(MatchingEngineTest, LobState_SpreadAndMidPrice_AfterAdd) {
    // Action 1: Add Buy Order 1001 at $50.00 (5000) for 100 shares
    Event ev1 = create_enter_order(1001, 5000, 100, 'B');
    engine.process_ouch_request(ev1, feed, seq_num);
    process_feed_to_parser(); // Push ITCH to Parser

    auto& state = parser_engine.books[test_stock].state;

    // Expectation after Buy Add:
    // Only Bid exists. When ask is 0, mid_price equals best_bid. Spread is 0.
    EXPECT_EQ(state.best_bid, 5000);
    EXPECT_EQ(state.best_ask, 0);
    EXPECT_EQ(state.mid_price, 5000);
    EXPECT_EQ(state.spread, 0);
    EXPECT_EQ(state.buy_shares, 100);
    EXPECT_EQ(state.sell_shares, 0);

    // Action 2: Add Sell Order 1002 at $50.10 (5010) for 200 shares
    Event ev2 = create_enter_order(1002, 5010, 200, 'S');
    engine.process_ouch_request(ev2, feed, seq_num);
    process_feed_to_parser();

    // Expectation after Sell Add:
    // Both Bid (5000) and Ask (5010) exist!
    // Spread = 5010 - 5000 = 10.
    // Midpoint = (5010 + 5000) / 2 = 5005. (Since TICK_SIZE=1, 5005 % 1 == 0, mid_price is 5005).
    EXPECT_EQ(state.best_bid, 5000);
    EXPECT_EQ(state.best_ask, 5010);
    EXPECT_EQ(state.spread, 10);
    EXPECT_EQ(state.mid_price, 5005);
    EXPECT_EQ(state.buy_shares, 100);
    EXPECT_EQ(state.sell_shares, 200);
}

// 12. Test executions updating Volume, Last Trade Price/Size, and Order Imbalance
TEST_F(MatchingEngineTest, LobState_TradeExecution_VolumeAndImbalance) {
    // Setup: Passive Buy at $50.00 (100 shares) & Passive Sell at $50.20 (100 shares)
    auto ev1 = create_enter_order(1001, 5000, 100, 'B') ;
    engine.process_ouch_request(ev1, feed, seq_num);
    auto ev2 = create_enter_order(1002, 5020, 100, 'S') ;
    engine.process_ouch_request(ev2, feed, seq_num);
    process_feed_to_parser();

    auto& state = parser_engine.books[test_stock].state;
    EXPECT_EQ(state.total_volume, 0);

    // Action: Aggressive Sell hits the Bid (50 shares at $50.00)
    Event ev3 = create_enter_order(1003, 5000, 50, 'S');
    engine.process_ouch_request(ev3, feed, seq_num);
    process_feed_to_parser();

    // Expectation after 50 share fill:
    // The resting order was on the BID side ('B'). So bid_executed_shares increases by 50.
    // Total volume traded = 50.
    // Order Imbalance = (bid_executed - ask_executed) / total = (50 - 0) / 50 = +1.0.
    EXPECT_EQ(state.last_trade_price, 5000);
    EXPECT_EQ(state.last_trade_size, 50);
    EXPECT_EQ(state.total_volume, 50);
    EXPECT_EQ(state.bid_executed_shares, 50);
    EXPECT_EQ(state.ask_executed_shares, 0);
    EXPECT_DOUBLE_EQ(state.order_imbalance, 1.0);
    EXPECT_EQ(state.buy_shares, 50); // 100 - 50 filled = 50 left

    // Action 2: Aggressive Buy hits the Ask (25 shares at $50.20)
    Event ev4 = create_enter_order(1004, 5020, 25, 'B');
    engine.process_ouch_request(ev4, feed, seq_num);
    process_feed_to_parser();

    // Expectation after second trade:
    // The resting order was on the ASK side ('S'). ask_executed_shares increases by 25.
    // Total volume traded = 50 + 25 = 75.
    // Imbalance = (50 - 25) / 75 = 25 / 75 = 0.333333...
    EXPECT_EQ(state.last_trade_price, 5020);
    EXPECT_EQ(state.last_trade_size, 25);
    EXPECT_EQ(state.total_volume, 75);
    EXPECT_EQ(state.ask_executed_shares, 25);
    EXPECT_NEAR(state.order_imbalance, 0.3333333, 1e-5);
    EXPECT_EQ(state.sell_shares, 75); // 100 - 25 filled = 75 left
}

// 13. Test that full cancels and level walks cleanly update Best Ask and reset spread
TEST_F(MatchingEngineTest, LobState_CancelAndLevelWalk_UpdatesBestPrices) {
    // Setup: Ask level 1 at $50.10 (50 shares), Ask level 2 at $50.20 (50 shares)
    auto ev1 = create_enter_order(1001, 5010, 50, 'S') ;
    engine.process_ouch_request(ev1, feed, seq_num);
    auto ev2 = create_enter_order(1002, 5020, 50, 'S') ;
    engine.process_ouch_request(ev2, feed, seq_num);
    auto ev3 = create_enter_order(1003, 5000, 50, 'B') ;
    engine.process_ouch_request(ev3, feed, seq_num);
    process_feed_to_parser();

    auto& state = parser_engine.books[test_stock].state;
    EXPECT_EQ(state.best_ask, 5010);
    EXPECT_EQ(state.spread, 10); // 5010 - 5000

    // Action: Fully cancel the best ask (1001 at $50.10)
    auto ev4 = create_cancel_order(1001, 0) ;
    engine.process_ouch_request(ev4, feed, seq_num);
    process_feed_to_parser();

    // Expectation:
    // Level $50.10 is erased! Best Ask MUST automatically walk up to $50.20.
    // New spread = 5020 - 5000 = 20.
    EXPECT_EQ(state.best_ask, 5020);
    EXPECT_EQ(state.spread, 20);
    EXPECT_EQ(state.sell_shares, 50);

    // Action 2: Fully cancel the remaining Ask ($50.20)
    auto ev5 = create_cancel_order(1002, 0) ;
    engine.process_ouch_request(ev5, feed, seq_num);
    process_feed_to_parser();

    // Expectation:
    // The ASK side is now completely empty!
    // Best Ask MUST reset to 0. Spread MUST reset to 0 (thanks to the fixed else block!).
    EXPECT_EQ(state.best_ask, 0);
    EXPECT_EQ(state.spread, 0);
    EXPECT_EQ(state.mid_price, 5000); // Reverts to best_bid when ask is 0
}
