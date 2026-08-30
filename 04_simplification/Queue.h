#ifndef QUEUE_H
#define QUEUE_H

#include <map>
#include <utility>
#include "linalg.h"

struct queueData {
    int u, v;
    float err;
    linalg::aliases::float4 vp;
};

std::pair<int, int> make_edge(int u, int v);

struct QueueSystem {
    using Edge = std::pair<int, int>;
    using PriorityKey = std::pair<float, int>;

    std::map<PriorityKey, queueData> priority_queue;
    std::map<Edge, PriorityKey> edge_to_key;
    int current_id = 0;

    void push_or_update(int u, int v, float err, const linalg::aliases::float4& vp);
    bool pop(queueData& out_data);
    void erase_edge(int u, int v);
    bool empty() const;
    int size() const;
};

#endif // QUEUE_H
