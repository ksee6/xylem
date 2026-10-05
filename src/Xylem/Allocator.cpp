#include <Xylem/Allocator.hpp>
#include <algorithm>

namespace Xylem {

Allocator::Allocator(BlockDevice* dev) : device(dev) {}

void Allocator::initFromFormat(u32 blockCount) {
    bam.allocate(blockCount);
    for (u32 i = 0; i < blockCount; ++i) {
        bam[i].eraseCount = 0;
        bam[i].setStatus(BlockStatus::FREE);
        bam[i].setType(BlockType::FREE);
    }
    buildHeap();
}

void Allocator::buildHeap() {
    freeHeap = Array<AllocHeapEntry>();
    for (u32 i = 0; i < bam.size(); ++i) {
        if (bam[i].getStatus() == BlockStatus::FREE) {
            pushHeap({bam[i].eraseCount, i});
        }
    }
}

static constexpr u32 BAM_MAGIC_V2 = 0x42414D32; // "BAM2"

// Dual-generation crash-consistent BAM format:
//   [4B] magic (0x42414D32)
//   [4B] generation (u32)
//   [4B] blockIndex (u32)
//   [4B] count of entries (u32)
//   [count * 3B] entries (u16 eraseCount LE + u8 packed)
//   [4B] CRC32 of all preceding bytes
void Allocator::saveBam() {
    if (!device || !device->config.onDeviceWrite) return;

    u32 blockSize = device->config.blockSize;
    u32 entriesPerBlock = (blockSize > 20) ? (blockSize - 20) / 3 : 1;
    usz totalEntries = bam.size();

    u32 targetBank = 0;
    u32 targetStart = bamStartBlock;
    u32 nextGen = currentGeneration + 1;

    // Check if device has space for dual-bank shadow BAM
    if (bamStartBlock + bamBlockCount * 2 <= bam.size()) {
        targetBank = (activeBank == 0) ? 1 : 0;
        targetStart = (targetBank == 0) ? bamStartBlock : (bamStartBlock + bamBlockCount);
    }

    bool allSuccess = true;
    for (u32 b = 0; b < bamBlockCount; ++b) {
        usz start = (usz)b * entriesPerBlock;
        if (start >= totalEntries) break;

        usz end = start + entriesPerBlock;
        if (end > totalEntries) end = totalEntries;
        u32 count = (u32)(end - start);

        String data; data.allocate(blockSize);
        data.fill(0xFF);
        u8* ptr = (u8*)data.data();
        u8* blockStart = ptr;

        *(u32*)ptr = BAM_MAGIC_V2; ptr += 4;
        *(u32*)ptr = nextGen;      ptr += 4;
        *(u32*)ptr = b;            ptr += 4;
        *(u32*)ptr = count;        ptr += 4;

        for (usz i = start; i < end; ++i) {
            *(u16*)ptr = bam[i].eraseCount; ptr += 2;
            *ptr++ = bam[i].packed;
        }

        u32 payloadLen = (u32)(ptr - blockStart);
        u32 c = crc32(blockStart, payloadLen);
        *(u32*)ptr = c; ptr += 4;

        u32 blockDataLen = (u32)(ptr - blockStart);
        if (!device->writeBlock(targetStart + b, 0, data.slice(0, blockDataLen))) {
            allSuccess = false;
            break;
        }
    }

    if (allSuccess) {
        activeBank = targetBank;
        currentGeneration = nextGen;
    }
}

bool Allocator::loadBam() {
    if (!device || !device->config.onDeviceRead) return false;

    u32 blockSize = device->config.blockSize;
    usz totalEntries = bam.size();

    auto tryLoadV2Bank = [&](u32 bankStart, u32& outGen) -> bool {
        u32 entriesPerBlock = (blockSize > 20) ? (blockSize - 20) / 3 : 1;
        u32 expectedGen = 0;
        usz entryIdx = 0;

        for (u32 b = 0; b < bamBlockCount && entryIdx < totalEntries; ++b) {
            String data = device->readBlock(bankStart + b, 0);
            if ((u32)data.size() < 20) return false;

            const u8* blockStart = (const u8*)data.data();
            const u8* ptr = blockStart;

            u32 magic = *(const u32*)ptr; ptr += 4;
            if (magic != BAM_MAGIC_V2) return false;

            u32 gen = *(const u32*)ptr; ptr += 4;
            if (b == 0) expectedGen = gen;
            else if (gen != expectedGen) return false;

            u32 bIdx = *(const u32*)ptr; ptr += 4;
            if (bIdx != b) return false;

            u32 count = *(const u32*)ptr; ptr += 4;
            if (count == 0 || count > entriesPerBlock) return false;

            u32 payloadLen = 16 + count * 3;
            if (payloadLen + 4 > (u32)data.size()) return false;

            u32 storedCrc   = *(const u32*)(blockStart + payloadLen);
            u32 computedCrc = crc32(blockStart, payloadLen);
            if (storedCrc != computedCrc) return false;

            entryIdx += count;
        }
        outGen = expectedGen;
        return entryIdx > 0;
    };

    auto applyV2Bank = [&](u32 bankStart) {
        u32 entriesPerBlock = (blockSize > 20) ? (blockSize - 20) / 3 : 1;
        usz entryIdx = 0;
        for (u32 b = 0; b < bamBlockCount && entryIdx < totalEntries; ++b) {
            String data = device->readBlock(bankStart + b, 0);
            const u8* ptr = (const u8*)data.data() + 16; // Skip header
            u32 count = *(const u32*)((const u8*)data.data() + 12);
            for (u32 i = 0; i < count && entryIdx < totalEntries; ++i, ++entryIdx) {
                bam[entryIdx].eraseCount = *(const u16*)ptr; ptr += 2;
                bam[entryIdx].packed     = *ptr++;
            }
        }
    };

    // Try Bank 0
    u32 gen0 = 0, gen1 = 0;
    bool bank0Valid = tryLoadV2Bank(bamStartBlock, gen0);

    // Try Bank 1 (if within bounds)
    bool bank1Valid = false;
    if (bamStartBlock + bamBlockCount * 2 <= totalEntries) {
        bank1Valid = tryLoadV2Bank(bamStartBlock + bamBlockCount, gen1);
    }

    if (bank0Valid && bank1Valid) {
        if (gen1 > gen0) {
            applyV2Bank(bamStartBlock + bamBlockCount);
            activeBank = 1;
            currentGeneration = gen1;
        } else {
            applyV2Bank(bamStartBlock);
            activeBank = 0;
            currentGeneration = gen0;
        }
        return true;
    } else if (bank0Valid) {
        applyV2Bank(bamStartBlock);
        activeBank = 0;
        currentGeneration = gen0;
        return true;
    } else if (bank1Valid) {
        applyV2Bank(bamStartBlock + bamBlockCount);
        activeBank = 1;
        currentGeneration = gen1;
        return true;
    }

    // Fall back to legacy V1 format (Bank 0)
    u32 entriesPerBlock = (blockSize > 8) ? (blockSize - 8) / 3 : 1;
    usz entryIdx = 0;

    for (u32 b = 0; b < bamBlockCount && entryIdx < totalEntries; ++b) {
        String data = device->readBlock(bamStartBlock + b, 0);
        if ((u32)data.size() < 8) { return false; }

        const u8* blockStart = (const u8*)data.data();
        const u8* ptr = blockStart;
        u32 count = *(const u32*)ptr; ptr += 4;
        if (count == 0 || count > entriesPerBlock) { return false; }

        u32 payloadLen = 4 + count * 3;
        if (payloadLen + 4 > (u32)data.size()) { return false; }

        u32 storedCrc   = *(const u32*)(blockStart + payloadLen);
        u32 computedCrc = crc32(blockStart, payloadLen);
        if (storedCrc != computedCrc) { return false; }

        for (u32 i = 0; i < count && entryIdx < totalEntries; ++i, ++entryIdx) {
            bam[entryIdx].eraseCount = *(const u16*)ptr; ptr += 2;
            bam[entryIdx].packed     = *ptr++;
        }
    }

    activeBank = 0;
    currentGeneration = 1;
    return entryIdx > 0;
}

void Allocator::pushHeap(AllocHeapEntry entry) {
    freeHeap.push(entry);
    usz idx = freeHeap.size() - 1;
    while (idx > 0) {
        usz p = (idx - 1) / 2;
        if (freeHeap[idx] < freeHeap[p]) {
            auto tmp = freeHeap[idx];
            freeHeap[idx] = freeHeap[p];
            freeHeap[p] = tmp;
            idx = p;
        } else {
            break;
        }
    }
}

AllocHeapEntry Allocator::popHeap() {
    if (freeHeap.size() == 0) return {0, 0};
    
    AllocHeapEntry minEntry = freeHeap[0];
    freeHeap[0] = freeHeap[freeHeap.size() - 1];
    freeHeap.pop();

    usz idx = 0;
    while (true) {
        usz left  = 2 * idx + 1;
        usz right = 2 * idx + 2;
        usz smallest = idx;

        if (left  < freeHeap.size() && freeHeap[left]  < freeHeap[smallest]) smallest = left;
        if (right < freeHeap.size() && freeHeap[right] < freeHeap[smallest]) smallest = right;
        
        if (smallest != idx) {
            auto tmp = freeHeap[idx];
            freeHeap[idx] = freeHeap[smallest];
            freeHeap[smallest] = tmp;
            idx = smallest;
        } else {
            break;
        }
    }
    return minEntry;
}

u32 Allocator::allocBlock(BlockType type) {
    while (true) {
        if (freeHeap.size() == 0) {
            if (device->config.deviceExpands) {
                u32 oldSize = bam.size();
                u32 newSize = oldSize + 1024;
                u32 entriesPerBlock = device->config.blockSize / sizeof(BlockMeta);
                if (newSize > bamBlockCount * entriesPerBlock) {
                    return 0; // BAM capacity reached, cannot expand further seamlessly
                }
                device->config.deviceSize = newSize * (u64)device->config.blockSize;
                bam.allocate(newSize);
                for (u32 i = oldSize; i < newSize; ++i) {
                    bam[i].setStatus(BlockStatus::FREE);
                    bam[i].setType(BlockType::FREE);
                    bam[i].eraseCount = 0;
                    AllocHeapEntry e; e.blockIdx = i; e.eraseCount = 0;
                    pushHeap(e);
                }
                continue;
            }
            return 0;
        }
        
        AllocHeapEntry entry = popHeap();
        
        if (bam[entry.blockIdx].getStatus() != BlockStatus::FREE) {
            continue;
        }
        
        if (device->config.blockCycles > 0 && entry.eraseCount >= device->config.blockCycles) {
            bam[entry.blockIdx].setStatus(BlockStatus::BAD);
            continue;
        }
        
        bam[entry.blockIdx].setStatus(BlockStatus::USED);
        bam[entry.blockIdx].setType(type);
        return entry.blockIdx;
    }
}

bool Allocator::freeBlock(u32 blockIdx) {
    if (blockIdx >= bam.size()) return false;
    // Allow freeing USED or RESERVED (for cleanup during rollback)
    BlockStatus st = bam[blockIdx].getStatus();
    if (st != BlockStatus::USED && st != BlockStatus::RESERVED) return false;

    if (!device->eraseBlock(blockIdx)) {
        bam[blockIdx].setStatus(BlockStatus::BAD);
        return false;
    }
    
    if (device->config.blockCycles > 0 || device->config.blockErase) {
        bam[blockIdx].eraseCount++;
    }

    bam[blockIdx].setStatus(BlockStatus::FREE);
    bam[blockIdx].setType(BlockType::FREE);
    pushHeap({bam[blockIdx].eraseCount, blockIdx});
    return true;
}

void Allocator::wearLevel() {
    // Min-heap already provides passive wear leveling.
    // Active relocation of cold data is deferred to a future GC pass.
}

} // namespace Xylem
