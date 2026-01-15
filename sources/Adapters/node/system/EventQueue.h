#ifndef _NODEEVENTQUEUE_H_
#define _NODEEVENTQUEUE_H_

#include "Externals/etl/include/etl/deque.h"
#include "Foundation/T_Singleton.h"

enum NodeEventType { PICO_REDRAW, PICO_CLOCK, LAST };

class NodeEvent {
public:
  NodeEvent(NodeEventType type) : type_(type) {}
  NodeEventType type_;
};

inline bool operator==(const NodeEvent &lhs,
                       const NodeEvent &rhs) {
  return lhs.type_ == rhs.type_;
};

class NodeEventQueue : public T_Singleton<NodeEventQueue> {
public:
  NodeEventQueue();
  void push(NodeEvent event);
  void pop_into(NodeEvent &event);
  bool empty();

private:
  etl::deque<NodeEvent, NodeEventType::LAST> queue_;
};

#endif
