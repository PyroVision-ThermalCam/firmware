# PyroVision VISA Interface

## Overview

The VISA (Virtual Instrument Software Architecture) interface provides standardized instrument control for the PyroVision thermal camera over TCP/IP. This implementation follows SCPI (Standard Commands for Programmable Instruments) conventions and IEEE 488.2 standards, enabling integration with test automation systems like LabVIEW, MATLAB, or Python-VISA.

## Features

- **TCP/IP Communication**: Standard VISA/SCPI port 5025
- **IEEE 488.2 Compliance**: Mandatory common commands implemented
- **SCPI Command Structure**: Hierarchical command tree with queries
- **Binary Data Transfer**: IEEE 488.2 definite length arbitrary block format
- **Error Management**: SCPI error queue with standard error codes
- **Thermal Camera Control**: Device-specific commands for imaging
- **Multiple Clients**: Support for up to 4 concurrent connections

## Network Configuration

- **Default Port**: 5025 (configurable in `visa_server.h`)
- **Protocol**: TCP/IP
- **Encoding**: ASCII text for commands, binary for image data
- **Termination**: Commands terminated with newline (`\n`)
- **Timeout**: 5000 ms (configurable)

## Command Reference

### IEEE 488.2 Common Commands

All commands are case-insensitive. Long form and short form are both supported (e.g., `SYSTem` or `SYST`).

#### `*IDN?` - Identification Query

Returns device identification string.

**Response Format**: `Manufacturer,Model,SerialNumber,FirmwareVersion`

**Example**:

```sh
*IDN?
PyroVision,ThermalCam-ESP32,0000001,1.0.0
```

#### `*RST` - Reset

Resets the device to default state.

**Example**:

```sh
*RST
```

#### `*CLS` - Clear Status

Clears the status data structures and error queue.

**Example**:

```sh
*CLS
```

#### `*OPC?` - Operation Complete Query

Returns 1 when all pending operations are complete.

**Response**: `1` or `0`

**Example**:

```sh
*OPC?
1
```

#### `*TST?` - Self-Test Query

Performs device self-test. Returns 0 for pass, non-zero for failure.

**Example**:

```sh
*TST?
0
```

### SCPI System Commands

#### `SYSTem:ERRor?` - Error Query

Returns the oldest error from the error queue.

**Response Format**: `ErrorCode,"ErrorMessage"`

**Example**:

```sh
SYST:ERR?
0,"No error"
```

#### `SYSTem:VERSion?` - SCPI Version Query

Returns the SCPI version supported.

**Response**: `1999.0` (SCPI-99)

**Example**:

```sh
SYST:VERS?
1999.0
```

### Sensor Commands

#### `SENSe:TEMPerature?` - Temperature Query

Returns the thermal sensor temperature in Celsius.

**Response**: Floating point temperature value

**Example**:

```sh
SENS:TEMP?
25.50
```

#### `SENSe:IMAGE:CAPTure` - Capture Image

Triggers image capture. Operation completes asynchronously.

**Example**:

```sh
SENS:IMG:CAPT
```

#### `SENSe:IMAGE:DATA?` - Image Data Query

Returns the captured image in binary format using IEEE 488.2 definite length arbitrary block format.

**Response Format**: `#<n><length><data>` where:

- `n` = number of digits in length
- `length` = byte count of data
- `data` = binary image data

**Example**:

```sh
SENS:IMG:DATA?
#41024<binary_data>
```

(Header `#41024` means 4 digits, 1024 bytes of data follows)

#### `SENSe:IMAGE:FORMat {JPEG|PNG|RAW}` - Set Image Format

Sets the format for captured images.

**Parameters**:

- `JPEG`: JPEG compressed format
- `PNG`: PNG compressed format
- `RAW`: Raw thermal data (16-bit)

**Example**:

```sh
SENS:IMG:FORM JPEG
```

#### `SENSe:IMAGE:PALette {IRON|GRAY|RAINBOW}` - Set Color Palette

Sets the color palette for thermal image visualization.

**Parameters**:

- `IRON`: Ironbow palette (thermal imaging standard)
- `GRAY`: Grayscale palette
- `RAINBOW`: Rainbow palette

**Example**:

```sh
SENS:IMG:PAL IRON
```

### Display Commands

#### `DISPlay:LED:STATe {ON|OFF|BLINK}` - LED Control

Controls the status LED.

**Parameters**:

- `ON`: LED continuously on
- `OFF`: LED off
- `BLINK`: LED blinking

**Example**:

```sh
DISP:LED:STAT ON
```

#### `DISPlay:LED:BRIGhtness <0-255>` - LED Brightness

Sets the LED brightness level.

**Parameters**: Integer value 0-255 (0=off, 255=maximum)

**Example**:

```sh
DISP:LED:BRIG 128
```

## Error Codes

Standard SCPI error codes are used:

| Code | Description |
|------|-------------|
| 0 | No error |
| -100 | Command error |
| -101 | Invalid character |
| -102 | Syntax error |
| -103 | Invalid separator |
| -104 | Data type error |
| -108 | Parameter not allowed |
| -109 | Missing parameter |
| -110 | Command header error |
| -113 | Undefined header |
| -200 | Execution error |
| -222 | Data out of range |
| -240 | Hardware error |
| -241 | Hardware missing |
| -300 | System error |
| -350 | Out of memory |
| -400 | Query error |

## Usage Examples

### Python with PyVISA

```python
import pyvisa

rm = pyvisa.ResourceManager()
# Connect to PyroVision camera
camera = rm.open_resource('TCPIP::192.168.4.1::5025::SOCKET')
camera.read_termination = '\n'
camera.write_termination = '\n'

# Identify device
idn = camera.query('*IDN?')
print(f"Connected to: {idn}")

# Set image format
camera.write('SENS:IMG:FORM JPEG')

# Set color palette
camera.write('SENS:IMG:PAL IRON')

# Capture image
camera.write('SENS:IMG:CAPT')

# Wait for operation complete
while camera.query('*OPC?') != '1':
    time.sleep(0.1)

# Get image data (binary)
camera.write('SENS:IMG:DATA?')
image_data = camera.read_raw()

# Parse IEEE 488.2 block format
# Skip header (#<n><length>)
header_start = image_data.find(b'#')
n = int(chr(image_data[header_start + 1]))
length = int(image_data[header_start + 2:header_start + 2 + n])
data = image_data[header_start + 2 + n:header_start + 2 + n + length]

# Save image
with open('thermal_image.jpg', 'wb') as f:
    f.write(data)

# Check for errors
error = camera.query('SYST:ERR?')
print(f"Error status: {error}")

camera.close()
```

### Python with Sockets

```python
import socket
import time

# Connect to camera
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('192.168.4.1', 5025))
sock.settimeout(5.0)

def send_command(sock, command):
    sock.sendall((command + '\n').encode())

def read_response(sock):
    data = b''
    while True:
        chunk = sock.recv(1024)
        data += chunk
        if b'\n' in chunk:
            break
    return data.decode().strip()

# Identify device
send_command(sock, '*IDN?')
response = read_response(sock)
print(f"Device: {response}")

# Get temperature
send_command(sock, 'SENS:TEMP?')
temp = read_response(sock)
print(f"Temperature: {temp}°C")

# Control LED
send_command(sock, 'DISP:LED:STAT BLINK')
send_command(sock, 'DISP:LED:BRIG 200')

sock.close()
```

### MATLAB

```matlab
% Connect to camera
t = tcpclient('192.168.4.1', 5025);
configureTerminator(t, 'LF');

% Identify device
writeline(t, '*IDN?');
idn = readline(t);
disp(['Connected to: ', idn]);

% Get temperature
writeline(t, 'SENS:TEMP?');
temp = str2double(readline(t));
fprintf('Temperature: %.2f°C\n', temp);

% Capture image
writeline(t, 'SENS:IMG:CAPT');

% Wait for completion
writeline(t, '*OPC?');
while str2double(readline(t)) ~= 1
    pause(0.1);
end

% Get image data
writeline(t, 'SENS:IMG:DATA?');
data = read(t);

% Parse IEEE 488.2 block format
header_idx = find(data == uint8('#'), 1);
n = data(header_idx + 1) - uint8('0');
length = str2double(char(data(header_idx + 2:header_idx + 1 + n)));
img_data = data(header_idx + 2 + n:header_idx + 1 + n + length);

% Save image
fid = fopen('thermal_image.jpg', 'wb');
fwrite(fid, img_data, 'uint8');
fclose(fid);

clear t;
```

### LabVIEW

1. Use **VISA TCP/IP Resource** with format: `TCPIP::192.168.4.1::5025::SOCKET`
2. Use **VISA Write** to send commands
3. Use **VISA Read** to receive responses
4. Set termination character to `\n` (0x0A)
5. For binary data, use **VISA Read** in binary mode

## Integration with Firmware

To integrate the VISA server with your application:

### 1. Include Headers

```cpp
#include "Manager/Network/VISA/visa_server.h"
```

### 2. Initialize Server

```cpp
// During application initialization
esp_err_t err = VISA_Server_Init();
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize VISA server");
}
```

### 3. Start Server

```cpp
// After network is connected
err = VISA_Server_Start();
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start VISA server");
}
```

### 4. Stop Server (if needed)

```cpp
// Before shutdown or network disconnect
VISA_Server_Stop();
```

### 5. Deinitialize

```cpp
// During application cleanup
VISA_Server_Deinit();
```

### CMake Integration

Add to your main component's `CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.cpp" ...
    INCLUDE_DIRS "." ...
    REQUIRES ... visa_interface
)
```

## Customization

### Adding New Commands

1. Define command handler in `visa_commands.cpp`:

```cpp
static int VISA_CMD_MY_COMMAND(char *Response, size_t MaxLen)
{
    // Implement command logic
    return snprintf(Response, MaxLen, "Result\n");
}
```

2. Add command to parser in `VISA_Commands_Execute()`:

```cpp
else if (strcasecmp(tokens[0], "MYCommand") == 0) {
    return VISA_CMD_MY_COMMAND(Response, MaxLen);
}
```

### Modifying Port

Edit `VISA_SERVER_PORT` in `visa_server.h`:

```cpp
#define VISA_SERVER_PORT    5555  // Custom port
```

### Adjusting Timeouts

Edit `VISA_SOCKET_TIMEOUT_MS` in `visa_server.h`:

```cpp
#define VISA_SOCKET_TIMEOUT_MS    10000  // 10 seconds
```

## Troubleshooting

### Cannot Connect

- Verify network connectivity: `ping 192.168.4.1`
- Check firewall settings
- Confirm server is running: Check logs for "VISA server listening"
- Verify correct port (default: 5025)

### Commands Not Responding

- Ensure commands are terminated with newline `\n`
- Check command syntax (case-insensitive)
- Query error queue: `SYST:ERR?`
- Enable debug logs in `visa_server.cpp` and `visa_commands.cpp`

### Binary Data Issues

- Verify IEEE 488.2 block format parsing
- Check data length matches header
- Use binary read mode, not text mode
- Ensure sufficient buffer size

### Performance Optimization

- Reduce `VISA_SOCKET_TIMEOUT_MS` for faster response
- Increase task stack size if needed
- Use binary format for large data transfers
- Consider caching frequently requested data

## Protocol Specification

### Command Format

```sh
<header>[<parameter>][\n]
```

- Header: Hierarchical command path separated by colons
- Parameter: Optional command argument
- Terminator: Newline character (0x0A)

### Query Format

```sh
<header>?[\n]
```

- Query: Header followed by question mark

### Response Format

```sh
<data>[\n]
```

- Text responses terminated with newline
- Binary data uses IEEE 488.2 block format

### IEEE 488.2 Block Format

```sh
#<n><length><data>
```

- `#`: Start marker
- `n`: Single digit, number of digits in length
- `length`: ASCII decimal length value
- `data`: Binary data bytes

**Example**: `#41024<1024 bytes>` means 4 digits, 1024 bytes follow
