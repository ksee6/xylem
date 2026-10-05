#ifndef XYLEM_SERVER_HPP
#define XYLEM_SERVER_HPP

#include <Xylem/Xylem.hpp>
#include <Rho/Tunnel.hpp>
#include <Util/Server.hpp>
#include <Lines/Bind.hpp>
#include <Ksee/Map.hpp>
#include <Ksee/String.hpp>
#include <Ksee/Array.hpp>

#include <mutex>

namespace Xylem {

class XylemServer {
public:
    XylemEngine& engine;
    Rho::Server server;
    Map<Rho::Tunnel*, String> clientIdentities;
    Map<Rho::Tunnel*, String> clientPubKeys;
    std::shared_mutex* engineMutex = nullptr;
    
    // Permission cache: avoids querying /perms/ on every request
    bool permsCacheChecked = false;
    bool permsCacheResult = false;

    XylemServer(XylemEngine& eng, std::shared_mutex* mtx = nullptr);
    ~XylemServer();

    void hook(Lines::Bind& bind);
    void update();

    bool hasAnyPermissions();
    bool checkPermission(const String& clientHash, const String& action, const String& path);
    String getClientPubKey(const String& clientHash);
    String getPathForId(const String& id);
    String getPathForRow(const Map<String, String>& row);
    String getPathForRowId(u64 rId);
    String getPermPath(const String& action, const String& path);

private:
    void handleCart(Rho::Cart& cart);
    void handlePacket(const Rho::Packet& p, Rho::Tunnel& tunnel);
    
    // Deconstruct paths and check permission helpers
    bool checkReadPermForRows(const String& clientHash, Array<Map<String, String>>& rows);
    bool checkWritePerm(const String& clientHash, const Array<Clause>& columns, const Array<Clauses>& clauses);
    bool checkRmPerm(const String& clientHash, const Array<Clauses>& clauses);
};

} // namespace Xylem

#endif // XYLEM_SERVER_HPP
