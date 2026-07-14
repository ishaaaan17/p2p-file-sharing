#pragma once
#include <map>
#include <cstdint>
#include <string>

namespace P2P {

    struct PeerStats {
        uint64_t bytes_uploaded = 0;
        uint64_t bytes_downloaded = 0;
        bool am_choking = true;
        bool is_choking_me = true;
    };

    class ChokeManager {
    private:
        std::map<uint16_t, PeerStats> m_swarm_stats;
        const uint64_t m_min_cooperation_threshold = 1000; // Min bytes required to remain unchoked

    public:
        ChokeManager() = default;
        ~ChokeManager() = default;

        // Record metrics to evaluate game theory decisions
        void record_upload(uint16_t port, uint32_t bytes) { m_swarm_stats[port].bytes_uploaded += bytes; }
        void record_download(uint16_t port, uint32_t bytes) { m_swarm_stats[port].bytes_downloaded += bytes; }

        // Core Tit-for-Tat Logic: Decides whether a peer should be choked or unchoked
        bool should_choke_peer(uint16_t port) {
            auto& stats = m_swarm_stats[port];
            // If they are downloading significantly more than they upload, choke them!
            if (stats.bytes_uploaded > 0 && stats.bytes_downloaded == 0) {
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