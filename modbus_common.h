#pragma once

#include "serial_port.h"
#include "serial_config.h"
#include "register.h"
#include <ostream>
#include <bitset>
#include <array>


namespace Modbus  // modbus protocol common utilities
{
    enum RegisterType {
        REG_HOLDING = 0, // used for 'setup' regsb
        REG_INPUT,
        REG_COIL,
        REG_DISCRETE,
    };

    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> reg_list, PDeviceConfig deviceConfig, bool debug);
};  // modbus protocol common utilities

namespace ModbusRTU // modbus rtu protocol utilities
{
    void WriteRegister(PAbstractSerialPort port, uint8_t slaveId, PRegister reg, uint64_t value, int shift = 0);

    void ReadRegisterRange(PAbstractSerialPort port, uint8_t slaveId, PRegisterRange range, int shift = 0);
};  // modbus rtu protocol utilities
