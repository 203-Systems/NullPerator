#include "EventQueue.h"

NodeEventQueue::NodeEventQueue(){};
void NodeEventQueue::push(NodeEvent event) {
  if (std::find(queue_.begin(), queue_.end(), event) == queue_.end()) {
    queue_.push_back(event);
  }
};

void NodeEventQueue::pop_into(NodeEvent &event) {
  event.type_ = queue_.front().type_;
  queue_.pop_front();
};

bool NodeEventQueue::empty() { return queue_.empty(); }
