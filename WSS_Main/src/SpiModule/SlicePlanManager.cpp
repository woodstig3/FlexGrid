/*
 * SlicePlanManager.cpp
 *
 *  Created on: Apr 1, 2025
 *      Author: Administrator
 */

#include <algorithm>
#include <iostream>
#include <sstream>
#include <memory>
#include "SlicePlanManager.h"


using namespace std;

//std::unique_ptr<CmdDecoder> SlicePlanManager::g_cmdDecoder = nullptr;
std::list<ChannelModules> CmdDecoder::activeChannels(0);

SlicePlanManager::SlicePlanManager()
{
/*	if(!g_cmdDecoder) {
		g_cmdDecoder = std::make_unique<CmdDecoder>();
	}

	restoreActiveConfig();
*/
}

SlicePlanManager::~SlicePlanManager() = default; // Define destructor

/*
void SlicePlanManager::updateRanges(uint8_t wss_id, const SliceRange& new_range, const SliceConfig& config) {
    auto& ranges = active_config_ranges[wss_id];
    vector<SliceRange> to_add;
    vector<SliceRange> to_remove;

    // Find all overlapping ranges
    for (const auto& range_config_pair : ranges) {
        const SliceRange& existing_range = range_config_pair.first;
        const SliceConfig& existing_config = range_config_pair.second;

        if (new_range.end < existing_range.start || new_range.start > existing_range.end) {
            continue; // No overlap
        }

        // Mark for removal (we'll split it)
        to_remove.push_back(existing_range);

        // Left non-overlapping part
        if (existing_range.start < new_range.start) {
            to_add.push_back(SliceRange{existing_range.start, static_cast<uint16_t>(new_range.start - 1)});
        }

        // Right non-overlapping part
        if (existing_range.end > new_range.end) {
            to_add.push_back(SliceRange{static_cast<uint16_t>(new_range.end + 1), existing_range.end});
        }
    }

    // Remove the old ranges
    for (const auto& range : to_remove) {
        ranges.erase(range);
    }

    // Add the split ranges (keeping their original config)
    for (const auto& range : to_add) {
        // Find the original config for this range
        for (const auto& old_pair : active_config_ranges[wss_id]) {
            if (range.start >= old_pair.first.start && range.end <= old_pair.first.end) {
                ranges[range] = old_pair.second;
                break;
            }
        }
    }

    // Add the new range
    ranges[new_range] = config;
}
*/

void SlicePlanManager::updateActiveConfig(const SPACommand& cmd) {
    for (const auto& assignment : cmd.assignments) {
        uint16_t start = std::get<0>(assignment);
        uint16_t end = std::get<1>(assignment);
        PortPair ports = std::get<2>(assignment);
        int16_t atten = std::get<3>(assignment);

        updateRanges(cmd.wss_id, SliceRange{start, end}, SliceConfig{ports, atten});
    }
}


void SlicePlanManager::updateRanges(uint8_t wss_id, const SliceRange& new_range, const SliceConfig& config) {
    auto& ranges = active_config_ranges[wss_id];
    vector<pair<SliceRange, SliceConfig>> to_add;
    vector<SliceRange> to_remove;

    // Find all overlapping ranges and prepare splits
    for (const auto& existing : ranges) {
        const SliceRange& existing_range = existing.first;
        const SliceConfig& existing_config = existing.second;

        if (new_range.end < existing_range.start || new_range.start > existing_range.end) {
            continue; // No overlap
        }

        to_remove.push_back(existing_range);

        // Left non-overlapping part (keep original config)
        if (existing_range.start < new_range.start) {
            to_add.emplace_back(
                SliceRange{existing_range.start, static_cast<uint16_t>(new_range.start-1)},
                existing_config
            );
        }

        // Right non-overlapping part (keep original config)
        if (existing_range.end > new_range.end) {
            to_add.emplace_back(
                SliceRange{static_cast<uint16_t>(new_range.end+1), existing_range.end},
                existing_config
            );
        }
    }

    // Apply changes
    for (const auto& range : to_remove) {
        ranges.erase(range);
    }

    for (const auto& range_config : to_add) {
        ranges[range_config.first] = range_config.second;
    }

    // Add/update the new range
    ranges[new_range] = config;

    // Debug validation
    if (debug_logging) {
        log() << "After updateRanges:\n";
        for (const auto& r : ranges) {
            log() << "  " << r.first.start << "-" << r.first.end
                  << "->P" << (int)r.second.ports.common_port
                  << ":" << (int)r.second.ports.switching_port
                  << "@" << r.second.attenuation << "cB\n";
        }
    }
}


/*
bool SlicePlanManager::processSPACommand(const SPACommand& cmd) {
    lock_guard<mutex> lock(config_mutex);
    logCommand("PROCESS", cmd);
    // Check for overlaps with executing commands
    bool has_overlap = false;
    for (const auto& assignment : cmd.assignments) {
        uint16_t start = std::get<0>(assignment);
        uint16_t end = std::get<1>(assignment);

        for (const auto& executing_cmd : executing_commands) {
            if (executing_cmd.wss_id != cmd.wss_id) continue;

            for (const auto& exec_assignment : executing_cmd.assignments) {
                uint16_t e_start = std::get<0>(exec_assignment);
                uint16_t e_end = std::get<1>(exec_assignment);
                bool overlaps = !(end < e_start || start > e_end);
                logOverlapCheck(start, end, e_start, e_end, overlaps);
                if (overlaps) {
                    has_overlap = true;
                    break;
                }
            }
            if (has_overlap) break;
        }
        if (has_overlap) break;
    }

    if (!has_overlap) {
        // No overlap - execute immediately
        executing_commands.push_back(cmd);
        updateActiveConfig(cmd);
        logSliceRanges(cmd.wss_id);
        return true;
    }

    log() << "  Overlaps detected - queuing command\n";
    // Has overlap - add to queue, replacing any existing assignments for the same slices
    for (const auto& assignment : cmd.assignments) {
        uint16_t start = std::get<0>(assignment);
        uint16_t end = std::get<1>(assignment);

        SliceRange slice_range{start, end};
        queued_commands[slice_range] = cmd; // Overwrites any existing entry for this range
    }
    logSliceRanges(cmd.wss_id);
    return false;
}*/

bool SlicePlanManager::processSPACommand(const SPACommand& cmd) {
    std::lock_guard<std::mutex> lock(config_mutex);
    logCommand("PROCESS", cmd);

    bool has_overlap = false;
    // Track all overlap checks for this command
    std::vector<std::tuple<uint16_t, uint16_t, uint16_t, uint16_t, bool>> overlap_results;

    for (const auto& assignment : cmd.assignments) {
        uint16_t start = std::get<0>(assignment);
        uint16_t end = std::get<1>(assignment);

        for (const auto& executing_cmd : executing_commands) {
            if (executing_cmd.wss_id != cmd.wss_id) continue;

            for (const auto& exec_assignment : executing_cmd.assignments) {
                uint16_t e_start = std::get<0>(exec_assignment);
                uint16_t e_end = std::get<1>(exec_assignment);
                bool overlaps = !(end < e_start || start > e_end);

                if (overlaps) {
                	has_overlap = true;
                	overlap_results.emplace_back(start, end, e_start, e_end, overlaps);
                }
                // DON'T break here - we want to log ALL checks
            }
        }
    }

    // Log all overlap checks for this command
    if (debug_logging && !overlap_results.empty()) {
        log() << "Overlap Check Results:\n";
        for (const auto& result : overlap_results) {
            logOverlapCheck(std::get<0>(result), std::get<1>(result),
                           std::get<2>(result), std::get<3>(result),
                           std::get<4>(result));
        }
    }

    if (!has_overlap) {
        log() << "  No overlaps found - executing immediately\n";
        executing_commands.push_back(cmd);
        updateActiveConfig(cmd);
        logSliceRanges(cmd.wss_id);
        return true;
    }

    log() << "  Overlaps detected - queuing command\n";
    for (const auto& assignment : cmd.assignments) {
        uint16_t start = std::get<0>(assignment);
        uint16_t end = std::get<1>(assignment);
        queued_commands[SliceRange{start, end}] = cmd;
        if (debug_logging) {
            log() << "    Queued range: " << start << "-" << end << "\n";
        }
    }

    logSliceRanges(cmd.wss_id);
    return false;
}

map<uint16_t, SliceConfig> SlicePlanManager::getCurrentConfig(uint8_t wss_id) const {
    lock_guard<mutex> lock(config_mutex);
    map<uint16_t, SliceConfig> result;

    if (active_config_ranges.count(wss_id)) {
        for (const auto& range_config_pair : active_config_ranges.at(wss_id)) {
            const SliceRange& range = range_config_pair.first;
            const SliceConfig& config = range_config_pair.second;

            for (uint16_t slice = range.start; slice <= range.end; ++slice) {
                result[slice] = config;
            }
        }
    }

    return result;
}

bool SlicePlanManager::hasPendingCommands() const {
    lock_guard<mutex> lock(config_mutex);
    return !queued_commands.empty();
}

void SlicePlanManager::onCommandComplete(const SPACommand& completed_cmd) {
    lock_guard<mutex> lock(config_mutex);

    // Remove completed command from executing list
    executing_commands.erase(
        remove_if(executing_commands.begin(), executing_commands.end(),
            [&completed_cmd](const SPACommand& cmd) {
                return cmd.timestamp == completed_cmd.timestamp;
            }),
        executing_commands.end()
    );

    // Check if any queued commands can now be executed
    vector<SPACommand> to_execute;
    for (auto it = queued_commands.begin(); it != queued_commands.end(); ) {
        bool can_execute = true;

        for (const auto& assignment : it->second.assignments) {
            uint16_t start = std::get<0>(assignment);
            uint16_t end = std::get<1>(assignment);

            for (const auto& executing_cmd : executing_commands) {
                if (executing_cmd.wss_id != it->second.wss_id) continue;

                for (const auto& exec_assignment : executing_cmd.assignments) {
                    uint16_t e_start = std::get<0>(exec_assignment);
                    uint16_t e_end = std::get<1>(exec_assignment);

                    if (!(end < e_start || start > e_end)) {
                        can_execute = false;
                        break;
                    }
                }
                if (!can_execute) break;
            }
            if (!can_execute) break;
        }

        if (can_execute) {
            to_execute.push_back(it->second);
            it = queued_commands.erase(it);
        } else {
            ++it;
        }
    }

    // Execute commands that can now run
    for (const auto& cmd : to_execute) {
        executing_commands.push_back(cmd);
        updateActiveConfig(cmd);
    }
}

void SlicePlanManager::logCommand(const std::string& phase, const SPACommand& cmd) const {
    if (!debug_logging) return;

    log() << "[" << phase << "] WSS" << (int)cmd.wss_id << " Command: ";
    for (const auto& assignment : cmd.assignments) {
        uint16_t start = std::get<0>(assignment);
        uint16_t end = std::get<1>(assignment);
        const auto& ports = std::get<2>(assignment);
        int16_t atten = std::get<3>(assignment);

        log() << start << "-" << end << "->P" << (int)ports.common_port
              << ":" << (int)ports.switching_port << "@" << atten << "cB ";
    }
    log() << "\n";
}

void SlicePlanManager::logSliceRanges(uint8_t wss_id) const {
    if (!debug_logging) return;

    log() << "Current Slice Plan for WSS" << (int)wss_id << ":\n";
    if (active_config_ranges.count(wss_id)) {
        for (const auto& range_config : active_config_ranges.at(wss_id)) {
            const auto& range = range_config.first;
            const auto& config = range_config.second;
            log() << "  " << range.start << "-" << range.end
                  << "->P" << (int)config.ports.common_port
                  << ":" << (int)config.ports.switching_port
                  << "@" << config.attenuation << "cB\n";
        }
    }
    log() << "Pending commands: " << queued_commands.size()
          << ", Executing: " << executing_commands.size() << "\n";
}

void SlicePlanManager::logOverlapCheck(uint16_t start, uint16_t end,
                                      uint16_t e_start, uint16_t e_end,
                                      bool overlaps) const {
    if (!debug_logging) return;

    log() << "  Overlap Check: [" << start << "," << end << "] vs ["
          << e_start << "," << e_end << "] -> "
          << (overlaps ? "OVERLAP" : "clear") << "\n";
}


