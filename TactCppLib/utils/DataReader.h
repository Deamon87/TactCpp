#ifndef DATAREADER_H
#define DATAREADER_H

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstring>
#include <string>

class DataReader
{
private:
    uint8_t*   m_ptr;
    std::size_t m_availableSize;
    std::size_t m_offset;

public:
    /// Construct with buffer pointer, total size, and optional initial offset
    DataReader(uint8_t* ptr, std::size_t availableSize, std::size_t offset = 0)
        : m_ptr(ptr)
        , m_availableSize(availableSize)
        , m_offset(offset)
    {
        // Can't start beyond the buffer
        assert(m_offset <= m_availableSize);
    }

    /// Get current offset
    std::size_t GetOffset() const { return m_offset; }
    /// Set offset explicitly (must still be in-range)
    void SetOffset(std::size_t offset)
    {
        assert(offset <= m_availableSize);
        m_offset = offset;
    }

    inline int8_t ReadInt8()
    {
        constexpr auto ReadSize = 1u;
        assert(m_offset + ReadSize <= m_availableSize);

        int8_t v = static_cast<int8_t>(m_ptr[m_offset]);
        m_offset += ReadSize;
        return v;
    }

    inline uint8_t ReadUInt8()
    {
        constexpr auto ReadSize = 1u;
        assert(m_offset + ReadSize <= m_availableSize);

        uint8_t v = m_ptr[m_offset];
        m_offset += ReadSize;
        return v;
    }

    /// Read big-endian signed 16-bit
    inline int16_t ReadInt16BE()
    {
        constexpr std::size_t ReadSize = 2;
        assert(m_offset + ReadSize <= m_availableSize);

        int16_t value = static_cast<int16_t>(
            (m_ptr[m_offset] << 8) |
             m_ptr[m_offset + 1]
        );

        m_offset += ReadSize;
        return value;
    }

    /// Read big-endian unsigned 24-bit
    inline uint32_t ReadUInt24BE()
    {
        constexpr std::size_t ReadSize = 3;
        assert(m_offset + ReadSize <= m_availableSize);

        uint32_t value =
            (uint32_t(m_ptr[m_offset    ]) << 16) |
            (uint32_t(m_ptr[m_offset + 1]) <<  8) |
             uint32_t(m_ptr[m_offset + 2]);

        m_offset += ReadSize;
        return value;
    }

    /// Read big-endian unsigned 16-bit
    inline uint16_t ReadUInt16BE()
    {
        constexpr std::size_t ReadSize = 2;
        assert(m_offset + ReadSize <= m_availableSize);

        uint16_t value = (uint16_t(m_ptr[m_offset]) << 8)
                       |  uint16_t(m_ptr[m_offset + 1]);

        m_offset += ReadSize;
        return value;
    }

    /// Read big-endian signed 32-bit
    inline int32_t ReadInt32BE()
    {
        constexpr std::size_t ReadSize = 4;
        assert(m_offset + ReadSize <= m_availableSize);

        int32_t value = (int32_t(m_ptr[m_offset]) << 24) |
                        (int32_t(m_ptr[m_offset + 1]) << 16) |
                        (int32_t(m_ptr[m_offset + 2]) <<  8) |
                         int32_t(m_ptr[m_offset + 3]);

        m_offset += ReadSize;
        return value;
    }

    /// Read big-endian unsigned 32-bit
    inline uint32_t ReadUInt32BE()
    {
        constexpr std::size_t ReadSize = 4;
        assert(m_offset + ReadSize <= m_availableSize);

        uint32_t value = (uint32_t(m_ptr[m_offset]) << 24) |
                         (uint32_t(m_ptr[m_offset + 1]) << 16) |
                         (uint32_t(m_ptr[m_offset + 2]) <<  8) |
                          uint32_t(m_ptr[m_offset + 3]);

        m_offset += ReadSize;
        return value;
    }

    /// Read little-endian unsigned 32-bit (named ReadInt32LE per original)
    inline uint32_t ReadInt32LE()
    {
        constexpr std::size_t ReadSize = 4;
        assert(m_offset + ReadSize <= m_availableSize);

        uint32_t value = uint32_t(m_ptr[m_offset])       |
                         (uint32_t(m_ptr[m_offset + 1]) <<  8) |
                         (uint32_t(m_ptr[m_offset + 2]) << 16) |
                         (uint32_t(m_ptr[m_offset + 3]) << 24);

        m_offset += ReadSize;
        return value;
    }

    /// Read big-endian unsigned 40-bit
    inline uint64_t ReadUInt40BE()
    {
        constexpr std::size_t ReadSize = 5;
        assert(m_offset + ReadSize <= m_availableSize);

        uint64_t value = (uint64_t(m_ptr[m_offset]) << 32) |
                         (uint64_t(m_ptr[m_offset + 1]) << 24) |
                         (uint64_t(m_ptr[m_offset + 2]) << 16) |
                         (uint64_t(m_ptr[m_offset + 3]) <<  8) |
                          uint64_t(m_ptr[m_offset + 4]);

        m_offset += ReadSize;
        return value;
    }

    /// Read little-endian unsigned 64-bit
    inline uint64_t ReadUInt64LE()
    {
        constexpr std::size_t ReadSize = 8;
        assert(m_offset + ReadSize <= m_availableSize);

        uint64_t value =
            (uint64_t(m_ptr[m_offset])      ) |
            (uint64_t(m_ptr[m_offset + 1]) <<  8) |
            (uint64_t(m_ptr[m_offset + 2]) << 16) |
            (uint64_t(m_ptr[m_offset + 3]) << 24) |
            (uint64_t(m_ptr[m_offset + 4]) << 32) |
            (uint64_t(m_ptr[m_offset + 5]) << 40) |
            (uint64_t(m_ptr[m_offset + 6]) << 48) |
            (uint64_t(m_ptr[m_offset + 7]) << 56);

        m_offset += ReadSize;
        return value;
    }

    /// Read a NUL-terminated string from the current offset,
    /// advance past the NUL, and return the string (excluding terminator)
    inline std::string ReadNullTermString()
    {
        // Must have at least one byte remaining to read
        assert(m_offset < m_availableSize);

        const char* start   = reinterpret_cast<const char*>(m_ptr + m_offset);
        std::size_t maxLen  = m_availableSize - m_offset;

        // Find length up to the next NUL (within bounds)
    #ifdef _MSC_VER
        std::size_t len = strnlen_s(start, maxLen);
    #else
        std::size_t len = strnlen(start, maxLen);
    #endif
        // Ensure we actually found a terminator within maxLen
        assert(len < maxLen);

        std::string result(start, len);
        // Advance past the string + the terminator byte
        m_offset += (len + 1);
        return result;
    }

    inline std::vector<uint8_t> ReadUint8Array(const std::size_t size) {
        assert(m_offset + size <= m_availableSize);

        m_offset += size;

        return std::vector<uint8_t>(m_ptr + m_offset, m_ptr+m_offset + size);
    }
};
#endif //DATAREADER_H
