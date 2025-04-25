//
// Created by Deamon on 4/22/2025.
//

#include "BLTE.h"

// BLTE.cpp
#include "BLTE.h"
#include <stdexcept>
#include <zlib.h>
#include <cstring>

#include "utils/DataReader.h"
#include "utils/KeyService.h"

// In BLTE.cpp, using DataReader:

std::vector<uint8_t> BLTE::Decode(const std::vector<uint8_t>& data, uint64_t totalDecompSize) {
    const size_t fixedHeaderSize = 8;
    if (data.size() < fixedHeaderSize + 1)
        throw std::runtime_error("Invalid BLTE header");

    DataReader dr(const_cast<uint8_t*>(data.data()), data.size());

    // 1) Magic check
    if (dr.ReadUInt8() != 'B' ||dr.ReadUInt8() != 'L' ||dr.ReadUInt8() != 'T' || dr.ReadUInt8() != 'E')
    {
        throw std::runtime_error("Invalid BLTE header");
    }

    // 2) headerSize (BE u32)
    uint32_t headerSize = dr.ReadUInt32BE();

    // 3) Single-block
    if (headerSize == 0) {
        dr.SetOffset(fixedHeaderSize);
        char mode = static_cast<char>(dr.ReadUInt8());

        if (mode != 'N' && totalDecompSize == 0)
            throw std::runtime_error(
                "totalDecompSize must be set for single non-normal BLTE block"
            );
        if (mode == 'N' && totalDecompSize == 0)
            totalDecompSize = data.size() - fixedHeaderSize - 1;

        std::vector<uint8_t> singleDecomp(static_cast<size_t>(totalDecompSize));

        size_t compOffset = fixedHeaderSize + 1;
        size_t compSize   = data.size() - compOffset;

        HandleDataBlock(
            mode,
            data.data() + compOffset,
            compSize,
            /*chunkIndex=*/0,
            singleDecomp.data(),
            singleDecomp.size()
        );
        return singleDecomp;
    }

    // 4) Multi-chunk
    if (data.size() < headerSize)
        throw std::runtime_error("Data too small for declared headerSize");

    // tableFormat and chunkCount
    dr.SetOffset(fixedHeaderSize);
    char tableFormat = static_cast<char>(dr.ReadUInt8());
    if (tableFormat != static_cast<char>(0xF))
        throw std::runtime_error("Unexpected BLTE table format");

    uint32_t chunkCount = dr.ReadUInt24BE();

    constexpr size_t blockInfoSize = 24;
    size_t infoStart = fixedHeaderSize + 4; // = 12

    if (totalDecompSize == 0) {
        for (uint32_t i = 0; i < chunkCount; ++i) {
            dr.SetOffset(infoStart + 4 + i * blockInfoSize);
            totalDecompSize += dr.ReadUInt32BE();
        }
    }

    std::vector<uint8_t> decompData(static_cast<size_t>(totalDecompSize));

    size_t infoOffset   = infoStart;
    size_t compOffset   = headerSize;
    size_t decompOffset = 0;

    for (uint32_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
        dr.SetOffset(infoOffset);
        uint32_t compSize   = dr.ReadUInt32BE();
        uint32_t decompSize = dr.ReadUInt32BE();

        char mode = static_cast<char>(data[compOffset]);

        HandleDataBlock(
            mode,
            data.data() + compOffset + 1,
            compSize - 1,
            static_cast<int>(chunkIndex),
            decompData.data() + decompOffset,
            decompSize
        );

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
    DataReader dr(const_cast<uint8_t*>(data), dataSize);

    // 1) keyNameSize
    uint8_t keyNameSize = dr.ReadUInt8();
    if (keyNameSize != 8)
        throw std::runtime_error("keyNameSize must be 8");

    // 2) keyName (little-endian u64)
    uint64_t keyName = dr.ReadUInt64LE();

    // 3) lookup key
    std::vector<uint8_t> key;
    if (!KeyService::TryGetKey(keyName, key))
        return false;

    // 4) ivSize
    uint8_t ivSize = dr.ReadUInt8();
    if (ivSize != 4 || ivSize > 0x10)
        throw std::runtime_error("IVSize invalid");

    // 5) IV bytes
    std::vector<uint8_t> iv = dr.ReadUint8Array(ivSize);

    // pad IV out to 8 bytes
    iv.resize(8, 0);

    // 6) encryption type
    char encType = static_cast<char>(dr.ReadUInt8());
    if (encType != 'S' && encType != 'A')
        throw std::runtime_error(std::string("Unhandled encryption type: ") + encType);

    // 7) XOR chunkIndex into first 4 bytes of IV
    for (int i = 0; i < 4; ++i) {
        iv[i] ^= static_cast<uint8_t>((chunkIndex >> (i * 8)) & 0xFF);
    }

    // 8) decrypt payload
    size_t dataOffset     = dr.GetOffset();
    size_t encryptedSize  = dataSize - dataOffset;

    if (encType == 'S') {
        output.resize(encryptedSize);
        // decrypt with Salsa
        //TODO::!!!
        // KeyService::SalsaInstance().Decrypt(key, iv, data + dataOffset, encryptedSize, output.data());
        return true;
    }
    else {
        // ARC4 not implemented
        throw std::runtime_error("Encryption type 'A' (ARC4) not implemented");
    }
}
