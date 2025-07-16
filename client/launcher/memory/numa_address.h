#ifndef NUMA_ADDRESS_H
#define NUMA_ADDRESS_H

#include <string>

struct NumaAddress {
    int serverId;
    int numaId;

    // 将NumaAddress转换为字符串，格式为"serverId:numaId"
    std::string toString() const {
        return std::to_string(serverId) + ":" + std::to_string(numaId);
    }

    // 从字符串解析出serverId和numaId
    static NumaAddress fromString(const std::string& str) {
        NumaAddress addr;
        size_t pos = str.find(':');
        if (pos != std::string::npos) {
            addr.serverId = std::stoi(str.substr(0, pos));
            addr.numaId = std::stoi(str.substr(pos+1));
        }
        return addr;
    }
};

#endif // NUMA_ADDRESS_H
