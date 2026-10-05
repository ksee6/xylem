#ifndef XYLEM_CLIENT_HPP
#define XYLEM_CLIENT_HPP

#include <Xylem/Xylem.hpp>
#include <Rho/Tunnel.hpp>
#include <Util/Client.hpp>
#include <Lines/Bind.hpp>
#include <Ksee/Map.hpp>
#include <Ksee/String.hpp>
#include <Ksee/Array.hpp>

namespace Xylem {

class XylemClient {
public:
    Rho::Client client;
    Lines::Bind* bind = nullptr;
    bool isConnected = false;

    XylemClient();
    ~XylemClient();

    bool connect(Lines::Bind& b, const Resource::NumericalAddress& address, const Security::KeyPair& staticKeyPair);
    bool connect(Lines::Bind& b, const String& addressStr, const Security::KeyPair& staticKeyPair);
    bool connect(Lines::Bind& b, const char* addressStr, const Security::KeyPair& staticKeyPair);

    bool isMounted() const;
    void update();

    // ─── Query Parser ────────────────────────────────────────────────────────
    QueryResult query(const String& queryString, const Array<String>& sanitized = Array<String>());

    // ─── Database ────────────────────────────────────────────────────────────
    Array<Map<String,String>> read(
        const Array<String>& columns,
        const Array<Clauses>& clauses,
        u64 length = 0, u64 page = 0, bool tombstones = false, u64 txId = 0,
        bool readAllColumns = false);

    int write(const Array<Clause>& columns,
              const Array<Clauses>& clauses = Array<Clauses>(),
              u64 txId = 0, const String& encryptionKey = "");

    int writeVolatile(const Array<Clause>& columns,
                      const Array<Clauses>& clauses = Array<Clauses>(),
                      u64 txId = 0, const String& encryptionKey = "");

    bool rm(const Array<Clauses>& clauses, u64 length = 0, u64 as = 0);

    // Transactions (MVCC)
    u64 lock(const Array<Clauses>& clauses = Array<Clauses>(), u64 id = 0, bool requiresExplicitAs = true);
    u64 commit(const Array<Clauses>& clauses = Array<Clauses>(), u64 id = 0);
    bool rollback(u64 id);
    int unlock(u64 id);

    String generateId(const String& column);

    // File-like convenience API
    QueryResult cat(const String& path, u64 start = 0, u64 end = 0);
    QueryResult tee(const String& path, const String& content, u64 start = 0, u64 end = 0);
    QueryResult ls(const String& path = "");
    bool rm(const String& path);
    QueryResult cp(const String& src, const String& dst);
    QueryResult mv(const String& src, const String& dst);

    // Reactivity
    u64 watch(const Array<Clauses>& clauses);
    bool unwatch(u64 id);
    Array<Map<String,String>> pull(u64 id);

private:
    Map<String, String> sendRequest(const Map<String, String>& req);
    
    String serializeClauses(const Array<Clauses>& clauses);
    String serializeClauseArray(const Array<Clause>& columns);
};

} // namespace Xylem

#endif // XYLEM_CLIENT_HPP
