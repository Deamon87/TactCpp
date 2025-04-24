//
// Created by Deamon on 4/22/2025.
//

#include "BLTE.h"

// BLTE.cpp
#include "BLTE.h"
#include <stdexcept>
#include <zlib.h>
#include <cstring>

#include "utils/KeyService.h"

uint32_t BLTE::ReadUInt32BE(const uint8_t* ptr) {
    return (static_cast<uint32_t>(ptr[0]) << 24)
         | (static_cast<uint32_t>(ptr[1]) << 16)
         | (static_cast<uint32_t>(ptr[2]) << 8)
         |  static_cast<uint32_t>(ptr[3]);
}

uint32_t BLTE::ReadUInt24BE(const uint8_t* ptr) {
    return (static_cast<uint32_t>(ptr[0]) << 16)
         | (static_cast<uint32_t>(ptr[1]) << 8)
         |  static_cast<uint32_t>(ptr[2]);
}

uint64_t BLTE::ReadUInt64LE(const uint8_t* ptr) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(ptr[i]) << (8 * i);
    }
    return v;
}

std::vector<uint8_t> BLTE::Decode(const std::vector<uint8_t>& data, uint64_t totalDecompSize) {
    const size_t fixedHeaderSize = 8;
    if (data.size() < fixedHeaderSize + 1
        || data[0] != 'B' || data[1] != 'L' || data[2] != 'T' || data[3] != 'E') {
        throw std::runtime_error("Invalid BLTE header");
    }
    uint32_t headerSize = ReadUInt32BE(data.data() + 4);
    if (headerSize == 0) {
        char mode = static_cast<char>(data[fixedHeaderSize]);
        if (mode != 'N' && totalDecompSize == 0)
            throw std::runtime_error("totalDecompSize must be set for single non-normal BLTE block");
        if (mode == 'N' && totalDecompSize == 0)
            totalDecompSize = data.size() - fixedHeaderSize - 1;

        std::vector<uint8_t> singleDecomp(static_cast<size_t>(totalDecompSize));
        HandleDataBlock(mode,
                        data.data() + fixedHeaderSize + 1,
                        data.size() - fixedHeaderSize - 1,
                        0,
                        singleDecomp.data(),
                        singleDecomp.size());
        return singleDecomp;
    }
    if (data.size() < headerSize)
        throw std::runtime_error("Data too small for declared headerSize");

    uint8_t tableFormat = data[fixedHeaderSize];
    if (tableFormat != 0xF)
        throw std::runtime_error("Unexpected BLTE table format");

    size_t blockInfoSize = 24;
    uint32_t chunkCount = ReadUInt24BE(data.data() + fixedHeaderSize + 1);
    size_t infoStart = fixedHeaderSize + 4;

    if (totalDecompSize == 0) {
        size_t scanOffset = infoStart + 4;
        for (uint32_t i = 0; i < chunkCount; ++i) {
            totalDecompSize += ReadUInt32BE(data.data() + scanOffset);
            scanOffset += blockInfoSize;
        }
    }

    std::vector<uint8_t> decompData(static_cast<size_t>(totalDecompSize));
    size_t infoOffset = infoStart;
    size_t compOffset = headerSize;
    size_t decompOffset = 0;

    for (uint32_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
        uint32_t compSize   = ReadUInt32BE(data.data() + infoOffset);
        uint32_t decompSize = ReadUInt32BE(data.data() + infoOffset + 4);
        char mode = static_cast<char>(data[compOffset]);

        HandleDataBlock(mode,
                        data.data() + compOffset + 1,
                        compSize - 1,
                        static_cast<int>(chunkIndex),
                        decompData.data() + decompOffset,
                        decompSize);

        infoOffset   += blockInfoSize;
        compOffset   += compSize;
        decompOffset += decompSize;
    }

    return decompData;
}

void BLTE::HandleDataBlock(char mode,
                           const uint8_t* compData, size_t compSize,
                           int chunkIndex,
                           uint8_t* decompData, size_t decompSize) {
    switch (mode) {
        case 'N':
            std::memcpy(decompData, compData, decompSize);
            break;

        case 'Z': {
            z_stream stream;
            std::memset(&stream, 0, sizeof(stream));
            stream.next_in   = const_cast<Bytef*>(compData);
            stream.avail_in  = static_cast<uInt>(compSize);
            stream.next_out  = decompData;
            stream.avail_out = static_cast<uInt>(decompSize);
            if (inflateInit(&stream) != Z_OK)
                throw std::runtime_error("Failed to init zlib inflate");

            int ret = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
            if (ret != Z_STREAM_END)
                throw std::runtime_error("Zlib decompression error");
            break;
        }

        case 'F':
            throw std::runtime_error("Frame decompression not implemented");

        case 'E': {
            std::vector<uint8_t> decrypted;
            if (TryDecrypt(compData, compSize, chunkIndex, decrypted) && !decrypted.empty()) {
                char nestedMode = static_cast<char>(decrypted[0]);
                HandleDataBlock(nestedMode,
                                decrypted.data() + 1,
                                decrypted.size() - 1,
                                chunkIndex,
                                decompData,
                                decompSize);
            }
            break;
        }

        default:
            throw std::runtime_error(std::string("Invalid BLTE chunk mode: ") + mode);
    }
}

bool BLTE::TryDecrypt(const uint8_t* data, size_t dataSize,
                       int chunkIndex,
                       std::vector<uint8_t>& output) {
    if (dataSize < 1)
        throw std::runtime_error("Invalid data for decrypt");

    uint8_t keyNameSize = data[0];

    if (keyNameSize != 8)
        throw std::runtime_error("keyNameSize must be 8");

    if (dataSize < keyNameSize + 2)
        throw std::runtime_error("Data too small for keyName and IV size");

    uint64_t keyName = ReadUInt64LE(data + 1);

    std::vector<uint8_t> key;
    if (!KeyService::TryGetKey(keyName, key))
        return false;

    size_t ivSizeOffset = 1 + keyNameSize;
    uint8_t ivSize = data[ivSizeOffset];
    if (ivSize != 4 || ivSize > 0x10)
        throw std::runtime_error("IVSize invalid");

    size_t ivOffset = ivSizeOffset + 1;
    if (dataSize < ivOffset + ivSize + 1)
        throw std::runtime_error("Data too small for IV and encType");

    std::vector<uint8_t> iv(ivSize);
    std::memcpy(iv.data(), data + ivOffset, ivSize);
    iv.resize(8, 0);

    char encType = static_cast<char>(data[ivOffset + ivSize]);
    if (encType != 'S' && encType != 'A')
        throw std::runtime_error(std::string("Unhandled encryption type: ") + encType);

    // XOR chunkIndex into first 4 bytes of IV
    for (int i = 0; i < 4; ++i) {
        iv[i] ^= static_cast<uint8_t>((chunkIndex >> (i * 8)) & 0xFF);
    }

    if (encType == 'S') {
        size_t dataOffset = ivOffset + ivSize + 1;
        size_t encryptedSize = dataSize - dataOffset;
        output.resize(encryptedSize);
        //TODO::!!!
        // KeyService::SalsaInstance().Decrypt(key, iv, data + dataOffset, encryptedSize, output.data());
        return true;
    } else {
        throw std::runtime_error("Encryption type 'A' (ARC4) not implemented");
    }
}
