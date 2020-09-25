#include "modbus_common.h"
#include "serial_device.h"
#include "crc16.h"
#include "log.h"

#include <cmath>
#include <array>
#include <cassert>
#include <unistd.h>
#include <math.h>

using namespace std;

#define LOG(logger) ::logger.Log() << "[modbus] "

namespace Modbus    // modbus protocol declarations
{
    const int MAX_READ_BITS = 2000;
    const int MAX_WRITE_BITS = 1968;

    const int MAX_READ_REGISTERS = 125;
    const int MAX_WRITE_REGISTERS = 123;
    const int MAX_RW_WRITE_REGISTERS = 121;

    const size_t EXCEPTION_RESPONSE_PDU_SIZE = 2;
    const size_t WRITE_RESPONSE_PDU_SIZE = 5;

    enum Error: uint8_t {
        ERR_NONE                                    = 0x0,
        ERR_ILLEGAL_FUNCTION                        = 0x1,
        ERR_ILLEGAL_DATA_ADDRESS                    = 0x2,
        ERR_ILLEGAL_DATA_VALUE                      = 0x3,
        ERR_SERVER_DEVICE_FAILURE                   = 0x4,
        ERR_ACKNOWLEDGE                             = 0x5,
        ERR_SERVER_DEVICE_BUSY                      = 0x6,
        ERR_MEMORY_PARITY_ERROR                     = 0x8,
        ERR_GATEWAY_PATH_UNAVAILABLE                = 0xA,
        ERR_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND = 0xB
    };

    union TAddress
    {
        int64_t AbsAddress;
        struct {
            int Type;
            int Address;
        };
    };

    class TModbusRegisterRange: public TRegisterRange
    {
    public:
        TModbusRegisterRange(const std::list<PRegister>& regs, bool hasHoles);
        ~TModbusRegisterRange();
        EStatus GetStatus() const override;
        void  SetStatus(EStatus status);
        int GetStart() const { return Start; }
        int GetCount() const { return Count; }
        uint8_t* GetBits();
        uint16_t* GetWords();

        bool HasHoles() const { return HasHolesFlg; }

        // Read each register separately
        bool ShouldReadOneByOne() const { return ReadOneByOne; }
        void SetReadOneByOne(bool readOneByOne) { ReadOneByOne = readOneByOne; }

    private:
        bool            ReadOneByOne    = false;
        bool            HasHolesFlg     = false;
        int             Start;
        int             Count;
        uint8_t*        Bits  = 0;
        uint16_t*       Words = 0;
        EStatus         Status = ST_UNKNOWN_ERROR;
    };

    using PModbusRegisterRange = std::shared_ptr<TModbusRegisterRange>;
}

namespace   // general utilities
{
    // write 16-bit value to byte array in big-endian order
    inline void WriteAs2Bytes(uint8_t* dst, uint16_t val)
    {
        dst[0] = static_cast<uint8_t>(val >> 8);
        dst[1] = static_cast<uint8_t>(val);
    }

    // returns true if multi write needs to be done
    inline bool IsPacking(PRegister reg)
    {
        return (reg->Type == Modbus::REG_HOLDING_MULTI) ||
              ((reg->Type == Modbus::REG_HOLDING) && (reg->Get16BitWidth() > 1));
    }

    inline bool IsPacking(Modbus::PModbusRegisterRange range)
    {
        return (range->Type() == Modbus::REG_HOLDING_MULTI) ||
              ((range->Type() == Modbus::REG_HOLDING) && (range->GetCount() > 1));
    }

    inline bool IsSingleBitType(int type)
    {
        return (type == Modbus::REG_COIL) || (type == Modbus::REG_DISCRETE);
    }

    inline uint64_t MersenneNumber(uint8_t bitCount)
    {
        assert(bitCount <= 64);
        return (uint64_t(1) << bitCount) - 1;
    }
}   // general utilities


namespace Modbus    // modbus protocol common utilities
{
    TModbusRegisterRange::TModbusRegisterRange(const std::list<PRegister>& regs, bool hasHoles)
        : TRegisterRange(regs)
        , HasHolesFlg(hasHoles)
    {
        if (regs.empty()) // shouldn't happen
            throw std::runtime_error("cannot construct empty register range");

        if (IsSingleBitType(Type())) {
            for (auto reg: RegisterList()) {
                if (reg->Get16BitWidth() != 1)
                    throw TSerialDeviceException(
                        "width other than 1 is not currently supported for reg type" +
                        reg->TypeName);
            }
        }

        auto it = regs.begin();
        Start = (*it)->Address;
        int end = Start + (*it)->Get16BitWidth();
        while (++it != regs.end()) {
            if ((*it)->Type != Type())
                throw std::runtime_error("registers of different type in the same range");
            int new_end = (*it)->Address + (*it)->Get16BitWidth();
            if (new_end > end)
                end = new_end;
        }
        Count = end - Start;
        if (Count > (IsSingleBitType(Type()) ? MAX_READ_BITS : MAX_READ_REGISTERS))
            throw std::runtime_error("Modbus register range too large");
    }

    void TModbusRegisterRange::SetStatus(TRegisterRange::EStatus status) {
        Status = status;
    }

    TRegisterRange::EStatus TModbusRegisterRange::GetStatus() const
    {
        return Status;
    }

    TModbusRegisterRange::~TModbusRegisterRange() {
        if (Bits)
            delete[] Bits;
        if (Words)
            delete[] Words;
    }

    uint8_t* TModbusRegisterRange::GetBits() {
        if (!IsSingleBitType(Type()))
            throw std::runtime_error("GetBits() for non-bit register");
        if (!Bits)
            Bits = new uint8_t[Count];
        return Bits;
    }

    uint16_t* TModbusRegisterRange::GetWords() {
        if (IsSingleBitType(Type()))
            throw std::runtime_error("GetWords() for non-word register");
        if (!Words)
            Words = new uint16_t[Count];
        return Words;
    }

    ostream& operator<<(ostream& s, const TModbusRegisterRange& range)
    {
        s << range.GetCount() << " " << range.TypeName()
          << "(s) @ " << range.GetStart() << " of device " << range.Device()->ToString();
        return s;
    }

    const uint8_t EXCEPTION_BIT = 1 << 7;

    enum ModbusFunction: uint8_t {
        FN_READ_COILS               = 0x1,
        FN_READ_DISCRETE            = 0x2,
        FN_READ_HOLDING             = 0x3,
        FN_READ_INPUT               = 0x4,
        FN_WRITE_SINGLE_COIL        = 0x5,
        FN_WRITE_SINGLE_REGISTER    = 0x6,
        FN_WRITE_MULTIPLE_COILS     = 0xF,
        FN_WRITE_MULTIPLE_REGISTERS = 0x10,
    };

    enum class OperationType: uint8_t {
        OP_READ = 0,
        OP_WRITE
    };

    inline bool IsException(const uint8_t* pdu)
    {
        return pdu[0] & EXCEPTION_BIT;
    }

    // returns modbus exception code if there is any, otherwise 0
    inline uint8_t GetExceptionCode(const uint8_t* pdu)
    {
        if (IsException(pdu)) {
            return pdu[1];      // then error code in the next byte
        }
        return 0;
    }

    // choose function code for modbus request
    uint8_t GetFunctionImpl(int registerType, OperationType op, const std::string& typeName, bool many)
    {
        switch (registerType) {
        case REG_HOLDING_SINGLE:
        case REG_HOLDING_MULTI:
        case REG_HOLDING:
            switch (op) {
            case OperationType::OP_READ:
                return FN_READ_HOLDING;
            case OperationType::OP_WRITE:
                return many ? FN_WRITE_MULTIPLE_REGISTERS : FN_WRITE_SINGLE_REGISTER;
            default:
                break;
            }
            break;
        case REG_INPUT:
            switch (op) {
            case OperationType::OP_READ:
                return FN_READ_INPUT;
            default:
                break;
            }
            break;
        case REG_COIL:
            switch (op) {
            case OperationType::OP_READ:
                return FN_READ_COILS;
            case OperationType::OP_WRITE:
                return many ? FN_WRITE_MULTIPLE_COILS : FN_WRITE_SINGLE_COIL;
            default:
                break;
            }
            break;
        case REG_DISCRETE:
            switch (op) {
            case OperationType::OP_READ:
                return FN_READ_DISCRETE;
            default:
                break;
            }
            break;
        default:
            break;
        }

        switch (op) {
        case OperationType::OP_READ:
            throw TSerialDeviceException("can't read from " + typeName);
        case OperationType::OP_WRITE:
            throw TSerialDeviceException("can't write to " + typeName);
        default:
            throw TSerialDeviceException("invalid operation type");
        }
    }

    inline uint8_t GetFunction(PRegister reg, OperationType op)
    {
        return GetFunctionImpl(reg->Type, op, reg->TypeName, IsPacking(reg));
    }

    inline uint8_t GetFunction(PModbusRegisterRange range, OperationType op)
    {
        return GetFunctionImpl(range->Type(), op, range->TypeName(), IsPacking(range));
    }

    // throws C++ exception on modbus error code
    void ThrowIfModbusException(uint8_t code)
    {
        if ( code == 0) {
            return;
        }
        typedef std::pair<const char*, bool> TModbusException;
        const TModbusException errs[] =             
            { {"illegal function",                        true },     // 0x01
              {"illegal data address",                    true },     // 0x02
              {"illegal data value",                      true },     // 0x03
              {"server device failure",                   false},     // 0x04
              {"long operation (acknowledge)",            false},     // 0x05
              {"server device is busy",                   false},     // 0x06
              {nullptr,                                   false},     // 0x07
              {"memory parity error",                     false},     // 0x08
              {nullptr,                                   false},     // 0x09
              {"gateway path is unavailable",             false},     // 0x0A
              {"gateway target device failed to respond", false}  };  // 0x0B
        --code;
        if (code < sizeof(errs)/sizeof(TModbusException)) {
            const auto& err = errs[code];
            if (err.first != nullptr) {
                if (err.second) {
                    throw TSerialDevicePermanentRegisterException(err.first);
                }
                throw TSerialDeviceTransientErrorException(err.first);
            }
        }
        throw TSerialDeviceTransientErrorException("invalid modbus error code (" + std::to_string(code+1) + ")");
    }

    // returns count of modbus registers needed to represent TRegister
    uint16_t GetQuantity(PRegister reg)
    {
        int w = reg->Get16BitWidth();

        if (IsSingleBitType(reg->Type)) {
            if (w != 1) {
                throw TSerialDeviceException("width other than 1 is not currently supported for reg type" + reg->TypeName);
            }
            return 1;
        } else {
            if (w > 4 && reg->BitOffset == 0) {
                throw TSerialDeviceException("can't pack more than 4 " + reg->TypeName + "s into a single value");
            }
            return w;
        }
    }

    // returns count of modbus registers needed to represent TModbusRegisterRange
    uint16_t GetQuantity(PModbusRegisterRange range)
    {
        auto type = range->Type();

        if (!IsSingleBitType(type) &&
             type != REG_HOLDING &&
             type != REG_HOLDING_SINGLE &&
             type != REG_HOLDING_MULTI &&
             type != REG_INPUT
        ) {
            throw TSerialDeviceException("invalid register type");
        }

        return range->GetCount();
    }

    // returns number of bytes needed to hold request
    size_t InferWriteRequestPDUSize(PRegister reg)
    {
       return IsPacking(reg) ? 6 + reg->Get16BitWidth() * 2 : 5;
    }

    // returns number of requests needed to write register
    size_t InferWriteRequestsCount(PRegister reg)
    {
       return IsPacking(reg) ? 1 : reg->Get16BitWidth();
    }

    // returns number of bytes needed to hold response
    size_t InferReadResponsePDUSize(PModbusRegisterRange range)
    {
        auto count = range->GetCount();

        if (IsSingleBitType(range->Type())) {
            return 2 + std::ceil(static_cast<float>(count) / 8);    // coil values are packed into bytes as bitset
        } else {
            return 2 + count * 2;   // count is for uint16_t, we need byte count
        }
    }

    inline size_t ReadResponsePDUSize(const uint8_t* pdu)
    {
        // Modbus stores data byte count in second byte of PDU,
        // so PDU size is data size + 2 (1b function code + 1b byte count itself)
        return IsException(pdu) ? EXCEPTION_RESPONSE_PDU_SIZE : pdu[1] + 2;
    }

    inline size_t WriteResponsePDUSize(const uint8_t* pdu)
    {
        return IsException(pdu) ? EXCEPTION_RESPONSE_PDU_SIZE : WRITE_RESPONSE_PDU_SIZE;
    }

    // fills pdu with read request data according to Modbus specification
    void ComposeReadRequestPDU(uint8_t* pdu, PRegister reg, int shift)
    {
        pdu[0] = GetFunction(reg, OperationType::OP_READ);
        WriteAs2Bytes(pdu + 1, reg->Address + shift);
        WriteAs2Bytes(pdu + 3, GetQuantity(reg));
    }

    void ComposeReadRequestPDU(uint8_t* pdu, PModbusRegisterRange range, int shift)
    {
        pdu[0] = GetFunction(range, OperationType::OP_READ);
        WriteAs2Bytes(pdu + 1, range->GetStart() + shift);
        WriteAs2Bytes(pdu + 3, GetQuantity(range));
    }

    // fills pdu with write request data according to Modbus specification
    void ComposeMultipleWriteRequestPDU(uint8_t* pdu, PRegister reg, uint64_t value, int shift)
    {
        auto & tmpCache = reg->Device()->ModbusTmpCache;
        const auto & cache = reg->Device()->ModbusCache;

        pdu[0] = GetFunction(reg, OperationType::OP_WRITE);

        auto baseAddress = reg->Address + shift;
        const auto bitWidth = reg->GetBitWidth();

        auto bitsToAllocate = bitWidth;

        TAddress address;

        address.Type = reg->Type;

        WriteAs2Bytes(pdu + 1, baseAddress);
        WriteAs2Bytes(pdu + 3, reg->Get16BitWidth());

        pdu[5] = reg->Get16BitWidth() * 2;

        uint8_t bitPos = 0, bitPosEnd = bitWidth;

        for (int i = 0; i < reg->Get16BitWidth(); ++i) {
            address.Address = baseAddress + i;

            uint16_t cachedValue;
            if (cache.count(address.AbsAddress)) {
                cachedValue = cache.at(address.AbsAddress);
            } else {
                cachedValue = value & 0xffff;
            }

            auto localBitOffset = std::max(reg->BitOffset - bitPos, 0);

            auto bitCount = std::min(uint8_t(16 - localBitOffset), bitsToAllocate);

            auto rBitPos = bitPosEnd - bitPos - bitCount;

            auto mask = MersenneNumber(bitCount);

            auto valuePart = mask & (value >> rBitPos);

            auto wordValue = (~mask & cachedValue) | (valuePart << localBitOffset);

            tmpCache[address.AbsAddress] = wordValue & 0xffff;

            WriteAs2Bytes(pdu + 6 + i * 2, wordValue & 0xffff);
            bitsToAllocate -= bitCount;
            bitPos += bitCount;
        }
    }

    void ComposeSingleWriteRequestPDU(uint8_t* pdu, PRegister reg, uint16_t value, int shift, uint8_t wordIndex)
    {
        auto & tmpCache = reg->Device()->ModbusTmpCache;
        const auto & cache = reg->Device()->ModbusCache;

        if (reg->Type == REG_COIL) {
            value = value ? uint16_t(0xFF) << 8: 0x00;
        }

        auto bitWidth = reg->GetBitWidth();

        TAddress address;

        address.Type = reg->Type;
        address.Address = reg->Address + shift + wordIndex;

        uint16_t cachedValue;
        if (cache.count(address.AbsAddress)) {
            cachedValue = cache.at(address.AbsAddress);
        } else {
            cachedValue = value & 0xffff;
        }


        auto localBitOffset = std::max(reg->BitOffset - wordIndex * 16, 0);

        auto bitCount = std::min(uint8_t(16 - localBitOffset), bitWidth);

        auto mask = MersenneNumber(bitCount) << localBitOffset;

        auto wordValue = (~mask & cachedValue) | (mask & (value << localBitOffset));

        tmpCache[address.AbsAddress] = wordValue & 0xffff;

        pdu[0] = GetFunction(reg, OperationType::OP_WRITE);

        WriteAs2Bytes(pdu + 1, address.Address);
        WriteAs2Bytes(pdu + 3, wordValue);
    }

    // parses modbus response and stores result
    void ParseReadResponse(const uint8_t* pdu, PModbusRegisterRange range)
    {
        TAddress address;

        address.Type = range->Type();

        auto & cache = range->Device()->ModbusCache;
        auto baseAddress = range->GetStart();

        ThrowIfModbusException(GetExceptionCode(pdu));

        uint8_t byte_count = pdu[1];

        auto start = pdu + 2;
        auto end = start + byte_count;
        if (IsSingleBitType(range->Type())) {
            auto destination = range->GetBits();
            auto coil_count = range->GetCount();
            while (start != end) {
                std::bitset<8> coils(*start++);
                auto coils_in_byte = std::min(coil_count, 8);
                for (int i = 0; i < coils_in_byte; ++i) {
                    destination[i] = coils[i];
                }

                coil_count -= coils_in_byte;
                destination += coils_in_byte;
            }

            for (auto reg: range->RegisterList()) {
                reg->SetValue(range->GetBits()[reg->Address - range->GetStart()]);
            }
            return;

        }

        auto destination = range->GetWords();
        for (int i = 0; i < byte_count / 2; ++i) {
            address.Address = baseAddress + i;

            cache[address.AbsAddress] = destination[i] = (*start << 8) | *(start + 1);

            start += 2;
        }

        for (auto reg: range->RegisterList()) {
            int w = reg->Get16BitWidth();
            auto bitWidth = reg->GetBitWidth();

            uint64_t r = 0;

            auto wordIndex = (reg->Address - range->GetStart());
            auto reverseWordIndex = w - 1;

            uint8_t bitsWritten = 0;

            while (w--) {
                uint16_t data = destination[reg->Address - range->GetStart() + w];

                auto localBitOffset = std::max(reg->BitOffset - wordIndex * 16, 0);

                auto bitCount = std::min(uint8_t(16 - localBitOffset), bitWidth);

                auto mask = MersenneNumber(bitCount);

                r |= (mask & (data >> localBitOffset)) << bitsWritten;

                --reverseWordIndex;
                ++wordIndex;
                bitWidth -= bitCount;
                bitsWritten += bitCount;

            }
            if ((reg->UnsupportedValue) && (*reg->UnsupportedValue == r)) {
                reg->SetError();
                reg->SetAvailable(false);
            } else {
                reg->SetValue(r);
            }
        }
    }

    // checks modbus response on write
    void ParseWriteResponse(const uint8_t* pdu)
    {
        ThrowIfModbusException(GetExceptionCode(pdu));
    }

    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister>& reg_list, PDeviceConfig deviceConfig, bool enableHoles)
    {
        std::list<PRegisterRange> r;
        if (reg_list.empty())
            return r;

        std::list<PRegister> l;
        int prev_start = -1, prev_type = -1, prev_end = -1;
        std::chrono::milliseconds prev_interval;
        int max_hole = enableHoles ? (IsSingleBitType(reg_list.front()->Type) ? deviceConfig->MaxBitHole : deviceConfig->MaxRegHole) : 0;
        int max_regs;

        if (IsSingleBitType(reg_list.front()->Type)) {
            max_regs = MAX_READ_BITS;
        } else {
            if ((deviceConfig->MaxReadRegisters > 0) && (deviceConfig->MaxReadRegisters <= MAX_READ_REGISTERS)) {
                max_regs = deviceConfig->MaxReadRegisters;
            } else {
                max_regs = MAX_READ_REGISTERS;
            }
        }

        bool hasHoles = false;
        for (auto reg: reg_list) {
            int new_end = reg->Address + reg->Get16BitWidth();
            if (!(prev_end >= 0 &&
                reg->Type == prev_type &&
                reg->Address >= prev_end &&
                reg->Address <= prev_end + max_hole &&
                reg->PollInterval == prev_interval &&
                new_end - prev_start <= max_regs)) {
                if (!l.empty()) {
                    auto range = std::make_shared<TModbusRegisterRange>(l, hasHoles);
                    hasHoles = false;
                    LOG(Debug) << "Adding range: " << *range;
                    r.push_back(range);
                    l.clear();
                }
                prev_start = reg->Address;
                prev_type = reg->Type;
                prev_interval = reg->PollInterval;
            }
            if (!l.empty()) {
                hasHoles |= (reg->Address != prev_end);
            }
            l.push_back(reg);
            prev_end = new_end;
        }
        if (!l.empty()) {
            auto range = std::make_shared<TModbusRegisterRange>(l, hasHoles);
            LOG(Debug) << "Adding range: " << *range;
            r.push_back(range);
        }
        return r;
    }
}  // modbus protocol common utilities

namespace ModbusRTU // modbus rtu protocol utilities
{
    using TReadRequest = std::array<uint8_t, 8>;
    using TWriteRequest = std::vector<uint8_t>;

    using TReadResponse = std::vector<uint8_t>;
    using TWriteResponse = std::array<uint8_t, 8>;

    class TInvalidCRCError: public TSerialDeviceTransientErrorException
    {
    public:
        TInvalidCRCError(): TSerialDeviceTransientErrorException("invalid crc")
        {}
    };

    class TMalformedResponseError: public TSerialDeviceTransientErrorException
    {
    public:
        TMalformedResponseError(const std::string & what): TSerialDeviceTransientErrorException("malformed response: " + what)
        {}
    };

    const size_t DATA_SIZE = 3;  // number of bytes in ADU that is not in PDU (slaveID (1b) + crc value (2b))

    // get pointer to PDU in message
    template <class T>
    inline const uint8_t* PDU(const T& msg)
    {
        return &msg[1];
    }

    template <class T>
    inline uint8_t* PDU(T& msg)
    {
        return &msg[1];
    }

    inline size_t InferWriteRequestSize(PRegister reg)
    {
        return Modbus::InferWriteRequestPDUSize(reg) + DATA_SIZE;
    }

    inline size_t InferReadResponseSize(Modbus::PModbusRegisterRange range)
    {
        return Modbus::InferReadResponsePDUSize(range) + DATA_SIZE;
    }

    TPort::TFrameCompletePred ExpectNBytes(int n)
    {
        return [n](uint8_t* buf, int size) {
            if (size < 2)
                return false;
            if (Modbus::IsException(PDU(buf)))
                return size >= static_cast<int>(Modbus::EXCEPTION_RESPONSE_PDU_SIZE + DATA_SIZE);
            return size >= n;
        };
    }

    void ComposeReadRequest(TReadRequest& req, Modbus::PModbusRegisterRange range, uint8_t slaveId, int shift)
    {
        req[0] = slaveId;
        Modbus::ComposeReadRequestPDU(PDU(req), range, shift);
        WriteAs2Bytes(&req[6], CRC16::CalculateCRC16(req.data(), 6));
    }

    void ComposeWriteRequests(std::vector<TWriteRequest> & requests, PRegister reg, uint8_t slaveId, uint64_t value, int shift)
    {
        requests.resize(Modbus::InferWriteRequestsCount(reg));

        for (std::size_t i = 0; i < requests.size(); ++i) {
            auto & req = requests[i];
            req.resize(InferWriteRequestSize(reg));
            req[0] = slaveId;

            if (IsPacking(reg)) {
                assert(requests.size() == 1 && "only one request is expected when using multiple write");
                Modbus::ComposeMultipleWriteRequestPDU(PDU(req), reg, value, shift);
            } else {
                Modbus::ComposeSingleWriteRequestPDU(PDU(req), reg, static_cast<uint16_t>(value & 0xffff), shift, requests.size() - i - 1);
                value >>= 16;
            }

            WriteAs2Bytes(&req[req.size() - 2], CRC16::CalculateCRC16(req.data(), req.size() - 2));
        }
    }

    size_t GetResponsePDUSize(const TReadResponse & res)
    {
        return Modbus::ReadResponsePDUSize(PDU(res));
    }

    size_t GetResponsePDUSize(const TWriteResponse & res)
    {
        return Modbus::WriteResponsePDUSize(PDU(res));
    }

    template <class TRequest, class TResponse>
    void CheckResponse(const TRequest & req, const TResponse & res)
    {
        auto pdu_size = GetResponsePDUSize(res);

        if (pdu_size >= (res.size() - 2)) {
            throw TMalformedResponseError("invalid data size");
        }

        uint16_t crc = (res[pdu_size + 1] << 8) + res[pdu_size + 2];
        if (crc != CRC16::CalculateCRC16(res.data(), pdu_size + 1)) {
            throw TInvalidCRCError();
        }

        auto requestSlaveId = req[0];
        auto responseSlaveId = res[0];
        if (requestSlaveId != responseSlaveId) {
            throw TSerialDeviceTransientErrorException("request and response slave id mismatch");
        }

        auto requestFunctionCode = PDU(req)[0];
        auto responseFunctionCode = PDU(res)[0] & 127; // get actual function code even if exception

        if (requestFunctionCode != responseFunctionCode) {
            throw TSerialDeviceTransientErrorException("request and response function code mismatch");
        }
    }

    template<class Req, class Resp> bool ProcessRequest(PPort& port, const Req& request, Resp& response, const PDeviceConfig& config)
    {
        port->SleepSinceLastInteraction(config->RequestDelay);
        port->WriteBytes(request.data(), request.size());

        auto rc = port->ReadFrame(response.data(),
                                  response.size(),
                                  config->ResponseTimeout + config->FrameTimeout,
                                  config->FrameTimeout,
                                  ExpectNBytes(response.size()));
        if (rc > 0) {
            try {
                ModbusRTU::CheckResponse(request, response);
            } catch (const TInvalidCRCError &) {
                try {
                    port->SkipNoise();
                } catch (const std::exception & e) {
                    LOG(Warn) << "SkipNoise failed: " << e.what();
                }
                throw;
            } catch (const TMalformedResponseError &) {
                try {
                    port->SkipNoise();
                } catch (const std::exception & e) {
                    LOG(Warn) << "SkipNoise failed: " << e.what();
                }
                throw;
            }
        }
        return (rc > 0);
    }

    void WriteRegister(PPort port, uint8_t slaveId, PRegister reg, uint64_t value, int shift)
    {
        reg->Device()->DismissTmpCache();

        std::unique_ptr<TRegister, std::function<void(TRegister*)>> tmpCacheGuard(reg.get(), [](TRegister* reg){reg->Device()->DismissTmpCache();});

        LOG(Debug) << "modbus: write " << reg->Get16BitWidth() << " " << reg->TypeName << "(s) @ " << reg->Address <<
                " of device " << reg->Device()->ToString();

        std::vector<TWriteRequest> requests;
        ComposeWriteRequests(requests, reg, slaveId, value, shift);

        for (const auto & request: requests) {
            TWriteResponse response;
            if (ProcessRequest(port, request, response, reg->Device()->DeviceConfig()) > 0) {
                Modbus::ParseWriteResponse(PDU(response));
            } else {
                throw TSerialDeviceTransientErrorException("ReadFrame unknown error");
            }
        }

        reg->Device()->ApplyTmpCache();
    }

    void ReadRange(Modbus::PModbusRegisterRange& range, PPort port, uint8_t slaveId, int shift)
    {
        auto config = range->Device()->DeviceConfig();
        range->SetStatus(TRegisterRange::ST_UNKNOWN_ERROR);
        TReadRequest request;
        ComposeReadRequest(request, range, slaveId, shift);
        TReadResponse response(InferReadResponseSize(range));
        try {
            if (ProcessRequest(port, request, response, config)) {
                Modbus::ParseReadResponse(PDU(response), range);
                range->SetStatus(TRegisterRange::ST_OK);
            } else {
                throw TSerialDeviceTransientErrorException("ReadFrame unknown error");
            }
        } catch (const TSerialDevicePermanentRegisterException&) {
            range->SetStatus(TRegisterRange::ST_DEVICE_ERROR);
            throw;
        } catch (const TSerialDeviceTransientErrorException&) {
            range->SetStatus(TRegisterRange::ST_DEVICE_ERROR);
            throw;
        }
    }

    void ProcessRangeException(Modbus::PModbusRegisterRange& range, const char* msg)
    {
        for (auto& r: range->RegisterList()) {
            r->SetError();
        }
        LOG(Warn) << "ModbusRTU::ReadRegisterRange(): failed to read " << *range << ": " << msg;
    }

    // Remove unsupported registers on borders
    std::list<PRegister> RemoveUnsupportedFromBorders(std::list<PRegister> l) {
        auto it = std::find_if(l.begin(), l.end(), [](auto& r) {return r->IsAvailable();});
        l.erase(l.begin(), it);
        auto it2 = std::find_if(l.rbegin(), l.rend(), [](auto& r) {return r->IsAvailable();});
        l.erase(it2.base(), l.end());
        return l;
    }

    std::list<PRegisterRange> SplitRangeByHoles(const std::list<PRegister>& regs, bool onlyAvailable)
    {
        std::list<PRegisterRange> newRanges;
        std::list<PRegister> l;
        PRegister lastReg;
        for (auto& reg: regs) {
            if (!l.empty() && lastReg->Address + 1 != reg->Address) {
                newRanges.push_back(std::make_shared<Modbus::TModbusRegisterRange>(l, false));
                l.clear();
            }
            if (!onlyAvailable || reg->IsAvailable()) {
                lastReg = reg;
                l.push_back(reg);
            }
        }
        if (!l.empty()) {
            newRanges.push_back(std::make_shared<Modbus::TModbusRegisterRange>(l, false));
        }
        return newRanges;
    }

    std::list<PRegisterRange> ReadWholeRange(Modbus::PModbusRegisterRange& range, PPort port, uint8_t slaveId, int shift)
    {
        std::list<PRegisterRange> newRanges;
        try {
            ReadRange(range, port, slaveId, shift);
            auto l = RemoveUnsupportedFromBorders(range->RegisterList());
            if (!l.empty()) {
                auto newRange = std::make_shared<Modbus::TModbusRegisterRange>(l, range->HasHoles());
                newRange->SetStatus(range->GetStatus());
                newRanges.push_back(newRange);
            }
        } catch (const TSerialDeviceTransientErrorException& e) {
            ProcessRangeException(range, e.what());
            newRanges.push_back(range);
        } catch (const TSerialDevicePermanentRegisterException& e) {
            ProcessRangeException(range, e.what());
            if (range->HasHoles()) {
                LOG(Debug) << "Disabling holes feature for " << *range;
                return SplitRangeByHoles(range->RegisterList(), false);
            }
            range->SetReadOneByOne(true);
            newRanges.push_back(range);
        }
        return newRanges;
    }

    std::list<PRegisterRange> ReadOneByOne(Modbus::PModbusRegisterRange& range, PPort port, uint8_t slaveId, int shift)
    {
        range->SetStatus(TRegisterRange::ST_UNKNOWN_ERROR);
        std::list<Modbus::PModbusRegisterRange> subRanges;
        for (auto& reg: range->RegisterList()) {
            subRanges.push_back(std::make_shared<Modbus::TModbusRegisterRange>(std::list<PRegister>{ reg }, false));
        }
        for (auto& r: subRanges) {
            try {
                ReadRange(r, port, slaveId, shift);
            } catch (const TSerialDeviceTransientErrorException& e) {
                ProcessRangeException(range, e.what());
                return std::list<PRegisterRange>{range};
            } catch (const TSerialDevicePermanentRegisterException& e) {
                r->RegisterList().front()->SetAvailable(false);
                r->RegisterList().front()->SetError();
                LOG(Warn) << "Register " << r->RegisterList().front()->ToString() << " is not supported";
            }
        }
        range->SetStatus(TRegisterRange::ST_OK);
        return SplitRangeByHoles(range->RegisterList(), true);;
    }

    std::list<PRegisterRange> ReadRegisterRange(PPort port, uint8_t slaveId, PRegisterRange range, int shift)
    {
        auto modbus_range = std::dynamic_pointer_cast<Modbus::TModbusRegisterRange>(range);
        if (!modbus_range) {
            throw std::runtime_error("modbus range expected");
        }

        LOG(Debug) << "modbus: read " << *modbus_range;

        if (modbus_range->ShouldReadOneByOne()) {
            return ReadOneByOne(modbus_range, port, slaveId, shift);
        }
        return ReadWholeRange(modbus_range, port, slaveId, shift);
    }

    void WarnFailedRegisterSetup(const PDeviceSetupItem& item, const char* msg)
    {
        LOG(Warn) << "Register " << item->Register->ToString() << " setup failed: " << msg;
    }

    bool WriteSetupRegisters(PPort port, uint8_t slaveId, const std::vector<PDeviceSetupItem>& setupItems, int shift)
    {
        for (const auto& item : setupItems) {
            try {
                LOG(Info) << "Init: " << item->Name 
                        << ": setup register " << item->Register->ToString()
                        << " <-- " << item->Value;
                WriteRegister(port, slaveId, item->Register, item->Value, shift);
            } catch (const TSerialDevicePermanentRegisterException& e) {
                WarnFailedRegisterSetup(item, e.what());
            } catch (const TSerialDeviceTransientErrorException& e) {
                WarnFailedRegisterSetup(item, e.what());
                return false;
            }
        }
        return true;
    }
}  // modbus rtu protocol utilities
