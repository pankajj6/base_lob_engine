#include <gtest/gtest.h>
#include "base_lob_engine.h" 
#include <cstdint>

// We create a Test Fixture class. 
// This gives every test a fresh, reset Engine instance automatically.
class EngineTest : public ::testing::Test {
protected:
    // Instantiate the engine in  mode for testing
    Engine<EngineMode::Parser> engine;
    uint16_t test_stock = 1; // Arbitrary stock locate code

    void SetUp() override {
        // This runs before every single TEST_F. 
        // Engine is already initialized by its constructor.
    }

    void TearDown() override {
        // Runs after every test. Cleanup if necessary.
    }
};

// 1. Test Adding an Order
TEST_F(EngineTest, ItchAdd_REDUCE_EXECUTE_DELETE_linklist_checks) { // 
    uint64_t order_id = 1001;
    uint32_t price = 5000; // $50.00
    uint32_t shares = 100;
    char side = 'B';

    size_t stack_size = engine.free_indexs.size() ;

    engine.itch_add_order(test_stock, order_id, price, shares, side);

    // Verify the order ID is now mapped
    ASSERT_TRUE(engine.orders_by_id.contains(order_id));
    
    // Get the pool index
    uint32_t pool_index = engine.orders_by_id[order_id];
    
    // Verify the pool data matches our input (assuming your Order struct has these fields)
    
    //auto it = engine.books[test_stock].bid_map.begin() ;
    //auto& lvl = it->second ;
    
    level& lvl = engine.books[test_stock].bid_map[5000];

    //EXPECT_EQ(it->first, price);
    EXPECT_EQ(lvl.head , 0) ;
    EXPECT_EQ(lvl.tail , 0) ;
    EXPECT_EQ(lvl.order_count , 1) ;
    EXPECT_EQ(lvl.total_volume , shares) ; 
    
    EXPECT_EQ(engine.pool[pool_index].price, price);
    EXPECT_EQ(engine.pool[pool_index].shares, shares);
    //EXPECT_EQ(engine.pool[pool_index].side, side);

    engine.itch_add_order(test_stock, 1002, 5000, 50, 'B') ;

    ASSERT_TRUE(engine.orders_by_id.contains(1002));

    auto& lvl2 = engine.books[test_stock].bid_map[5000];

    pool_index = engine.orders_by_id[1002];

    EXPECT_EQ(pool_index , 1); // second free index poped from stack
    EXPECT_EQ(engine.free_indexs.size() , stack_size - 2  ); // verfiy current stack size .
    EXPECT_EQ(lvl2.tail , pool_index); // next 
    EXPECT_EQ(lvl2.head , 0) ;// first order 
    EXPECT_EQ(lvl2.total_volume , 150);
    EXPECT_EQ(lvl2.order_count , 2);
    EXPECT_EQ(engine.pool[0].next , pool_index);
    EXPECT_EQ(engine.pool[pool_index].prev , 0);
    
    
    engine.itch_reduce_order(test_stock , 1002 , 20);
    
    ASSERT_TRUE(engine.orders_by_id.contains(1002));

    auto& lvl3 = engine.books[test_stock].bid_map[5000];

    pool_index = engine.orders_by_id[1002];

    EXPECT_EQ(pool_index , 1); // second free index poped from stack
    EXPECT_EQ(engine.free_indexs.size() , stack_size - 2  ); // verfiy current stack size .
    EXPECT_EQ(lvl3.tail , pool_index); // next 
    EXPECT_EQ(lvl3.head , 0) ;// first order 
    EXPECT_EQ(lvl3.total_volume , 130);
    EXPECT_EQ(lvl3.order_count , 2);
    EXPECT_EQ(engine.pool[0].next , pool_index);
    EXPECT_EQ(engine.pool[pool_index].prev , 0);
    
    engine.itch_execute_order(test_stock, 1001 , 50);

    ASSERT_TRUE(engine.orders_by_id.contains(1001));

    auto& lvl4 = engine.books[test_stock].bid_map[5000];

    pool_index = engine.orders_by_id[1001];

    EXPECT_EQ(pool_index , 0); // second free index poped from stack
    EXPECT_EQ(engine.free_indexs.size() , stack_size - 2  ); // verfiy current stack size .
    EXPECT_EQ(lvl4.tail , 1); // next 
    EXPECT_EQ(lvl4.head , 0) ;// first order 
    EXPECT_EQ(lvl4.total_volume , 80);
    EXPECT_EQ(lvl4.order_count , 2);
    EXPECT_EQ(engine.pool[0].prev , -1);
    EXPECT_EQ(engine.pool[1].next , -1) ;
    
    engine.itch_delete_order(test_stock , 1001 ); // remove fully.

    ASSERT_FALSE(engine.orders_by_id.contains(1001));

    auto& lvl5 = engine.books[test_stock].bid_map[5000];
    // pool_index = engine.orders_by_id[1001];

    // EXPECT_EQ( , 0); // second free index poped from stack
    EXPECT_EQ(engine.free_indexs.size() , stack_size - 1  ); // verfiy current stack size .
    EXPECT_EQ(lvl5.tail , 1); // next 
    EXPECT_EQ(lvl5.head , 1) ;// same 
    EXPECT_EQ(lvl5.total_volume , 30);
    EXPECT_EQ(lvl5.order_count , 1);
    EXPECT_EQ(engine.pool[1].prev , -1);
    EXPECT_EQ(engine.pool[1].next , -1) ;
    
    // EXPECT_EQ() ; 
}

// 2. Test Reducing an Order (Partial Cancel)
TEST_F(EngineTest, ItchReduceOrder_DecrementsShares) {
    uint64_t order_id = 1002;
    engine.itch_add_order(test_stock, order_id, 5000, 200, 'B'); // Add 200 shares

    engine.itch_reduce_order(test_stock, order_id, 50); // Cancel 50 shares

    uint32_t pool_index = engine.orders_by_id[order_id];
    
    // The order should still exist, but with 150 shares
    EXPECT_EQ(engine.pool[pool_index].shares, 150);
}

// 3. Test Deleting an Order (Full Cancel)
TEST_F(EngineTest, ItchDeleteOrder_RemovesFromState) {
    uint64_t order_id = 1003;
    engine.itch_add_order(test_stock, order_id, 5000, 100, 'S');

    int32_t x = engine.free_indexs.size() ;

    engine.itch_delete_order(test_stock, order_id);

    int32_t y = engine.free_indexs.size() ;


    // Verify it is completely removed from the hash map
    EXPECT_FALSE(engine.orders_by_id.contains(order_id));
    
    // (Optional) You could also check if the free_indexs stack grew by 1
}

// 4. Test Executing an Order (Partial and Full)
TEST_F(EngineTest, ItchExecuteOrder_PartialAndFullExecutions) {
    uint64_t order_id = 1004;
    engine.itch_add_order(test_stock, order_id, 5000, 100, 'B'); // Add 100 shares

    // Partial execution
    engine.itch_execute_order(test_stock, order_id, 40);
    ASSERT_TRUE(engine.orders_by_id.contains(order_id)); // Should still exist
    EXPECT_EQ(engine.pool[engine.orders_by_id[order_id]].shares, 60);

    // Full execution (remaining 60 shares)
    engine.itch_execute_order(test_stock, order_id, 60);
    
    // Since it was fully executed, the wrapper should have called delete
    EXPECT_FALSE(engine.orders_by_id.contains(order_id)); 
}

// 5. Test Replacing an Order
TEST_F(EngineTest, ItchReplaceOrder_SwapsIdsAndUpdatesData) {
    uint64_t old_id = 1005;
    uint64_t new_id = 1006;
    engine.itch_add_order(test_stock, old_id, 5000, 100, 'B');

    // Replace with new ID, new price (5005), and new shares (200)
    engine.itch_replace_order(test_stock, old_id, new_id, 5005, 200);

    // The old ID should be dead
    EXPECT_FALSE(engine.orders_by_id.contains(old_id));

    // The new ID should be active with the updated details
    ASSERT_TRUE(engine.orders_by_id.contains(new_id));
    uint32_t new_pool_index = engine.orders_by_id[new_id];
    EXPECT_EQ(engine.pool[new_pool_index].price, 5005);
    EXPECT_EQ(engine.pool[new_pool_index].shares, 200);
}
