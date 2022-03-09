#pragma once

#include "binary_semaphore.h"
#include "port.h"
#include "rpc_config.h"
#include <mutex>
#include <wblib/rpc.h>

enum class RPCPortState
{
    RPC_IDLE,
    RPC_WRITE,
    RPC_READ,
    RPC_ERROR
};

class TRPCPortHandler
{
public:
    TRPCPortHandler();
    bool RPCTransceive(const TRPCPortConfig& config,
                       std::vector<uint8_t>& response,
                       size_t& actualResponseSize,
                       PBinarySemaphore rpcSemaphore,
                       PBinarySemaphoreSignal rpcSignal);
    void RPCRequestHandling(PPort port);

private:
    std::mutex RPCMutex;
    std::vector<uint8_t> RPCWriteData, RPCReadData;
    size_t RPCRequestedSize, RPCActualSize;
    std::chrono::microseconds RPCRespTimeout;
    std::chrono::microseconds RPCFrameTimeout;
    RPCPortState RPCState = RPCPortState::RPC_IDLE;
    PBinarySemaphore Semaphore;
    PBinarySemaphoreSignal Signal;
};