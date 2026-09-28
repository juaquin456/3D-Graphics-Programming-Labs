#ifndef QUEUE_H
#define QUEUE_H

#include <map>
#include <queue>
#include <unordered_map>
#include <utility>
#include "glm/vec4.hpp"

struct queueData {
    int u, v;
    float err;
    glm::vec4 vp;
    int version = 0;
};

struct QueueSystem {
    struct EdgeHash {
        std::size_t operator()(uint64_t key) const {
            key ^= key >> 30;
            key *= 0xbf58476d1ce4e5b9ULL;
            key ^= key >> 27;
            key *= 0x94d049bb133111ebULL;
            key ^= key >> 31;
            return static_cast<std::size_t>(key);
        }
    };

    static uint64_t make_edge_key(int u, int v) {
        uint32_t min_uv = static_cast<uint32_t>(std::min(u, v));
        uint32_t max_uv = static_cast<uint32_t>(std::max(u, v));
        return (static_cast<uint64_t>(min_uv) << 32) | static_cast<uint64_t>(max_uv);
    }

    struct CompareQueueData {
        bool operator()(const queueData& a, const queueData& b) const {
            return a.err > b.err;
        }
    };

    std::priority_queue<queueData, std::vector<queueData>, CompareQueueData> pq;
    std::unordered_map<uint64_t, int, EdgeHash> edge_versions;

    void push_or_update(int u, int v, float err, const glm::vec4& vp);
    bool pop(queueData& out_data);
    void erase_edge(int u, int v);
    bool empty();
    int size() const;
};

#endif // QUEUE_H
