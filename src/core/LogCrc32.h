#pragma once
#include <QByteArrayView>
#include <QtGlobal>
#include <array>

namespace pat {
// IEEE CRC-32, compatible with existing PATMBIN1 blocks and indexes.
inline quint32 LogCrc32(QByteArrayView bytes) {
    static constexpr auto table = [] {
        std::array<quint32, 256> values{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 crc = i;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
            values[i] = crc;
        }
        return values;
    }();
    quint32 crc = 0xFFFFFFFFu;
    for (char byte : bytes) crc = (crc >> 8) ^ table[(crc ^ quint8(byte)) & 0xFFu];
    return ~crc;
}
}
