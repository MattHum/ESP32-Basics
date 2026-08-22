#ifndef SHTC3_H
#define SHTC3_H

#include <Wire.h>
#include <stdint.h>

#define SHTC3_ADDR 0x70

struct SHTC3Data {
    float temperature;
    float humidity;
    bool valid;
};

class SHTC3 {
public:
    bool begin(TwoWire &wire = Wire);
    SHTC3Data read();
    bool isConnected();

private:
    TwoWire *_wire;
    bool wake();
    bool sleep();
    bool softReset();
    uint8_t calcCRC(uint8_t data[], uint8_t len);
};

#endif
