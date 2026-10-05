#ifndef XYLEM_CACHE_HPP
#define XYLEM_CACHE_HPP

#include <Xylem/Format.hpp>
#include <Xylem/BlockDevice.hpp>
#include <Ksee/Map.hpp>
#include <Ksee/Array.hpp>

namespace Xylem {

using namespace Ksee;

struct CacheEntry {
    u32 blockIdx;
    Array<u8> decompressed;
    bool dirty;
    u64 accessSeq;
};

class Cache {
public:
    BlockDevice* device;
    usz maxCacheSize;
    usz currentUsedBytes;
    u64 accessCounter;
    
    Map<u32, CacheEntry> entries;

    Cache(BlockDevice* dev, usz maxCache);

    Array<u8> get(u32 blockIdx);
    void put(u32 blockIdx, const Array<u8>& data);
    void flushAll();

private:
    void evict();
};

} // namespace Xylem

#endif // XYLEM_CACHE_HPP
