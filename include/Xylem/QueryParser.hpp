#ifndef XYLEM_QUERYPARSER_HPP
#define XYLEM_QUERYPARSER_HPP

#include <Xylem/Format.hpp>
#include <Xylem/Query.hpp>
#include <Ksee/Array.hpp>
#include <Ksee/Map.hpp>
#include <Ksee/String.hpp>
#include <Ksee/Tree.hpp>

namespace Xylem {

using namespace Ksee;

class XylemEngine; // Forward declaration

struct QueryResult {
    int code = -1;
    Tree<void>* treeResult = nullptr;
    Array<Map<String, String>> readRows;

    String getRowsJson() const;
};

class QueryParser {
public:
    static QueryResult execute(XylemEngine* engine, const String& query, const Array<String>& args = Array<String>(), u64 now = 0);

    // Visible for testing or internal composition
    static Array<String> tokenize(const String& query, const Array<String>& args);

};

} // namespace Xylem

#endif // XYLEM_QUERYPARSER_HPP
