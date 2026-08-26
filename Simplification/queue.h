//
// Created by juaquin-remon on 8/25/26.
//

#ifndef LEARN_OPENGL_QUEUE_H
#define LEARN_OPENGL_QUEUE_H
#include <map>
#include <utility>
#include <vector>
#include "linalg.h"
#include <iostream>

struct queueData {
    int u, v;
    float err;
    linalg::aliases::float4 vp;
};


std::pair<int, int> make_edge(int u, int v) {
    return {std::min(u, v), std::max(u, v)};
}

struct QueueSystem {
    using Edge = std::pair<int, int>;
    using PriorityKey = std::pair<float, int>;

    std::map<PriorityKey, queueData> priority_queue;
    std::map<Edge, PriorityKey> edge_to_key;
    int current_id = 0;

    void push_or_update(int u, int v, float err, const linalg::aliases::float4& vp) {
        Edge edge = make_edge(u, v);

        if (auto it = edge_to_key.find(edge); it != edge_to_key.end()) {
            priority_queue.erase(it->second);
        }

        PriorityKey new_key = {err, current_id++};
        priority_queue[new_key] = queueData{u, v, err, vp};
        edge_to_key[edge] = new_key;
    }

    bool pop(queueData& out_data) {
        if (priority_queue.empty()) return false;

        auto top_it = priority_queue.begin();
        out_data = top_it->second;

        Edge edge = make_edge(out_data.u, out_data.v);
        edge_to_key.erase(edge);
        priority_queue.erase(top_it);

        return true;
    }

    void erase_edge(int u, int v) {
        Edge edge = make_edge(u, v);
        if (auto it = edge_to_key.find(edge); it != edge_to_key.end()) {
            priority_queue.erase(it->second);
            edge_to_key.erase(it);
        }
    }

    bool empty() const {
        return priority_queue.empty();
    }
};
#endif //LEARN_OPENGL_QUEUE_H