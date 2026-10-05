#ifndef XYLEM_XBDIFF_HPP
#define XYLEM_XBDIFF_HPP

#include <Ksee/Array.hpp>
#include <Ksee/String.hpp>

namespace Xylem {

using namespace Ksee;

class BlobStore;

class XBDiff {
public:
    struct Segment {
        enum Type {
            COPY = 1,
            LITERAL = 2,
            HASH = 3
        };
        Type type;
        usz length;
        
        // For COPY
        usz sourceOffset = 0;
        
        // For LITERAL
        String literalData;
        
        // For HASH
        String hash;
        usz hashOffset = 0;
    };

    String baseContent;
    Array<Segment> segments;
    Array<String> hashesInserted;
    BlobStore* blobStore = nullptr;

    XBDiff();
    explicit XBDiff(const String& baseVal);

    // Proxy for array-like writing: df[index] = byte
    struct ByteRef {
        XBDiff& diff;
        usz index;
        operator u8() const;
        ByteRef& operator=(u8 val);
    };

    ByteRef operator[](usz index);
    u8 operator[](usz index) const;

    u8 getByte(usz index) const;
    void setByte(usz index, u8 val);

    usz size() const;

    void insertHash(usz index, const String& hash);
    void set(usz position, const String& array);
    void splice(usz start, usz deleteCount, const String& insertData = String());

    String toBinary() const;
    String toString() const;
    String toBinaryContent(BlobStore* bs = nullptr) const;

    static XBDiff create(const String& oldData, const String& newData);
    static XBDiff fromBinary(const String& bin, const String& baseVal = String());
};

} // namespace Xylem

#endif // XYLEM_XBDIFF_HPP
