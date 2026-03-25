#include "TraceDispatcher.hpp"
#include "Fuzzer.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>

using OP = Fuzzer::traceOp;

namespace {

constexpr int kMatrixSliceBaseAgent = 2;
constexpr int kMatrixSliceCount = 4;
constexpr uint64_t kDefaultMldASegmentStride = 0x100;
constexpr uint64_t kDefaultMldAIntraStride = 0x900;

using MldASegments = std::array<std::vector<std::vector<uint64_t>>, kMatrixSliceCount>;

int sliceIndexFromAgent(int agentId) {
    int slice = agentId - kMatrixSliceBaseAgent;
    return (slice >= 0 && slice < kMatrixSliceCount) ? slice : -1;
}

bool isMarkerLine(const std::string& line, const std::string& marker) {
    return line.rfind(marker, 0) == 0;
}

std::string makeConstName(const std::string& key, int slice) {
    return "l2_matrix_mlda_" + key + "_slice" + std::to_string(slice) + "_0";
}

std::string resolveConstantinPath() {
    const char* noopHome = std::getenv("NOOP_HOME");
    if (noopHome && *noopHome) {
        return std::string(noopHome) + "/build/constantin.txt";
    }
    return std::string(DUT_PATH) + "/build/constantin.txt";
}

std::map<std::string, uint64_t> loadConstantinValues(const std::string& path) {
    std::map<std::string, uint64_t> values;
    std::ifstream input(path);
    if (!input.is_open()) {
        return values;
    }

    std::string key;
    uint64_t value = 0;
    while (input >> key >> value) {
        values[key] = value;
    }
    return values;
}

void storeConstantinValues(const std::string& path, const std::map<std::string, uint64_t>& values) {
    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open()) {
        std::cerr << "[TraceDispatcher] failed to update constantin file: " << path << std::endl;
        return;
    }

    for (const auto& [key, value] : values) {
        output << key << ' ' << value << '\n';
    }
}

void updateMatrixMldAConstantin(const MldASegments& segments) {
    auto values = loadConstantinValues(resolveConstantinPath());
    values["l2_matrix_mlda_enable0"] = 0;

    bool allSlicesLearned = true;
    for (int slice = 0; slice < kMatrixSliceCount; ++slice) {
        values[makeConstName("first_base", slice)] = 0;
        values[makeConstName("segment_stride", slice)] = kDefaultMldASegmentStride;
        values[makeConstName("intra_stride", slice)] = kDefaultMldAIntraStride;
        values[makeConstName("line_count", slice)] = 0;
        values[makeConstName("segment_count", slice)] = 0;

        const auto& sliceSegments = segments[slice];
        if (sliceSegments.empty() || sliceSegments.front().empty()) {
            allSlicesLearned = false;
            continue;
        }

        uint64_t firstBase = sliceSegments.front().front();
        uint64_t intraStride = kDefaultMldAIntraStride;
        uint64_t segmentStride = kDefaultMldASegmentStride;
        uint64_t lineCount = static_cast<uint64_t>(sliceSegments.front().size());
        uint64_t segmentCount = static_cast<uint64_t>(sliceSegments.size());

        if (sliceSegments.front().size() >= 2) {
            intraStride = sliceSegments.front()[1] - sliceSegments.front()[0];
        }
        if (sliceSegments.size() >= 2 && !sliceSegments[1].empty()) {
            segmentStride = sliceSegments[1].front() - sliceSegments.front().front();
        }

        values[makeConstName("first_base", slice)] = firstBase;
        values[makeConstName("segment_stride", slice)] = segmentStride;
        values[makeConstName("intra_stride", slice)] = intraStride;
        values[makeConstName("line_count", slice)] = lineCount;
        values[makeConstName("segment_count", slice)] = segmentCount;
    }

    values["l2_matrix_mlda_enable0"] = allSlicesLearned ? 1 : 0;
    storeConstantinValues(resolveConstantinPath(), values);

    std::cerr << "[TraceDispatcher] mld a learning "
              << (allSlicesLearned ? "enabled" : "incomplete")
              << " for l2_matrix_mlda_enable0" << std::endl;
    for (int slice = 0; slice < kMatrixSliceCount; ++slice) {
        std::cerr << "[TraceDispatcher] slice" << (slice + kMatrixSliceBaseAgent)
                  << " first_base=0x" << std::hex << values[makeConstName("first_base", slice)]
                  << " segment_stride=0x" << values[makeConstName("segment_stride", slice)]
                  << " intra_stride=0x" << values[makeConstName("intra_stride", slice)]
                  << std::dec
                  << " line_count=" << values[makeConstName("line_count", slice)]
                  << " segment_count=" << values[makeConstName("segment_count", slice)]
                  << std::endl;
    }
}

} // namespace

TraceDispatcher::TraceDispatcher(uint64_t *cycles, const std::string& path) {
    this->cycles = cycles;

    std::ifstream tracefile(path);
    if (!tracefile.is_open()) {
        std::cerr << "[TraceDispatcher] failed to open tracefile: " << path << std::endl;
        std::exit(EXIT_FAILURE);
    }

    entries.clear();
    MldASegments mldASegments;
    std::array<std::vector<uint64_t>, kMatrixSliceCount> currentMldASegment;
    bool inMldA = false;

    for (std::string line; std::getline(tracefile, line); ) {
        std::istringstream iss(line);
        std::string op;
        std::string token1;
        std::string token2;
        uint64_t address = 0;
        uint64_t vaddr   = 0;
        int agent_id = 0;

        if (!(iss >> op))
            continue;

        if (isMarkerLine(line, "!!!! mld a START")) {
            inMldA = true;
            for (auto& sliceAddrs : currentMldASegment) {
                sliceAddrs.clear();
            }
            continue;
        }
        if (isMarkerLine(line, "!!!! mld a END")) {
            if (inMldA) {
                for (int slice = 0; slice < kMatrixSliceCount; ++slice) {
                    if (!currentMldASegment[slice].empty()) {
                        mldASegments[slice].push_back(currentMldASegment[slice]);
                    }
                }
            }
            inMldA = false;
            continue;
        }

        // trace format: (1) <op> V=0x<vaddr> P=0x<paddr> <agent>
        //               (2) <op> 0x<paddr> <agent> [0x<vaddr>] (legacy, vaddr defaults to paddr)
        std::transform(op.begin(), op.end(), op.begin(), ::toupper);
        if ((iss >> token1) && token1.rfind("V=", 0) == 0) {
            token1 = token1.substr(2);
            if (!(iss >> token2) || token2.rfind("P=", 0) != 0)
                continue;
            token2 = token2.substr(2);
            vaddr   = std::stoull(token1, nullptr, 16);
            address = std::stoull(token2, nullptr, 16);
            if (!(iss >> agent_id))
                continue;
        } else {
            iss.clear();
            iss.str(line);
            iss >> op;
            if (!(iss >> std::hex >> address >> agent_id))
                continue;
            if (!(iss >> std::hex >> vaddr))
                vaddr = address;
        }

        if (inMldA && op == "MR") {
            int slice = sliceIndexFromAgent(agent_id);
            if (slice >= 0) {
                currentMldASegment[slice].push_back(address);
            }
        }

        uint8_t mapped_op = 0xFF;
        if (op == "LD" || op == "IF" || op == "MR")
            mapped_op = OP::READ;
        else if (op == "CR")
            mapped_op = OP::MODIFY;
        else if (op == "ST" || op == "MW")
            mapped_op = OP::WRITE;
        else if (op == "EV")
            mapped_op = OP::EVICT;
        else if (op == "FE")
            mapped_op = OP::FENCE;
        else
            continue;

        TraceEntry e;
        e.agentId = agent_id;
        e.op      = mapped_op;
        e.addr    = address;
        e.user    = vaddr;
        entries.push_back(e);
    }

    updateMatrixMldAConstantin(mldASegments);

    std::cerr << "[TraceDispatcher] loaded " << entries.size()
              << " entries from " << path << std::endl;
}

bool TraceDispatcher::send(const std::function<bool(int,const TraceEntry&,bool*)>& tryIssue)
{
    if (entries.empty()) return false;

    const TraceEntry& e = entries.front();

    if (*cycles % 10000 == 0) {
        LogX("%ld [Dump] sent: R=%d W=%d E=%d, recvd: R=%d W=%d E=%d, ",
            *cycles/2,
            numReadSent, numWriteSent, numEvictSent,
            numReadReceived, numWriteReceived, numEvictReceived);
        LogX("     reqs left: %lu\n", entries.size());
        if (e.op == OP::FENCE) {
            LogX("      FENCE waiting\n");
        }
    }

    if (e.op == OP::FENCE) {
        // check whether all previous requests have been completed
        if (numReadSent == numReadReceived &&
            numWriteSent == numWriteReceived &&
            numEvictSent == numEvictReceived) {
            // all previous requests completed, we can issue the fence
            LogX("%ld issue Fence(0x%lx)\n", *cycles/2, e.addr);
            entries.pop_front();
            return true;
        }
        return false;
    }

    bool skip = false;
    if (tryIssue(e.agentId, e, &skip)) {
        if (e.op == OP::READ || e.op == OP::MODIFY)
            numReadSent++;
        else if (e.op == OP::WRITE)
            numWriteSent++;
        else if (e.op == OP::EVICT && !skip)
            numEvictSent++;

        entries.pop_front();
        return true;
    }
    return false;
}
