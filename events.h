#pragma once
// ============================================================
// Events.h
// All event types, payload variants, and enums.
//
// Three event categories:
//   OUCH          : agent request to exchange (L2)
//                   Kernel MODE 1: pop OUCH, call matching engine
//   ITCH          : broadcast exchange to ALL agents (L1)
//                   Kernel MODE 2: pop ITCH, iterate all agents
//   SPECIFIC_OUCH : private exchange to ONE agent (L3)
//                   Kernel MODE 3: update one agent, let them react
// ============================================================

#include <cstdint>

enum class EventType : uint8_t {
    OUCH  = 0,   // MODE 1: agent request to exchange
    ITCH  = 1,   // MODE 2: broadcast to all agents
    S_OUCH = 2,  // MODE 3: private exchange-to-one-agent
    AgentWakeUP = 3
} ;

enum class AgentTier : uint8_t {
    //HFT = 0,
    //Fundamentalist = 1,
    //INSTITUTIONAL = 2,
    //RETAIL = 3,
    EXCHANGE = 4,
    ZI = 5
} ;

enum class Symbol : uint16_t {
    AAPL = 0, // stock locate
    MSFT = 1,
    TSLA = 2,
    SPY  = 3,
    NIL  = 4,
} ;

enum class Reason : uint8_t {
    order_id_not_found = 0,
    invalid_order = 1, // price or shares field is invalid. or duplicate_order_id 
    invalid_request = 2 , // for cancel shares = 0  
    book_empty = 3,
    lob_full = 4,
} ;


// OUCH
struct EnterOrder {
    uint64_t order_id ;
    uint32_t price ;
    uint32_t shares ;
    char side ;       // 'B' , 'S'
    uint8_t time_in_force ; // 0=DAY, 1=IOC, 2=GTC
} ;

struct CancelReq {
    uint64_t order_id ;
    uint32_t max_shares ;  // 0=full cancel, >0=reduce to this size
} ;

struct ReplaceReq {
    uint64_t old_id ;
    uint64_t new_id ;
    uint32_t price  ; // new ord
    uint32_t shares ;
} ;


// ITCH 
struct OrderAdd {
    uint64_t order_id ;
    uint32_t price    ;
    uint32_t shares   ;
    char side ;
} ;

struct OrderExecuted {
    uint64_t order_id ;
    uint32_t executed_shares ;
        // uint64_t match_number ; // review 
} ;

struct OrderCancel {
    uint64_t order_id ;
    uint32_t cancelled_shares ;
} ;

struct OrderReplace {
    uint64_t old_id ;
    uint64_t new_id ;
    uint32_t price  ;
    uint32_t shares ;
} ;


// S_OUCH
// Limit order now rests in book (passive or aggressive-then-passive).
struct OrderRestingNotify {
    uint64_t order_id ;
    uint32_t price ;
    uint32_t resting_shares ;
    char side ;
} ;

// request rejected privately. No public ITCH.
struct OrderRejected {
    uint64_t order_id ;
    Reason reason ;
} ;

struct FillNotification {
    uint64_t agg_order_id ; // aggreesive
    uint64_t pass_order_id ; // passive
    uint32_t fill_shares ;
    uint32_t price ;
    char side ;
                     // uint32_t remaining_shares ;  // 0=fully filled
} ;

// Cancel request succeeded.

struct CancelSuccess {
    uint64_t order_id ;
    uint32_t remaining_shares ;  // 0=fully cancelled
} ;

struct CancelRejected {
    uint64_t order_id ;
    Reason reason ;  
} ;


// Sent to agent: old order cancelled, new order resting.
// Paired with public OrderReplaced ITCH pushed after this.
struct ReplaceSuccess {
    uint64_t new_id ;
    uint32_t price ;
    uint32_t cancelled_shares ;  // remaining of old cancelled
    uint32_t shares ;    // new resting
    char side ;
} ;


struct ReplaceRejected {
    uint64_t order_id ;
    Reason reason ;
} ;


struct AgentWakePayload{
    uint64_t last_wakeup = 0 ; // 0 helpful in start of sim
    AgentTier tier = AgentTier::ZI ;
    uint32_t index = 0 ;
} ;


enum class MsgType: uint8_t{
  
  // ouch (inbound)
  EnterOrder , CancelReq , ReplaceReq ,
  
  // itch 
  OrderAdd , OrderExec , OrderCancel, OrderReplace ,
  
  // ouch (outbound) -- specific ouch
  OrderResting , OrderRej , Fill , CancelSucss , CancelRej , ReplaceSucss , ReplaceRej ,
  
  // random zi wake up
  WakeUp

} ;

struct Event {
    uint64_t timestamp = 0 ;
    uint64_t sequence_num = 0 ;
    uint64_t causal_parent_id = 0 ;
    EventType event_type ;
    MsgType msg_type ;

    uint16_t stock_locate ;
    
    union payload {
      // ouch (inbound)
      EnterOrder order_req ;
      CancelReq cancel_req ; 
      ReplaceReq replace_req ;

      // itch
      OrderAdd itch_add ;
      OrderExecuted itch_execute ;
      OrderCancel itch_cancel ;
      OrderReplace itch_replace ;
      
      // ouch (outbound)
      OrderRestingNotify order_resting ;
      OrderRejected order_rej ;
      FillNotification fill ; 
      CancelSuccess cancal_sucss ;
      CancelRejected cancel_rej ;
      ReplaceSuccess replace_sucss ;
      ReplaceRejected replace_rej ;
      
      // agent zi wake up
      AgentWakePayload wake_up ;
      
      // default
      payload() {} 
      
      //  ADD THIS TEMPLATE CONSTRUCTOR FOR BRACES TO WORK:
      template<typename T>
      payload(const T& msg) {
          // This safely uses placement new to copy any packet type into the shared memory
          ::new (this) T(msg); 
      }
      
    } p ;
} ;

// kernel will maintain a order_id to {agent tier , agent index} map and deliver him specific ouch

