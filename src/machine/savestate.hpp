#pragma once
#include "../core/types.hpp"
#include <vector>
#include <cstring>
#include <type_traits>

// Sequential binary buffer used for save states. Components write their
// state field-by-field and read it back in the same order; Read returns
// false past the end so truncated files fail cleanly.
class StateBuffer {
public:
    template<typename T>
    void Write(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        WriteBytes(&value, sizeof(T));
    }

    void WriteBytes(const void* data, size_t size) {
        const u8* bytes = static_cast<const u8*>(data);
        m_data.insert(m_data.end(), bytes, bytes + size);
    }

    template<typename T>
    bool Read(T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        return ReadBytes(&value, sizeof(T));
    }

    bool ReadBytes(void* data, size_t size) {
        if (m_pos + size > m_data.size()) {
            return false;
        }
        std::memcpy(data, m_data.data() + m_pos, size);
        m_pos += size;
        return true;
    }

    const std::vector<u8>& Data() const { return m_data; }
    void SetData(std::vector<u8> data) {
        m_data = std::move(data);
        m_pos = 0;
    }

private:
    std::vector<u8> m_data;
    size_t m_pos = 0;
};
