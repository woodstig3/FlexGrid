/*
 * SlicePlanManager.h
 *
 *  Created on: Apr 1, 2025
 *      Author: Administrator
 */

#ifndef SLICE_PLAN_MANAGER_H
#define SLICE_PLAN_MANAGER_H

#include <map>
#include <vector>
#include <tuple>
#include <chrono>
#include <mutex>
#include <utility>

struct PortPair {
    uint8_t common_port;
    uint8_t switching_port;

    bool operator<(const PortPair& other) const {
        return (common_port < other.common_port) ||
               (common_port == other.common_port && switching_port < other.switching_port);
    }

    bool operator==(const PortPair& other) const {
        return (common_port == other.common_port) &&
               (switching_port == other.switching_port);
    }
};

struct SliceConfig {
    PortPair ports;
    int16_t attenuation; // in cB (1dB = 10cB)

    bool operator==(const SliceConfig& other) const {
        return (ports == other.ports) && (attenuation == other.attenuation);
    }
};

struct SliceRange {
    uint16_t start;
    uint16_t end;

    bool operator<(const SliceRange& other) const {
        return (start < other.start) || (start == other.start && end < other.end);
    }
};

struct SPACommand {
    uint8_t wss_id;
    std::vector<std::tuple<uint16_t, uint16_t, PortPair, int16_t>> assignments;
    std::chrono::system_clock::time_point timestamp;
};

class SlicePlanManager {
public:
    bool processSPACommand(const SPACommand& cmd);
    std::map<uint16_t, SliceConfig> getCurrentConfig(uint8_t wss_id) const;
    bool hasPendingCommands() const;
    void onCommandComplete(const SPACommand& completed_cmd);

    std::map<uint8_t, std::map<SliceRange, SliceConfig>> active_config_ranges;

private:
    std::vector<SPACommand> executing_commands;
    std::map<SliceRange, SPACommand> queued_commands;
    mutable std::mutex config_mutex;

    void updateRanges(uint8_t wss_id, const SliceRange& new_range, const SliceConfig& config);
    void updateActiveConfig(const SPACommand& cmd);

public:
    // Enable/disable debug logging
    void setDebugLogging(bool enabled) { debug_logging = enabled; }

private:
    bool debug_logging = false;
    std::ostream& log() const {
        return debug_logging? std::cerr : std::cout;
    }

    // Logging helpers
    void logCommand(const std::string& phase, const SPACommand& cmd) const;
    void logSliceRanges(uint8_t wss_id) const;
    void logOverlapCheck(uint16_t start, uint16_t end,
                        uint16_t e_start, uint16_t e_end,
                        bool overlaps) const;
};

#endif // SLICE_PLAN_MANAGER_H
