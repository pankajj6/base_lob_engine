#pragma once
// base lob engine for all . ( simulation + parser )


// flat map for price levels . (version 1)
// price level {head , tail } // no need to store price.
// storage pool , orders_by_id , is in engine itself . 
// LOB provides the bid ask map.

// lob.bid_map[price].head or tail , total volume , order_count.


// sudo apt install libboost-dev

#include <boost/unordered/unordered_flat_map.hpp>
#include <flat_map>
#include <cstdint>
#include <memory>
#include <vector>
//#include <stack>
#include "events.h"
#include <deque>

constexpr size_t ORDER_POOL_SIZE = 10000000 ; 
// 10 million
constexpr size_t ORDER_ID_MAP_SIZE = 10000000 ;

constexpr size_t MAX_LEVELS = 2000 ;

constexpr size_t MAX_TICKERS = 10000 ; // covers full nasdaq .

constexpr int32_t INVALID_INDEX = -1; // or should be use 0 ?

constexpr uint32_t MAX_PRICE = 1999999900 ; // in case of Bk. hathway . use two decimal place interpretation. so agents will send this price when they mean $19,999,999.00

// processing times : (engine )
constexpr uint64_t PT_BASE = 5000;
constexpr uint64_t PT_ORDER_FILL = 10;
constexpr uint64_t PT_LEVEL_WALK = 100;
constexpr uint64_t PT_ADD_ORDER = 250;
constexpr uint64_t PT_CANCEL = 100;


// price level
struct level{ // 16 bytes
  // link list index
  int32_t head = INVALID_INDEX ;  
  int32_t tail = INVALID_INDEX;
  uint32_t order_count = 0 ; 
  uint32_t total_volume = 0 ; // for CME/ICE option feeds, only update this. 
};


//template <int x>
//class test{

//public:

//  void move(void* ptr){
  
//    if constexpr(x == 1){
//      *ptr  = *ptr + 18 ; // vlan ethernet 
//    }
//    else {
//      *ptr = *ptr + 14 ; // standard ethernet.
//    }
//  }

//};

struct Order{
  
  int32_t next = INVALID_INDEX ;
  int32_t prev = INVALID_INDEX; 
  uint32_t price = 0;
  uint32_t shares = 0;
  uint64_t order_id ;
};


enum class OrderType : uint8_t{
  Limit , Market
} ;


enum class EngineMode : uint8_t{
  Simulation = 0  ,
  Parser = 1
};


// 2 * [2000 * (4 price + 16 level ) ] = 2 * 40,000 bytes = 80kb per lob. + 

class LOB {  

private: 
  
public:

  uint64_t clock = 0 ; // use in sim mode .
  // tick size : for stock with precison (2) eg.berkshire hathaway , update this field TICK_SIZE = 1 ; // 0.01 * 100
  uint64_t TICK_SIZE = 100 ; // 0.01 * 10000 . precision (4).
  char stock[8] ; // 8 byte field.
  
  // key - price | value - level
  std::flat_map<uint32_t , level, std::greater<uint32_t>> bid_map ; 
  std::flat_map<uint32_t , level, std::less<uint32_t>> ask_map ;
  
  
  struct LobState {
      uint16_t stock_locate = 0 ; 
      uint64_t clock = 0 ; 
      uint32_t best_bid = 0 ;
      uint32_t best_ask = 0 ;
      uint32_t mid_price = 0 ;
      uint32_t old_best_bid = 0 ;
      uint32_t old_best_ask = 0 ;
      uint32_t spread = 0 ;
      uint32_t last_trade_price = 0 ;
      uint64_t bid_executed_shares = 0 ;
      uint64_t ask_executed_shares = 0 ;
      uint64_t total_volume = 0 ; // total shares traded
      uint32_t last_trade_size = 0 ; 
      
      double order_imbalance = 0; // (BidVol - AskVol) / (TotalVol)
      
      // Book liquidity
      uint64_t buy_shares = 0;  
      uint64_t sell_shares = 0;

      int volatility ; 
          
  } state ;

                                                                          
  LOB(){
  
    std::vector<uint32_t> bid_keys;
    std::vector<level> bid_values;
    
    bid_keys.reserve(MAX_LEVELS);
    bid_values.reserve(MAX_LEVELS);

    std::vector<uint32_t> ask_keys;
    std::vector<level> ask_values;
    
    ask_keys.reserve(MAX_LEVELS);
    ask_values.reserve(MAX_LEVELS);

    bid_map.replace( std::move(bid_keys) , std::move(bid_values) );
    ask_map.replace( std::move(ask_keys), std::move(ask_values) );
   
  }
  
} ;

// for parser mode :

// pool = 10 mil * 24 bytes = 240 mb
// orders_by_id =  10 mil * 12 bytes = 120 mb at most. look how boost stores......
// free indexs = 10 mil * 4 bytes = 40 mb .
// books = 10000 * 80 kb = 800 mb. 
// total engine = 1.2 gb.
// kernel lob engine = same. 
// total memory 2.4 gb for lob s.

template <EngineMode mode>
class Engine{

public:

  std::unique_ptr<Order[]> pool ; // storage pool . 
  
  boost::unordered_flat_map<uint64_t, uint32_t> orders_by_id ; // order id : pool index 
  
  std::vector<uint32_t> free_indexs ; // hold pool indexes
  
  std::unique_ptr<LOB[]> books ; // 10000 * 80kb = 800 mb.
  
  // store the locate : stock code . and write a function maybe 
  

  Engine() 
    :   pool(std::make_unique<Order[]>(ORDER_POOL_SIZE)),
        books(std::make_unique<LOB[]>(MAX_TICKERS)) 
  {
    
    orders_by_id.reserve(ORDER_ID_MAP_SIZE);
    free_indexs.reserve(ORDER_POOL_SIZE);

    // push avaiable indexes
    for (int i= ORDER_POOL_SIZE-1 ; i >= 0 ; i--){
      free_indexs.push_back(i) ;
    }
    
    // warm up loop 
    for (int i =0; i < ORDER_POOL_SIZE ; i+=256){
    
      volatile int warm = pool[i].next ;
      (void)warm ;
    }
    
  }

  void itch_add_order(uint16_t stock_locate , uint64_t order_id , uint32_t price, uint32_t shares , char side ){
    
    // prevent 0 share order entry (possible simulation mode)
    if (shares == 0) return ;
    
    // free index
    auto idx = free_indexs.back() ; 
    free_indexs.pop_back() ;
    
  
    // id : index
    orders_by_id[order_id] = idx ;
    
    // write order
    pool[idx].order_id = order_id ; 
    pool[idx].price = price ;
    pool[idx].shares = shares ;
    
    
    LOB& book = books[stock_locate] ;
    
    if (side == 'B'){
      helper_link_list_chaining<std::flat_map<uint32_t, level, std::greater<uint32_t>>>(book.bid_map, idx) ;
    }
    else{
      helper_link_list_chaining<std::flat_map<uint32_t, level, std::less<uint32_t>>>(book.ask_map, idx) ;
    }
    
    return ;
  } 
  
  void itch_reduce_order(uint16_t stock_locate , uint64_t order_id , uint32_t cancel_shares){

    auto it = orders_by_id.find(order_id) ;
    if (it == orders_by_id.end() ) return ; // safety
    
    // index 
    auto idx = it->second ; 
    
    // reduce shares
    pool[idx].shares -= cancel_shares ;
    
    auto price = pool[idx].price ;
    
    // reduce volume from correct map.
    if (books[stock_locate].bid_map.contains( pool[idx].price) ){
      
      auto& side_map = books[stock_locate].bid_map ;
      side_map[price].total_volume -= cancel_shares ;
    }
    else{
      auto& side_map = books[stock_locate].ask_map ;
      side_map[price].total_volume -= cancel_shares ;
    }
    
    return ;
  }

  
  void itch_delete_order(uint16_t stock_locate , uint64_t order_id ){

    auto it = orders_by_id.find(order_id) ;
    if (it == orders_by_id.end() ) return ; // safety
    
    auto idx = it->second ; 
    
    // remove entry
    orders_by_id.erase(it); 
    // push free index
    free_indexs.push_back(idx) ;
    
    Order& order = pool[idx] ;
    LOB& lob = books[stock_locate];
    
    // remove order from link list and update correct map of price level
    if (lob.bid_map.contains(order.price)){
      helper_link_list_removal<std::flat_map<uint32_t, level, std::greater<uint32_t>>>(lob.bid_map, idx);
    }
    else{
      helper_link_list_removal<std::flat_map<uint32_t, level, std::less<uint32_t>>>(lob.ask_map, idx);
    }
    
    // overwrite with default values
    pool[idx] = Order{} ; // reset values
    
    return ;
 }
 
 void itch_execute_order(uint16_t stock_locate , uint64_t order_id , uint32_t executed_shares){
 
    auto it = orders_by_id.find(order_id) ;
    auto idx = it->second ; 
    
    if (pool[idx].shares == executed_shares){
      itch_delete_order(stock_locate , order_id) ;
    }
    else{
      itch_reduce_order(stock_locate , order_id , executed_shares) ;
    }
  
    return ;
 
 }
 
 void itch_replace_order(uint16_t stock_locate , uint64_t old_id , uint64_t new_id , uint32_t price , uint32_t shares ){
 
    auto it = orders_by_id.find(old_id) ;
    
    auto idx = it->second ;
    
    char side = books[stock_locate].bid_map.contains( pool[idx].price) ? 'B' : 'S' ; 

    itch_delete_order(stock_locate, old_id) ;
    itch_add_order(stock_locate , new_id , price , shares , side) ;
 
    return ;
 }
 
 
 // ------------------------------Simulation Mode functions----------------------------------// 
 
 // ouch : for simulation mode
 
 void process_ouch_request(Event& event , std::deque<Event>& feed , uint64_t& seq_num){
  
  auto& lob = books[event.stock_locate] ;
  
  // jump to event timestamp
  if (event.timestamp > lob.clock ){
    lob.clock = event.timestamp ;
  }
  // else -> interpretation :  this pkt was waiting in exchange buffer while lob was busy.
  
  // base increment in clock for any processing
  lob.clock += PT_BASE ;
  
  
  
  switch (event.msg_type){
  
    case MsgType::EnterOrder: {
      
      // pkt
      auto& pkt = event.p.order_req ;
      
      // check invalid price/shares or duplicate order id.
      if (pkt.price%lob.TICK_SIZE != 0 || pkt.shares == 0 || orders_by_id.contains(pkt.order_id)){
        // push ouch
        OrderRejected ouch = {pkt.order_id , Reason::invalid_order } ;
        feed.emplace_back( Event{ lob.clock, seq_num++ , event.sequence_num , EventType::S_OUCH, MsgType::OrderRej , event.stock_locate , {ouch} } ); 
      }
      else {
        // process
        ouch_process_order(event, feed, seq_num);
      }
      
      break ;
    }
    
    case MsgType::CancelReq: {
    
      // pkt
      auto& pkt = event.p.cancel_req ;
      
      // order id check
      if (!orders_by_id.contains( pkt.order_id) ){
        // push ouch
        CancelRejected ouch = {pkt.order_id , Reason::order_id_not_found} ;
        feed.emplace_back( Event{lob.clock, seq_num++ , event.sequence_num , EventType::S_OUCH, MsgType::CancelRej , event.stock_locate , {ouch} } ) ;
      }
      else{ 
        // process
        ouch_cancel(event , feed, seq_num);
      }
      
      break ;
    }
  
    case MsgType::ReplaceReq: {
    
      // pkt
      auto& pkt = event.p.replace_req ;
      
      // order id check
      if (!orders_by_id.contains( pkt.old_id) || pkt.price%lob.TICK_SIZE != 0 || pkt.shares == 0 ){
        
        // replace ouch
        ReplaceRejected ouch = {pkt.old_id , Reason::order_id_not_found} ;
        
        // check if reason invalid order 
        if (pkt.price%lob.TICK_SIZE != 0 || pkt.shares == 0){
          ouch = { pkt.new_id , Reason::invalid_order } ; 
        }
        
        // push
        feed.emplace_back( Event{lob.clock, seq_num++ , event.sequence_num , EventType::S_OUCH, MsgType::ReplaceRej , event.stock_locate , {ouch} } );
      } 
      else {
        // process
        ouch_replace(event , feed, seq_num) ;
      }
      
      break ;
      
    }
  
  }
  // all events already pushed.
  return ;
 }
 
 void ouch_replace(Event& event , std::deque<Event>& feed, uint64_t& seq_num){
  
  // pkt
  auto& pkt = event.p.replace_req ; // payload
  
  // check if no shares to replace 
  if (pkt.shares == 0) return  ;
  
  // lob 
  auto& lob = books[event.stock_locate] ;
  
  // calculate old remaining shares
  auto idx = orders_by_id[pkt.old_id];
  auto old_cancel_shares = pool[idx].shares ;
  
  // replace order ( utilising same function used for parser)
  itch_replace_order(event.stock_locate , pkt.old_id , pkt.new_id , pkt.price , pkt.shares);
  // increment clock ( cancel old + add new )
  lob.clock += PT_CANCEL ;
  lob.clock += PT_ADD_ORDER ;
  
  // push replace itch 
  OrderReplace itch = {pkt.old_id,  pkt.new_id , pkt.price , pkt.shares} ;
  feed.emplace_back( Event{ lob.clock, seq_num++ , event.sequence_num , EventType::ITCH, MsgType::OrderReplace , event.stock_locate , { itch } } ) ;
  
  // resting order side  
  char side = lob.bid_map.contains(pkt.price) ? 'B' : 'S' ; 
  // push replace success ouch 
  ReplaceSuccess ouch = {pkt.new_id , pkt.price , old_cancel_shares , pkt.shares , side} ;
  feed.emplace_back( Event{ lob.clock, seq_num++ , event.sequence_num , EventType::S_OUCH, MsgType::ReplaceSucss , event.stock_locate , {ouch} } );
  
  return ;
}
 
 void ouch_cancel(Event& event , std::deque<Event>& feed, uint64_t& seq_num){
  
  // pkt
  auto& pkt = event.p.cancel_req ;
  
  // lob
  auto& lob = books[event.stock_locate] ;
  
  // calculate shares to cancel
  auto idx = orders_by_id[pkt.order_id] ;
  auto cancel_shares = pool[idx].shares - pkt.max_shares ;
  
  // check if no shares to cancel
  if (cancel_shares  == 0){
    // push
    CancelRejected ouch = {pkt.order_id , Reason::invalid_request} ;
    feed.emplace_back( Event{lob.clock, seq_num++ , event.sequence_num , EventType::S_OUCH, MsgType::CancelRej , event.stock_locate , {ouch} } ) ;
    return ;
  }; 
  
  // remaining
  auto& remaining_shares = pkt.max_shares ;
  
  // full delete
  if (remaining_shares == 0) itch_delete_order(event.stock_locate , pkt.order_id) ;

  // partial reduce
  else  itch_reduce_order(event.stock_locate , pkt.order_id , cancel_shares  ) ;
  
  // increment clock
  lob.clock += PT_CANCEL ;
  
  // push cancel itch
  OrderCancel itch = {pkt.order_id , cancel_shares} ;
  feed.emplace_back( Event{ lob.clock, seq_num++ , event.sequence_num , EventType::ITCH, MsgType::OrderCancel , event.stock_locate , {itch} } ) ;
  
  // push cancel success ouch
  CancelSuccess ouch = {pkt.order_id , remaining_shares} ;
  feed.emplace_back( Event{ lob.clock, seq_num++ , event.sequence_num , EventType::S_OUCH, MsgType::CancelSucss , event.stock_locate , {ouch} } );
  
  return ;
}
 
 void ouch_process_order(Event& event , std::deque<Event>& feed , uint64_t& seq_num){
  // pkt
  auto& pkt = event.p.order_req ;
  
  // correct map
  if (pkt.side == 'B'){
    auto& opposite_map = books[event.stock_locate].ask_map ; // opposite for matching
    add_or_match_order<std::flat_map<uint32_t , level , std::less<uint32_t>>>(opposite_map , feed , event , seq_num, event.stock_locate , pkt.order_id , pkt.price , pkt.shares , pkt.side) ;
  } 
  else{
    auto& opposite_map = books[event.stock_locate].bid_map ; // opposite for matching
    add_or_match_order<std::flat_map<uint32_t , level , std::greater<uint32_t>>>(opposite_map , feed , event , seq_num, event.stock_locate , pkt.order_id , pkt.price , pkt.shares , pkt.side) ;
  }
  return ;
}
 
 
 
 
 
 template <typename mapType>
 auto get_best_level_iterator(mapType& lob_map){
    return lob_map.begin() ;
 } 
 
 bool is_passive(uint32_t order_price , uint32_t opp_best_price , char order_side){
    
    if (order_price > MAX_PRICE) return false ; // market order
    if (order_side == 'B' && (order_price < opp_best_price) ) return true ;
    if (order_side == 'S' && (order_price > opp_best_price) ) return true ;
    
    return false ;
 }

 template <typename mapType>
 void add_or_match_order(mapType& opposite_map , std::deque<Event>& feed , Event& event , uint64_t& seq_num, uint16_t stock_locate , uint64_t order_id , uint32_t price , uint32_t shares , char side ){
 
  // lob ref for clock increment
  auto& lob = books[stock_locate] ;
 
  // start matching loop : ( for aggresive orders)
  while(shares > 0 && !opposite_map.empty() ){
  
    // get best price & level 
    auto it = get_best_level_iterator(opposite_map) ;
    auto best_price = it->first ;
    auto& lvl = it->second ;
    
    // check if non aggresive/marketable limit order :
    if (is_passive( price , best_price , side )) break ; 
    
    // to break second loop
    bool level_exhausted = false ; 
     
    while(shares > 0 && level_exhausted != true){
      
      auto idx = lvl.head ;
      auto& order = pool[idx] ;
      
      
      auto executed_shares = std::min(shares , order.shares) ;
      
      // generate execution 
      OrderExecuted itch = {order.order_id , executed_shares} ;   
      // fill
      FillNotification ouch = {order_id , order.order_id , executed_shares , order.price , side } ;
      
      // increment clock:
      lob.clock += PT_ORDER_FILL ;
      
      if (shares < order.shares ){
        // reduce order logic :
        order.shares -= shares ;
        lvl.total_volume -= shares ;
        
        // no shares left to fill (incoming order)
        shares = 0 ;

      }
      else {
        shares -= order.shares ; // decrement
        
        if (lvl.order_count == 1){
          level_exhausted = true ; // going to exhaust (below)
        }
        
        // delete head
        itch_delete_order(stock_locate , order.order_id); // utilising same function used for itch parser
        
      }
      
      // push execution
      feed.emplace_back( Event{ lob.clock, seq_num++ , event.sequence_num , EventType::ITCH, MsgType::OrderExec , stock_locate , {itch} } ); 
      
      // push fill notification 
      feed.emplace_back( Event{ lob.clock, seq_num++ , event.sequence_num , EventType::S_OUCH, MsgType::Fill , stock_locate , {ouch} } ); 
      
      
      // pre-increment clock with level walk time , if it happens in next iteration
      if (level_exhausted && shares > 0 && !opposite_map.empty()) {
        if (!is_passive(price, get_best_level_iterator(opposite_map)->first, side)) {
          lob.clock += PT_LEVEL_WALK; // next 
        }
      }
      
    }
   // inner while loop ends    
 }  

 auto ord_type = OrderType::Limit ;
 // check if market order
 if (price > MAX_PRICE){
  ord_type = OrderType::Market ;
 }
 
 // remaining limit order ( or passive limit order )
 if (shares > 0 && ord_type == OrderType::Limit){
  // add order to the book
  itch_add_order(stock_locate , order_id , price, shares , side); //  utilising same function used for itch parser
  
  // increment clock
  lob.clock += PT_ADD_ORDER ;
  
  // push Order Add itch into feed
  OrderAdd itch = {order_id , price , shares , side} ;
  feed.emplace_back( Event{ lob.clock, seq_num++ , event.sequence_num , EventType::ITCH, MsgType::OrderAdd , stock_locate , {itch} } );
  
  // push Resting notifi to agent
  OrderRestingNotify ouch = {order_id, price , shares , side } ;
  feed.emplace_back( Event{ lob.clock, seq_num++ , event.sequence_num , EventType::S_OUCH, MsgType::OrderResting , stock_locate , {ouch} } );
 }
 
 // remaining market order ( book exhausted)
 if (shares > 0 && ord_type == OrderType::Market){
  // remaining market order rejected.
  OrderRejected ouch = {order_id , Reason::book_empty } ;
  feed.emplace_back( Event{ lob.clock, seq_num++ , event.sequence_num , EventType::S_OUCH, MsgType::OrderRej , stock_locate , {ouch} } );
 }
 
 return ;
}
 
 

private :
  
  // helper functions 
  
  template <typename mapType>
  void helper_link_list_chaining(mapType& side_map , uint32_t idx){
  
    Order& order = pool[idx] ;
    
    level& lvl = side_map[order.price] ; // Get or create level
    
    if (lvl.order_count == 0){ // new level
      lvl.head = idx ;
      lvl.tail = idx ;
    } 
    else{  // level already exist

      pool[lvl.tail].next = idx ; // update old tail order's next 
      order.prev = lvl.tail ;
      lvl.tail = idx ; 
    }
    
    lvl.order_count++ ;
    lvl.total_volume += order.shares ;
    
    return ;
  }
  
  template <typename mapType>
  void helper_link_list_removal(mapType& side_map , uint32_t idx){

    Order& ord = pool[idx] ;
  
    level& lvl = side_map[ord.price] ; 
    
    // linklist removal 
    if (lvl.head == idx) lvl.head = ord.next ;
    if (lvl.tail == idx) lvl.tail = ord.prev ;
    if (ord.prev != INVALID_INDEX) pool[ord.prev].next = ord.next ; 
    if (ord.next != INVALID_INDEX) pool[ord.next].prev = ord.prev ;
    
    // decrement count and shares from level
    lvl.order_count-- ;
    lvl.total_volume -= ord.shares ;
    
    // last order
    if (lvl.order_count == 0 ){ 
      side_map.erase(ord.price) ; // remove level 
    } 

    return ;
  } 


//void 

};

