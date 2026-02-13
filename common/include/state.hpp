#pragma once

#include <iosfwd>
#include <array>
#include <vector>
#include <types.hpp>

namespace state {

template<typename T>
void Write(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template<typename T, Size N>
void Write(std::ostream& out, const std::array<T, N>& arr) {
    out.write(reinterpret_cast<const char*>(arr.data()), N * sizeof(T));
}

inline void Write(std::ostream& out, const std::vector<U8>& vec) {
    U32 size = static_cast<U32>(vec.size());
    Write(out, size);
    if (size > 0)
        out.write(reinterpret_cast<const char*>(vec.data()), size);
}

template<typename T>
void Read(std::istream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
}

template<typename T, Size N>
void Read(std::istream& in, std::array<T, N>& arr) {
    in.read(reinterpret_cast<char*>(arr.data()), N * sizeof(T));
}

inline void Read(std::istream& in, std::vector<U8>& vec) {
    U32 size = 0;
    Read(in, size);
    vec.resize(size);
    if (size > 0)
        in.read(reinterpret_cast<char*>(vec.data()), size);
}

constexpr U32 Magic = 0x53534247;  // "GBSS"
constexpr U8 Version = 3;

} // namespace state
