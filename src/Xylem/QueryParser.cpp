#include <Xylem/QueryParser.hpp>
#include <Xylem/Xylem.hpp>
#include <cstdlib>

using namespace Ksee;

namespace Xylem {

Array<String> QueryParser::tokenize(const String& query, const Array<String>& args) {
    Array<String> tokens;
    String current;
    bool inQuotes = false;
    char quoteChar = 0;
    bool escape = false;

    for (usz i = 0; i < query.size(); ++i) {
        char c = query[i];

        if (escape) {
            current += c;
            escape = false;
            continue;
        }

        if (c == '\\') {
            escape = true;
            continue;
        }

        if (!inQuotes && (c == '\'' || c == '"')) {
            inQuotes = true;
            quoteChar = c;
            continue;
        }

        if (inQuotes && c == quoteChar) {
            inQuotes = false;
            tokens.push(current);
            current = "";
            continue;
        }

        if (inQuotes) {
            current += c;
            continue;
        }

        // Comments
        if (c == '#') {
            while (i < query.size() && query[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < query.size() && query[i + 1] == '/') {
            while (i < query.size() && query[i] != '\n') ++i;
            continue;
        }

        // Arguments
        if (c == '%' && i + 1 < query.size() && query[i + 1] >= '1' && query[i + 1] <= '9') {
            int argIdx = query[i + 1] - '1';
            if (argIdx >= 0 && (usz)argIdx < args.size()) {
                current += args[argIdx];
            }
            ++i; // skip digit
            continue;
        }

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!current.isEmpty()) {
                tokens.push(current);
                current = "";
            }
            continue;
        }

        current += c;
    }

    if (!current.isEmpty()) {
        tokens.push(current);
    }

    return tokens;
}

static Clause parseClauseStr(const String& str) {
    Clause c;
    c.op = "=";
    
    // Check if the query starts with "blake" prefix operator, e.g. "blake doc_id == expected_hash"
    // which compiles to col = "blake:16:doc_id", op = "==", val = "expected_hash"
    // Or with slice format: "blake[:8] doc_id == expected_hash" -> col = "blake:8:doc_id"
    if (str.startsWith("blake")) {
        long long spaceIdx = str.indexOf(" ");
        if (spaceIdx > 0) {
            String blakeSpec = str.slice(0, spaceIdx);
            String rest = str.slice(spaceIdx + 1);
            
            // Extract bits (default 16 bytes = 128 bits)
            u64 bits = 16;
            if (blakeSpec.startsWith("blake[:") && blakeSpec.endsWith("]")) {
                String bitStr = blakeSpec.slice(7, blakeSpec.size() - 1);
                bits = (u64)strtoull((const char*)bitStr.data(), nullptr, 10);
            }
            
            Clause sub = parseClauseStr(rest);
            c.col = "blake:" + String::from(bits) + ":" + sub.col;
            c.op = sub.op;
            c.val = sub.val;
            return c;
        }
    }

    // Look for operators
    Array<String> ops;
    ops.push("==="); ops.push("!==");
    ops.push("=="); ops.push("<="); ops.push(">="); ops.push("reg"); ops.push("!=");
    ops.push("cos"); ops.push("hash"); ops.push("path"); ops.push("empty");
    ops.push("!has"); ops.push("has");
    ops.push("="); ops.push("<"); ops.push(">");
    for (const String& op : ops) {
        long long pos = str.indexOf(op);
        if (pos >= 0) {
            c.col = str.slice(0, pos);
            c.op = op;
            c.val = str.slice(pos + op.size(), str.size());
            return c;
        }
    }
    // Default fallback
    c.col = str;
    c.val = "";
    return c;
}


static void parseClausesFromTokens(const Array<String>& tokens, usz& idx, Array<Clauses>& queryClauses) {
    Clauses currentGroup;
    bool inWhere = false;
    bool inAssert = false;
    bool inFollow = false;
    bool inRepeatFollow = false;
    bool nextIsOr = false;
    
    while (idx < tokens.size()) {
        String t = tokens[idx];
        String tu = t.toUpperCase();
        
        if (tu == "LIMIT" || tu == "PAGE" || tu == "SET" || tu == "REMOVE") {
            break;
        }
        
        if (tu == "WHERE" || tu == "MATCH") {
            if (currentGroup.size() > 0) {
                currentGroup.isAssert = inAssert;
                currentGroup.isFollow = inFollow;
                currentGroup.isRepeatFollow = inRepeatFollow;
                currentGroup.isOrConnection = nextIsOr;
                queryClauses.push(currentGroup);
                currentGroup.clear();
                nextIsOr = false;
            }
            inWhere = true;
            inAssert = false;
            inFollow = false;
            inRepeatFollow = false;
        } else if (tu == "ASSERT") {
            if (currentGroup.size() > 0) {
                currentGroup.isAssert = inAssert;
                currentGroup.isFollow = inFollow;
                currentGroup.isRepeatFollow = inRepeatFollow;
                currentGroup.isOrConnection = nextIsOr;
                queryClauses.push(currentGroup);
                currentGroup.clear();
                nextIsOr = false;
            }
            inWhere = false;
            inAssert = true;
            inFollow = false;
            inRepeatFollow = false;
        } else if (tu == "FOLLOW") {
            if (currentGroup.size() > 0) {
                currentGroup.isAssert = inAssert;
                currentGroup.isFollow = inFollow;
                currentGroup.isRepeatFollow = inRepeatFollow;
                currentGroup.isOrConnection = nextIsOr;
                queryClauses.push(currentGroup);
                currentGroup.clear();
                nextIsOr = false;
            }
            inWhere = false;
            inAssert = false;
            inFollow = true;
            inRepeatFollow = false;
            if (idx + 1 < tokens.size() && tokens[idx+1].toUpperCase() == "WHERE") {
                idx++;
            }
        } else if (tu == "REPEATFOLLOW") {
            if (currentGroup.size() > 0) {
                currentGroup.isAssert = inAssert;
                currentGroup.isFollow = inFollow;
                currentGroup.isRepeatFollow = inRepeatFollow;
                currentGroup.isOrConnection = nextIsOr;
                queryClauses.push(currentGroup);
                currentGroup.clear();
                nextIsOr = false;
            }
            inWhere = false;
            inAssert = false;
            inFollow = false;
            inRepeatFollow = true;
            if (idx + 1 < tokens.size() && tokens[idx+1].toUpperCase() == "WHERE") {
                idx++;
            }
        } else if (tu == "OR") {
            bool hasNextKeyword = false;
            String nextTu;
            if (idx + 1 < tokens.size()) {
                nextTu = tokens[idx+1].toUpperCase();
                if (nextTu == "WHERE" || nextTu == "ASSERT" || nextTu == "FOLLOW" || nextTu == "REPEATFOLLOW") {
                    hasNextKeyword = true;
                }
            }
            
            if (hasNextKeyword) {
                if (currentGroup.size() > 0) {
                    currentGroup.isAssert = inAssert;
                    currentGroup.isFollow = inFollow;
                    currentGroup.isRepeatFollow = inRepeatFollow;
                    currentGroup.isOrConnection = nextIsOr;
                    queryClauses.push(currentGroup);
                    currentGroup.clear();
                }
                inWhere = (nextTu == "WHERE");
                inAssert = (nextTu == "ASSERT");
                inFollow = (nextTu == "FOLLOW");
                inRepeatFollow = (nextTu == "REPEATFOLLOW");
                nextIsOr = true;
                idx++;
                if (nextTu == "FOLLOW" || nextTu == "REPEATFOLLOW") {
                    if (idx + 1 < tokens.size() && tokens[idx+1].toUpperCase() == "WHERE") {
                        idx++;
                    }
                }
            } else {
                if (currentGroup.size() > 0) {
                    currentGroup.isAssert = inAssert;
                    currentGroup.isFollow = inFollow;
                    currentGroup.isRepeatFollow = inRepeatFollow;
                    currentGroup.isOrConnection = nextIsOr;
                    queryClauses.push(currentGroup);
                    currentGroup.clear();
                }
                nextIsOr = true;
            }
        } else {
            if (inWhere || inAssert || inFollow || inRepeatFollow) {
                bool isSpecialOp = false;
                if (idx + 1 < tokens.size() && tokens[idx+1].toUpperCase() == "EMPTY") {
                    Clause c;
                    c.col = tokens[idx];
                    c.op = "empty";
                    c.val = "";
                    currentGroup.push(c);
                    idx += 1;
                    isSpecialOp = true;
                } else if (idx + 2 < tokens.size()) {
                    String op = tokens[idx+1];
                    if (op == "===" || op == "!==" ||
                        op == "==" || op == "<=" || op == ">=" || op == "reg" ||
                        op == "cos" || op == "hash" || op == "path" ||
                        op == "!has" || op == "has" ||
                        op == "=" || op == "<" || op == ">") {
                        Clause c;
                        c.col = tokens[idx];
                        c.op = op;
                        c.val = tokens[idx+2];
                        currentGroup.push(c);
                        idx += 2;
                        isSpecialOp = true;
                    }
                }

                if (!isSpecialOp) {
                    currentGroup.push(parseClauseStr(t));
                }
            } else {
                break;
            }
        }
        idx++;
    }
    
    if (currentGroup.size() > 0) {
        currentGroup.isAssert = inAssert;
        currentGroup.isFollow = inFollow;
        currentGroup.isRepeatFollow = inRepeatFollow;
        currentGroup.isOrConnection = nextIsOr;
        queryClauses.push(currentGroup);
    }
}

QueryResult QueryParser::execute(XylemEngine* engine, const String& queryStr, const Array<String>& args, u64 now) {
    QueryResult res;
    Array<String> tokens = tokenize(queryStr, args);
    if (tokens.size() == 0) return res;

    String cmd = tokens[0].toUpperCase();

    if (cmd == "FORMAT") {
        res.code = engine->format() ? 0 : -1;
        return res;
    }
    if (cmd == "MOUNT") {
        res.code = engine->mount() ? 0 : -1;
        return res;
    }
    if (cmd == "DESTROY") {
        engine->destroy();
        res.code = 0;
        return res;
    }
    if (cmd == "MEMORY" && tokens.size() > 1) {
        String sizeStr = tokens[1];
        usz size = 0;
        if (sizeStr.endsWith("MB")) {
            size = (usz)std::atoll((const char*)sizeStr.slice(0, sizeStr.size() - 2).data()) * 1024 * 1024;
        } else if (sizeStr.endsWith("KB")) {
            size = (usz)std::atoll((const char*)sizeStr.slice(0, sizeStr.size() - 2).data()) * 1024;
        } else {
            size = (usz)std::atoll((const char*)sizeStr.data());
        }
        engine->maxCache = size;
        if (engine->tableStore) engine->tableStore->maxMemoryBytes = size;
        res.code = 0;
        return res;
    }

    // Parse range spec from command: CMD[x:y] or CMD[:y] or CMD[x:]
    auto parseCmdRange = [](const String& cmdStr) -> BlobRange {
        BlobRange r = {0, 0, false, false, false};
        long long bracket = -1;
        for (usz i = 0; i < cmdStr.size(); ++i) {
            if (cmdStr[i] == '[') { bracket = (long long)i; break; }
        }
        if (bracket < 0) return r;
        String rangeSpec = cmdStr.slice(bracket);
        return parseBlobRange(rangeSpec);
    };

    // Extract base command name without range spec
    auto baseCmdName = [](const String& cmdStr) -> String {
        for (usz i = 0; i < cmdStr.size(); ++i) {
            if (cmdStr[i] == '[') return cmdStr.slice(0, i);
        }
        return cmdStr;
    };

    // Pre-processing query-level prefixes/modifiers:
    // trashed/removed, decrypt, encrypt
    bool viewTombstones = false;
    Array<String> queryDecryptPool;
    String queryEncryptKey;
    
    usz tokenIdx = 0;
    while (tokenIdx < tokens.size()) {
        String t = tokens[tokenIdx];
        String tu = t.toUpperCase();
        if (tu == "TRASHED" || tu == "REMOVED") {
            viewTombstones = true;
            tokenIdx++;
        } else if (tu == "DECRYPT" && tokenIdx + 1 < tokens.size()) {
            // decrypt base64/raw support, comma-separated keys
            String keysList = tokens[tokenIdx + 1];
            Array<String> keys = keysList.split(",");
            for (usz i = 0; i < keys.size(); ++i) {
                if (!keys[i].isEmpty()) {
                    engine->autoDecrypt.push(keys[i]);
                    queryDecryptPool.push(keys[i]);
                }
            }
            tokenIdx += 2;
        } else if (tu == "ENCRYPT" && tokenIdx + 1 < tokens.size()) {
            queryEncryptKey = tokens[tokenIdx + 1];
            tokenIdx += 2;
        } else {
            break;
        }
    }

    if (tokenIdx >= tokens.size()) return res;
    
    // ─── Query Token Rewriting for Action-Later syntax ───
    String actionCmd;
    usz cmdTokenIdx = tokens.size();
    for (usz i = tokenIdx; i < tokens.size(); ++i) {
        String base = baseCmdName(tokens[i]).toUpperCase();
        if (base == "READ" || base == "READ*" || base == "WRITE" || base == "WRITEVOLATILE" || base == "APPEND" || base == "RM" || base == "BURN" ||
            base == "CAT" || base == "TEE" || base == "LS" || base == "VACCUM" || base == "FREEZE" || base == "THAW" ||
            base == "LOCK" || base == "UNLOCK" || base == "COMMIT" || base == "ROLLBACK" || base == "UNWATCH" || base == "PULL" || base == "WATCH") {
            actionCmd = tokens[i];
            cmdTokenIdx = i;
            break;
        }
    }

    if (cmdTokenIdx < tokens.size() && cmdTokenIdx > tokenIdx) {
        Array<String> prefixTokens;
        for (usz i = 0; i < tokenIdx; ++i) {
            prefixTokens.push(tokens[i]);
        }
        
        Array<String> clauseBeforeTokens;
        for (usz i = tokenIdx; i < cmdTokenIdx; ++i) {
            clauseBeforeTokens.push(tokens[i]);
        }
        
        Array<String> paramTokens;
        for (usz i = cmdTokenIdx + 1; i < tokens.size(); ++i) {
            paramTokens.push(tokens[i]);
        }
        
        Array<String> paramCols;
        Array<String> paramMods;
        bool inMods = false;
        for (usz i = 0; i < paramTokens.size(); ++i) {
            String tu = paramTokens[i].toUpperCase();
            if (tu == "LIMIT" || tu == "PAGE") {
                inMods = true;
            }
            if (inMods) {
                paramMods.push(paramTokens[i]);
            } else {
                paramCols.push(paramTokens[i]);
            }
        }
        
        Array<String> newTokens;
        for (usz i = 0; i < prefixTokens.size(); ++i) {
            newTokens.push(prefixTokens[i]);
        }
        newTokens.push(actionCmd);
        for (usz i = 0; i < paramCols.size(); ++i) {
            newTokens.push(paramCols[i]);
        }
        newTokens.push("WHERE");
        for (usz i = 0; i < clauseBeforeTokens.size(); ++i) {
            newTokens.push(clauseBeforeTokens[i]);
        }
        for (usz i = 0; i < paramMods.size(); ++i) {
            newTokens.push(paramMods[i]);
        }
        
        tokens = newTokens;
    }
    
    // Shift command
    cmd = tokens[tokenIdx].toUpperCase();
    BlobRange cmdRange = parseCmdRange(tokens[tokenIdx]);
    String baseCmd = baseCmdName(tokens[tokenIdx]).toUpperCase();

    // ─── REMOVE / TRASH command ───
    if (baseCmd == "REMOVE" || baseCmd == "TRASH") {
        // e.g. REMOVE (tombstone) or REMOVE <date>
        // TRASH or TRASH <date>
        // Finds target rows, sets ::remove or ::trash to either '1' or microsecond date
        String expiryVal = "1";
        usz nextIdx = tokenIdx + 1;
        if (nextIdx < tokens.size()) {
            String nextToken = tokens[nextIdx];
            String nextTokenU = nextToken.toUpperCase();
            if (nextTokenU != "WHERE" && nextTokenU != "MATCH" && nextTokenU != "ASSERT" && nextTokenU != "LIMIT" && nextTokenU != "PAGE") {
                expiryVal = nextToken;
                nextIdx++;
            }
        }
        
        Array<Clauses> queryClauses;
        parseClausesFromTokens(tokens, nextIdx, queryClauses);
        
        Array<Clause> writeCols;
        Clause c;
        c.col = (baseCmd == "REMOVE") ? "::remove" : "::trash";
        c.op = "=";
        c.val = expiryVal;
        writeCols.push(c);
        
        res.code = engine->write(writeCols, queryClauses, 0, queryEncryptKey, now);
        return res;
    }
    if (baseCmd == "CAT") {
        String path = tokens[tokenIdx + 1];
        String cleanPath = path;
        long long bracketPos = -1;
        for (usz i = 0; i < path.size(); ++i) {
            if (path[i] == '[') { bracketPos = (long long)i; break; }
        }
        if (bracketPos >= 0) {
            cleanPath = path.slice(0, bracketPos);
        }

        Array<String> readCols;
        readCols.push("content");

        Array<Clauses> queryClauses;
        queryClauses.push(WHERE("name", "path", cleanPath));
        
        usz idx = tokenIdx + 2;
        parseClausesFromTokens(tokens, idx, queryClauses);
        
        u64 limitVal = 0;
        u64 pageVal = 0;
        for (usz i = tokenIdx + 2; i < tokens.size(); ++i) {
            String tu = tokens[i].toUpperCase();
            if (tu == "LIMIT" && i + 1 < tokens.size()) limitVal = (u64)strtoull((const char*)tokens[i+1].data(), nullptr, 0);
            if (tu == "PAGE" && i + 1 < tokens.size()) pageVal = (u64)strtoull((const char*)tokens[i+1].data(), nullptr, 0);
        }
        
        auto rows = engine->read(readCols, queryClauses, limitVal, pageVal, viewTombstones, 0, false, now);

        if (rows.size() > 0 && rows[0].has("content")) {
            String content = *rows[0].get("content");
            u64 s = cmdRange.valid ? cmdRange.start : 0;
            u64 e = cmdRange.valid ? (cmdRange.isSingleIndex ? (cmdRange.start + 1) : cmdRange.end) : 0;
            
            long long bracketPos = -1;
            for (usz i = 0; i < path.size(); ++i) {
                if (path[i] == '[') { bracketPos = (long long)i; break; }
            }
            if (bracketPos >= 0) {
                BlobRange br = parseBlobRange(path.slice(bracketPos));
                if (br.valid) {
                    s = br.start;
                    e = br.isSingleIndex ? (br.start + 1) : br.end;
                }
            }
            
            if (s > 0 || e > 0 || cmdRange.valid || bracketPos >= 0) {
                u64 s_idx = s;
                u64 e_idx = (e > 0) ? e : (u64)content.size();
                if (e_idx > (u64)content.size()) e_idx = (u64)content.size();
                if (s_idx < e_idx) content = content.slice((usz)s_idx, (usz)e_idx);
                else content = "";
            }
            Map<String, String> row;
            row.set("content", content);
            res.readRows.push(row);
            res.code = 0;
        } else {
            res.code = -1;
        }
        return res;
    }

    // ─── TEE command ───
    if (baseCmd == "TEE") {
        if (tokens.size() <= tokenIdx + 2) { res.code = -1; return res; }
        String path = tokens[tokenIdx + 1];
        String content = tokens[tokenIdx + 2];
        
        Array<Clauses> queryClauses;
        usz idx = tokenIdx + 3;
        parseClausesFromTokens(tokens, idx, queryClauses);
        
        if (queryClauses.size() > 0) {
            Array<String> idCols; idCols.push("::id");
            auto checkRows = engine->read(idCols, queryClauses, 0, 0, viewTombstones, 0, false, now);
            if (checkRows.size() == 0) {
                res.code = -1;
                return res;
            }
        }
        
        u64 start = cmdRange.valid ? cmdRange.start : 0;
        u64 end   = cmdRange.valid ? (cmdRange.isSingleIndex ? (cmdRange.start + 1) : cmdRange.end) : 0;
        res = engine->tee(path, content, start, end);
        return res;
    }

    // ─── LS command ───
    if (baseCmd == "LS") {
        String path = tokens.size() >= tokenIdx + 2 ? tokens[tokenIdx + 1] : "";
        if (path.toUpperCase() == "WHERE" || path.toUpperCase() == "MATCH" || path.toUpperCase() == "ASSERT" || path.toUpperCase() == "LIMIT" || path.toUpperCase() == "PAGE") {
            path = "";
        }
        
        String cleanPath = path;
        if (cleanPath.endsWith("/") && cleanPath.size() > 1) {
            cleanPath = cleanPath.slice(0, cleanPath.size() - 1);
        }
        
        Array<String> cols;
        cols.push("::id");
        cols.push("name");
        cols.push("::parent");
        cols.push("type");
        cols.push("perms");
        
        Array<Clauses> queryClauses;
        if (cleanPath.isEmpty() || cleanPath == "/") {
            queryClauses.push(WHERE("::parent", "=", "0"));
        } else {
            String queryPath = cleanPath;
            if (!queryPath.startsWith("/")) {
                queryPath = "/" + queryPath;
            }
            queryPath = queryPath + "/*";
            queryClauses.push(WHERE("name", "path", queryPath));
        }
        
        usz idx = (path.isEmpty() ? tokenIdx + 1 : tokenIdx + 2);
        parseClausesFromTokens(tokens, idx, queryClauses);
        
        u64 limitVal = 0;
        u64 pageVal = 0;
        for (usz i = idx; i < tokens.size(); ++i) {
            String tu = tokens[i].toUpperCase();
            if (tu == "LIMIT" && i + 1 < tokens.size()) limitVal = (u64)strtoull((const char*)tokens[i+1].data(), nullptr, 0);
            if (tu == "PAGE" && i + 1 < tokens.size()) pageVal = (u64)strtoull((const char*)tokens[i+1].data(), nullptr, 0);
        }
        
        res.readRows = engine->read(cols, queryClauses, limitVal, pageVal, viewTombstones, 0, false, now);
        res.code = (int)res.readRows.size();
        return res;
    }

    // ─── RM/BURN command ───
    if (baseCmd == "RM" || baseCmd == "BURN") {
        if (tokens.size() <= tokenIdx + 1) { res.code = -1; return res; }
        String firstArg = tokens[tokenIdx + 1];
        
        Array<Clauses> queryClauses;
        usz idx = tokenIdx + 2;
        if (firstArg.startsWith("/") || firstArg.startsWith("./") || firstArg == ".") {
            String normPath = firstArg;
            if (normPath.endsWith("/") && normPath.size() > 1) {
                normPath = normPath.slice(0, normPath.size() - 1);
            }
            queryClauses.push(WHERE("name", "path", normPath + "/**"));
        } else {
            idx = tokenIdx + 1;
        }
        
        parseClausesFromTokens(tokens, idx, queryClauses);
        res.code = engine->rm(queryClauses, 0, 0, baseCmd == "BURN", now) ? 0 : -1;
        return res;
    }

    // ─── CP command ───
    if (baseCmd == "CP") {
        if (tokens.size() <= tokenIdx + 2) { res.code = -1; return res; }
        String src = tokens[tokenIdx + 1];
        String dst = tokens[tokenIdx + 2];
        
        Array<Clauses> queryClauses;
        usz idx = tokenIdx + 3;
        parseClausesFromTokens(tokens, idx, queryClauses);
        
        if (queryClauses.size() > 0) {
            Array<String> idCols; idCols.push("::id");
            auto checkRows = engine->read(idCols, queryClauses, 0, 0, viewTombstones, 0, false, now);
            if (checkRows.size() == 0) {
                res.code = -1;
                return res;
            }
        }
        
        res = engine->cp(src, dst);
        return res;
    }

    // ─── MV command ───
    if (baseCmd == "MV") {
        if (tokens.size() <= tokenIdx + 2) { res.code = -1; return res; }
        String src = tokens[tokenIdx + 1];
        String dst = tokens[tokenIdx + 2];
        
        Array<Clauses> queryClauses;
        usz idx = tokenIdx + 3;
        parseClausesFromTokens(tokens, idx, queryClauses);
        
        if (queryClauses.size() > 0) {
            Array<String> idCols; idCols.push("::id");
            auto checkRows = engine->read(idCols, queryClauses, 0, 0, viewTombstones, 0, false, now);
            if (checkRows.size() == 0) {
                res.code = -1;
                return res;
            }
        }
        
        res = engine->mv(src, dst);
        return res;
    }


    // ─── VACCUM command ───
    if (baseCmd == "VACCUM") {
        if (cmdRange.valid) {
            if (cmdRange.end > 0) {
                res.code = engine->vaccum(cmdRange.start, cmdRange.end) ? 0 : -1;
            } else {
                res.code = engine->vaccum(cmdRange.start) ? 0 : -1;
            }
        } else {
            engine->vaccum();
            res.code = 0;
        }
        return res;
    }

    if (cmd == "FREEZE" || cmd == "THAW") {
        Array<Clauses> queryClauses;
        u64 freezePos = 0;
        usz idx = 1;
        
        if (cmd == "FREEZE") {
            if (idx < tokens.size()) {
                freezePos = (u64)strtoull((const char*)tokens[idx].data(), nullptr, 0);
                idx++;
            }
        }
        
        parseClausesFromTokens(tokens, idx, queryClauses);
        
        Array<String> readCols;
        readCols.push("::id");
        readCols.push("content:blob");
        Array<Map<String, String>> rows = engine->read(readCols, queryClauses);
        u64 affected = 0;
        for (const auto& row : rows) {
            if (row.has("content:blob")) {
                u32 ref = (u32)row.get("content:blob")->toInt();
                if (cmd == "FREEZE") {
                    if (engine->freezeBlob(freezePos, ref)) affected++;
                } else {
                    engine->thawBlob(ref);
                    affected++;
                }
            }
        }
        res.code = affected;
        return res;
    }

    if (cmd == "VACUUM" || cmd == "VACCUM") {
        if (tokens.size() == 1) { engine->vaccum(); res.code = 0; }
        else if (tokens.size() == 2) { engine->vaccum(strtoull((const char*)tokens[1].data(), nullptr, 0)); res.code = 0; }
        else if (tokens.size() == 3) { engine->vaccum(strtoull((const char*)tokens[1].data(), nullptr, 0), strtoull((const char*)tokens[2].data(), nullptr, 0)); res.code = 0; }
        return res;
    }

    if (cmd == "LOCK") {
        bool req = true;
        if (tokens.size() > 2 && tokens[1].toUpperCase() == "AS") {
            req = tokens[2] == "1" || tokens[2].toUpperCase() == "TRUE";
        }
        res.code = engine->lock(Array<Clauses>(), 0, req);
        return res;
    }

    if (cmd == "COMMIT") {
        res.code = engine->commit();
        return res;
    }

    if (cmd == "UNLOCK") {
        if (tokens.size() > 1) res.code = engine->unlock(strtoull((const char*)tokens[1].data(), nullptr, 0));
        return res;
    }

    if (cmd == "ROLLBACK") {
        if (tokens.size() > 1) res.code = engine->rollback(strtoull((const char*)tokens[1].data(), nullptr, 0)) ? 0 : -1;
        return res;
    }

    if (cmd == "UNWATCH") {
        if (tokens.size() > 1) res.code = engine->unwatch(strtoull((const char*)tokens[1].data(), nullptr, 0)) ? 0 : -1;
        return res;
    }

    if (cmd == "PULL") {
        if (tokens.size() > 1) {
            u64 watchId = strtoull((const char*)tokens[1].data(), nullptr, 0);
            res.readRows = engine->pull(watchId);
            res.code = 0;
        } else {
            res.code = -1;
        }
        return res;
    }

    if (cmd == "WATCH") {
        Array<Clauses> queryClauses;
        usz idx = 1;
        parseClausesFromTokens(tokens, idx, queryClauses);
        res.code = engine->watch(queryClauses);
        return res;
    }

    // Standard CRUD (READ, WRITE, WRITEVOLATILE, APPEND, RM, BURN)
    bool isRead = (cmd == "READ" || cmd == "READ*");
    if (isRead || cmd == "WRITE" || cmd == "WRITEVOLATILE" || cmd == "APPEND" || cmd == "RM" || cmd == "BURN") {
        bool readAllColumns = (cmd == "READ*");
        Array<String> columns;
        Array<Clause> writeCols;
        Array<Clauses> queryClauses;

        usz idx = tokenIdx + 1;
        while (idx < tokens.size()) {
            String t = tokens[idx];
            String tu = t.toUpperCase();
            if (tu == "WHERE" || tu == "MATCH" || tu == "ASSERT" || tu == "FOLLOW" || tu == "REPEATFOLLOW" || tu == "LIMIT" || tu == "PAGE") {
                break;
            }
            if (isRead) {
                columns.push(t);
            } else {
                bool isThreeToken = false;
                if (idx + 2 < tokens.size()) {
                    String op = tokens[idx+1];
                    if (op == "=") {
                        Clause c;
                        c.col = tokens[idx];
                        c.op = op;
                        c.val = tokens[idx+2];
                        writeCols.push(c);
                        idx += 2;
                        isThreeToken = true;
                    } else if (op == "+=") {
                        Clause c;
                        String col = tokens[idx];
                        if (col.endsWith("]")) {
                            c.col = col.substring(0, col.size() - 1) + "+]";
                        } else {
                            c.col = col + "[+]";
                        }
                        c.op = op;
                        c.val = tokens[idx+2];
                        writeCols.push(c);
                        idx += 2;
                        isThreeToken = true;
                    }
                }
                if (!isThreeToken && idx + 1 < tokens.size()) {
                    String op = tokens[idx+1].toUpperCase();
                    if (op == "EMPTY") {
                        Clause c;
                        c.col = tokens[idx];
                        c.op = "empty";
                        c.val = "";
                        writeCols.push(c);
                        idx += 1;
                        isThreeToken = true;
                    }
                }
                if (!isThreeToken) {
                    writeCols.push(parseClauseStr(t));
                }
            }
            idx++;
        }

        bool hasWildcard = false;
        for (usz i = 0; i < columns.size(); ++i) {
            if (columns[i] == "*") {
                hasWildcard = true;
                columns[i] = columns[columns.size() - 1];
                columns.pop();
                break;
            }
        }

        if ((cmd == "READ" || cmd == "READ*") && (columns.size() == 0 || hasWildcard)) {
            readAllColumns = true;
        }

        parseClausesFromTokens(tokens, idx, queryClauses);

        // Retrieve LIMIT and PAGE parameters
        u64 limitVal = 0;
        u64 pageVal = 0;
        for (usz i = idx; i < tokens.size(); ++i) {
            String tu = tokens[i].toUpperCase();
            if (tu == "LIMIT" && i + 1 < tokens.size()) {
                limitVal = (u64)strtoull((const char*)tokens[i+1].data(), nullptr, 0);
            }
            if (tu == "PAGE" && i + 1 < tokens.size()) {
                pageVal = (u64)strtoull((const char*)tokens[i+1].data(), nullptr, 0);
            }
        }
        
        if (isRead) {
            res.readRows = engine->read(columns, queryClauses, limitVal, pageVal, viewTombstones, 0, readAllColumns, now);
            res.code = res.readRows.size();
        } else if (cmd == "WRITE" || cmd == "WRITEVOLATILE" || cmd == "APPEND") {
            for (usz i = 0; i < writeCols.size(); ++i) {
                if (writeCols[i].col.endsWith(":generate")) {
                    String baseCol = writeCols[i].col.substring(0, writeCols[i].col.size() - 9);
                    writeCols[i].col = baseCol;
                    writeCols[i].val = engine->generateId(baseCol);
                }
            }
            if (cmd == "WRITE") {
                res.code = engine->write(writeCols, queryClauses, 0, queryEncryptKey, now);
            } else if (cmd == "APPEND") {
                res.code = engine->append(writeCols, queryClauses, 0, queryEncryptKey, now);
            } else {
                res.code = engine->writeVolatile(writeCols, queryClauses);
            }
        } else if (cmd == "RM" || cmd == "BURN") {
            res.code = engine->rm(queryClauses, 0, 0, cmd == "BURN", now) ? 0 : -1;
        }
        return res;
    }


    res.code = -99; // Unknown command
    return res;
}

String QueryResult::getRowsJson() const {
    String json = "[";
    for (usz i = 0; i < readRows.size(); ++i) {
        if (i > 0) json += ",";
        json += "{";
        bool first = true;
        for (auto it = readRows[i].begin(); it != readRows[i].end(); ++it) {
            if (!first) json += ",";
            first = false;
            
            // Escape double quotes and backslashes in key and value
            String escapedKey;
            for (usz j = 0; j < it->key.length(); ++j) {
                if (it->key[j] == '"' || it->key[j] == '\\') escapedKey.push('\\');
                escapedKey.push(it->key[j]);
            }
            
            String escapedVal;
            for (usz j = 0; j < it->value.length(); ++j) {
                if (it->value[j] == '"' || it->value[j] == '\\') escapedVal.push('\\');
                escapedVal.push(it->value[j]);
            }
            
            json += "\"" + escapedKey + "\":\"" + escapedVal + "\"";
        }
        json += "}";
    }
    json += "]";
    return json;
}

} // namespace Xylem
