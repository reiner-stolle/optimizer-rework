#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <string>

namespace tuddbs {
static const uint32_t TCP_START_DELIM = 0x5ADB0BB1;

using ClientHandle = int;
using ServerHandle = int;

enum class TCPPackageType : uint32_t {
    Undefined = 0,
    Work,
    RerouteWork,
    TaskFinished,
    Text,
    UpdateUnitType,
    QueryPlan,
    MonitorRequest,
    ConnectAction,
    ConnectActionInfo,
    ConfigurationAction,
    UuidForUnitRequest,
    UuidForUnitResponse,
    UuidCollision
};

enum class UnitType : uint32_t {
    Undefined = 0,
    QueryPlaner,
    ComputeUnit,
    MemoryUnit,
    MetaUnit,
    MonitorUnit,
    DatabaseUnit,
    OptimizerUnit
};

struct TCPMetaInfo {
    uint32_t message_delimiter = TCP_START_DELIM;
    UnitType unit_type = UnitType::Undefined;
    uint32_t payload_size = 0;
    TCPPackageType package_type = TCPPackageType::Undefined;
    uint64_t src_uuid = 0;
    uint64_t tgt_uuid = 0;

    size_t bytesize() const { return sizeof(TCPMetaInfo) + payload_size; }
};

struct ClientInfo {
    ClientHandle handle;
    std::thread* receiver;
    size_t unprocessed_bytes;
    UnitType type = UnitType::Undefined;
    uint64_t uuid;
    std::string prettyName;
    bool abort = false;

    ClientInfo() = default;
    ~ClientInfo();
    ClientInfo& operator=(const ClientInfo&);

    void runReceive();
    void release();

    bool operator==(const ClientInfo& other) {
        return this->handle == other.handle;
    }

   private:
    bool cleanupDone = false;
};

typedef std::function<void(TCPMetaInfo* meta, void* data, size_t len)> ReceiveCallback;
typedef std::map<TCPPackageType, ReceiveCallback> CallbackMap;

class TCPServer {
   public:
    typedef std::function<void(ClientHandle)> ConnectCallback;

   private:
    int _port;
    std::thread t;
    ConnectCallback connectCallback;

   public:
    TCPServer(int port);
    ~TCPServer();

    void setConnectCallback(ConnectCallback cc);

    void start();

    void closeConnection();

    bool hasClients() const;
    void clientDebugInfo() const;
    ClientInfo* getClientByUuid(uint64_t uuid) const;
    std::vector<std::pair<std::string,uint64_t>> getUuidForUnitType( UnitType type );

    bool sendTo(ClientInfo* info, const char* data, uint32_t len);
    void sendToAll(const char* data, uint32_t len);
    void sendToAllOfType(UnitType type, const char* data, uint32_t len);
    void sendToAnyOfType(UnitType type, const char* data, uint32_t len);
    void rerouteToAnyOfType(UnitType type, const uint64_t original_uuid, const char* original_data, uint32_t original_len);
    void addCallback(TCPPackageType type, ReceiveCallback cb);
    void setTimeoutToHandle( ClientHandle handle, const size_t sec, const size_t usec );

    static std::string unitTypeToString( UnitType type );
    static std::string packageTypeToString( TCPPackageType type );

    void clear_aborted();
    static void freeHandle(ClientHandle handle);
    std::string monitorInfoToString();

   private:
    bool globalAbort = false;
    bool cleanupDone = false;
    ServerHandle serverHandle;

    CallbackMap callbacks;
    std::map<UnitType, std::vector<ClientInfo*> > clientMap;
    std::unordered_map<uint64_t, ClientInfo*> clientUuidMap;
    std::recursive_mutex cl_mutex;
    void acceptLoop();
    void removeClient(ClientInfo* info);
    void removeClient(UnitType type, ClientInfo* info);
};

}  // namespace tuddbs