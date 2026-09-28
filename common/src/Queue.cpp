#include "common/Queue.h"

void QueueSystem::push_or_update(int u, int v, float err, const glm::vec4& vp) {
    uint64_t edge = make_edge_key(u, v);

    int new_version = ++edge_versions[edge];

    pq.push(queueData{u, v, err, vp, new_version});
}

bool QueueSystem::pop(queueData& out_data) {
    while (!pq.empty()) {
        queueData top = pq.top();
        pq.pop();

        uint64_t edge = make_edge_key(top.u, top.v);
        auto it = edge_versions.find(edge);

        if (it != edge_versions.end() && it->second == top.version) {
            edge_versions.erase(it);
            out_data = top;
            return true;
        }
    }
    return false;
}

void QueueSystem::erase_edge(int u, int v) {
    uint64_t edge = make_edge_key(u, v);
    edge_versions.erase(edge);
}

bool QueueSystem::empty() {
    while (!pq.empty()) {
        queueData top = pq.top();
        uint64_t edge = make_edge_key(top.u, top.v);
        auto it = edge_versions.find(edge);

        if (it != edge_versions.end() && it->second == top.version) {
            return false;
        }
        pq.pop();
    }
    return true;
}

int QueueSystem::size() const {
    return static_cast<int>(edge_versions.size());
}