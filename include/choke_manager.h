#pragma once
#include <map>
#include <cstdint>
#include <string>

namespace P2P {

    struct PeerStats {
        // Bytes WE have served TO this peer (our uploads to them).
        uint64_t bytes_uploaded = 0;
        // Bytes WE have received FROM this peer (their uploads to us).
        uint64_t bytes_downloaded = 0;
        // Peers start UNCHOKED. The previous default (true) combined with a
        // set_choke_status(port, false) call that existed nowhere in the
        // codebase meant the CHOKE-send branch was unreachable and serving
        // state was never what the code thought it was.
        bool am_choking = false;
        bool is_choking_me = true;
        // Can this peer actually give anything back? Set by the caller from
        // bitfield knowledge: a peer that owns nothing we need can never
        // reciprocate, so choking it punishes it for something impossible.
        bool can_reciprocate = false;
    };

    class ChokeManager {
    private:
        std::map<uint16_t, PeerStats> m_swarm_stats;
        const uint64_t m_min_cooperation_threshold = 1000; // Min bytes required to remain unchoked
        // Grace allowance: the first piece served to any peer is always free.
        // Tit-for-Tat is evaluated on what happens after that first piece.
        const uint64_t m_grace_bytes = 1000;

    public:
        ChokeManager() = default;
        ~ChokeManager() = default;

        // Record metrics to evaluate game theory decisions
        void record_upload(uint16_t port, uint32_t bytes) { m_swarm_stats[port].bytes_uploaded += bytes; }
        void record_download(uint16_t port, uint32_t bytes) { m_swarm_stats[port].bytes_downloaded += bytes; }

        // Tell the manager whether this peer owns anything we still need,
        // i.e. whether reciprocation is even possible for them.
        void set_can_reciprocate(uint16_t port, bool can_reciprocate) { m_swarm_stats[port].can_reciprocate = can_reciprocate; }
        bool get_can_reciprocate(uint16_t port) { return m_swarm_stats[port].can_reciprocate; }

        // Core Tit-for-Tat Logic: Decides whether a peer should be choked or unchoked
        bool should_choke_peer(uint16_t port) {
            auto& stats = m_swarm_stats[port];
            // A peer that cannot reciprocate is never choked. This is what
            // lets a pure seeder (which needs nothing back) serve a leecher
            // to completion instead of stalling it after the first piece.
            if (!stats.can_reciprocate) {
                return false;
            }
            // Grace period: never choke over the first piece.
            if (stats.bytes_uploaded <= m_grace_bytes) {
                return false;
            }
            // They are taking without giving anything back, choke them!
            if (stats.bytes_downloaded == 0) {
                return true;
            }
            if (stats.bytes_uploaded > m_min_cooperation_threshold && stats.bytes_downloaded < (stats.bytes_uploaded / 2)) {
                return true;
            }
            return false;
        }

        void set_choke_status(uint16_t port, bool am_choking) { m_swarm_stats[port].am_choking = am_choking; }
        bool get_choke_status(uint16_t port) { return m_swarm_stats[port].am_choking; }
    };

} // namespace P2P
