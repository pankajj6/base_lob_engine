#pragma once

#include "base_lob_engine.h"
#include "events.h"
#include <cstdint>
// #define NDEBUG 
#include <cassert>

template <EngineMode Mode>
inline uint32_t find_order_index(Engine<Mode>& engine, uint64_t order_id){
  auto it = engine.orders_by_id.find(order_id) ;
  
  assert(it != engine.orders_by_id.end() ) ;
  // in NDEBUG mode this can be UB
  return it->second ;
}

template <EngineMode Mode>
inline void reconstruct_market_state(Engine<Mode>& engine ,  Event& event )
{
    
    if (event.event_type != EventType::ITCH){
        return; // we only update using itch.
    }

    auto& lob = engine.books[event.stock_locate] ;
    auto& state = lob.state ;

    // update clocks
    lob.clock = event.timestamp ;
    state.clock = lob.clock ;

    // store
    state.stock_locate = event.stock_locate ;

    // uncomment when engine is updated to have this fields.

    state.old_best_ask = state.best_ask;
    state.old_best_bid = state.best_bid;


    if (event.msg_type == MsgType::OrderAdd)
    {
        auto& add = event.p.itch_add ;
        // update shares
        if (add.side == 'B') state.buy_shares += add.shares ;
        else state.sell_shares += add.shares ;

        // update lob
        engine.itch_add_order(event.stock_locate, add.order_id , add.price, add.shares , add.side) ;
    
    }

    else if (event.msg_type == MsgType::OrderCancel)
    {
        auto& canc = event.p.itch_cancel ;

        // order
        auto idx = find_order_index(engine , canc.order_id) ;
        auto& ord = engine.pool[idx] ;
        auto side = lob.bid_map.contains(ord.price) ? 'B' : 'S' ;

        if(side == 'B') state.buy_shares -= canc.cancelled_shares;
        else state.sell_shares -= canc.cancelled_shares;


        // full delete
        if (canc.cancelled_shares == ord.shares){
            engine.itch_delete_order(event.stock_locate, canc.order_id) ;
        }
        else {
            engine.itch_reduce_order(event.stock_locate, canc.order_id, canc.cancelled_shares) ;
        }
            
    }

    else if (event.msg_type == MsgType::OrderReplace)
    {
        auto& rep = event.p.itch_replace ;

        // order
        auto idx = find_order_index(engine, rep.old_id) ;
        auto& ord = engine.pool[idx] ;
        auto side = lob.bid_map.contains(ord.price) ? 'B' : 'S' ;

        // can be negative
        int32_t net_shares = rep.shares - ord.shares ;

        if (side == 'B') state.buy_shares += net_shares  ;
        else state.sell_shares += net_shares; 
            

        engine.itch_replace_order(event.stock_locate , rep.old_id, rep.new_id, rep.price, rep.shares) ;
    }
    
    // Update Volume parameters.
    else if (event.msg_type == MsgType::OrderExec)
    {
        auto& exec = event.p.itch_execute ;

        // order
        auto idx = find_order_index(engine, exec.order_id) ;
        auto& ord = engine.pool[idx] ;
        auto side = lob.bid_map.contains(ord.price) ? 'B' : 'S' ;

        state.last_trade_price = ord.price ;
        state.last_trade_size = exec.executed_shares;
        
        // incr total volume.
        state.total_volume += exec.executed_shares ;

        if(side == 'B') state.bid_executed_shares += exec.executed_shares;
        else state.ask_executed_shares += exec.executed_shares;

        state.order_imbalance = static_cast<double>(state.bid_executed_shares - state.ask_executed_shares) / state.total_volume ;

        if(side == 'B') state.buy_shares -= exec.executed_shares;
        else state.sell_shares -= exec.executed_shares;

      
        engine.itch_execute_order(event.stock_locate, exec.order_id, exec.executed_shares) ;

    }

    
    // now update : 

    // update best prices :
    if (!lob.bid_map.empty()){
      auto it = lob.bid_map.begin() ;
      state.best_bid = it->first ;
    }
    else {
      state.best_bid = 0 ;
    }
    
    if (!lob.ask_map.empty()){
        auto it = lob.ask_map.begin() ; 
        state.best_ask = it->first ;
    }
    else {
      state.best_ask = 0 ;
    }


    // update mid price :
    if (state.best_ask == 0 && state.best_bid > 0) {
        state.mid_price = state.best_bid ; 
    } 
    else if (state.best_bid == 0 && state.best_ask > 0) {
        state.mid_price = state.best_ask ;
    } 
    else if (state.best_bid > 0 && state.best_ask > 0) {

        uint64_t midpoint = (state.best_ask + state.best_bid)/2;

        auto remdr = ( midpoint % lob.TICK_SIZE ) ;
        midpoint -= remdr;
        state.mid_price = midpoint;
    } 
    else {
        state.mid_price = 0;
    }

    // update spread :
    if (state.best_bid > 0 && state.best_ask > 0 && (state.best_ask > state.best_bid))// state.mid_price = midpoint; // 
    {    
        state.spread = (state.best_ask - state.best_bid); // in PRICE_SCALE
    }
    else { 
        state.spread = 0; 
    }

}
