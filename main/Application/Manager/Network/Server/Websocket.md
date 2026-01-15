# WebSocket Communication

## Overview

Simplified WebSocket protocol for real-time thermal image streaming. **All clients receive JPEG format only** for maximum compatibility and efficiency.

## Features

- **Real-time Streaming** - JPEG frames at configurable FPS (1-30)
- **Multi-Client Support** - Up to 4 simultaneous connections
- **Simple Protocol** - Flat JSON structure, no nested types
- **Robust** - Single encoding per frame, proper error handling

## Protocol

### Message Format

**All messages use flat JSON:**

```json
{
  "cmd": "command_name",
  "data": { }
}
```

### Commands (Client → Server)

#### Start Streaming

```json
{
  "cmd": "start",
  "data": {
    "fps": 8
  }
}
```

**Response:**
```json
{
  "cmd": "started",
  "data": {
    "status": "ok",
    "fps": 8
  }
}
```

#### Stop Streaming

```json
{
  "cmd": "stop"
}
```

**Response:**
```json
{
  "cmd": "stopped",
  "data": {
    "status": "ok"
  }
}
```

#### Subscribe to Telemetry

```json
{
  "cmd": "subscribe",
  "data": {
    "interval": 1000
  }
}
```

**Response:**
```json
{
  "cmd": "subscribed",
  "data": {
    "status": "ok"
  }
}
```

#### Unsubscribe from Telemetry

```json
{
  "cmd": "unsubscribe"
}
```

### Events (Server → Client)

#### Telemetry Event

```json
{
  "cmd": "telemetry",
  "data": {
    "temp": 25.5
  }
}
```

#### Binary Frame Data

Image frames are sent as **binary WebSocket messages (OPCODE 0x02)** containing JPEG data.

## Python Client Example

```python
import asyncio
import websockets
import json
from PIL import Image
from io import BytesIO

async def stream():
    uri = "ws://192.168.1.100/ws"
    
    async with websockets.connect(uri, 
                                   ping_interval=20, 
                                   ping_timeout=60,
                                   max_size=None) as ws:
        # Start streaming at 8 FPS
        await ws.send(json.dumps({
            "cmd": "start",
            "data": {"fps": 8}
        }))
        
        # Receive frames
        while True:
            message = await ws.recv()
            
            if isinstance(message, bytes):
                # JPEG frame
                img = Image.open(BytesIO(message))
                img.show()  # or process as needed
            else:
                # JSON response/event
                data = json.loads(message)
                print(f"Event: {data}")

asyncio.run(stream())
```

## Architecture

### System Components

```sh
┌─────────────┐              ┌──────────────────┐               ┌──────────────┐
│  GUI Task   │              │  Broadcast Task  │               │  WS Clients  │
│  (CPU 0)    │              │    (CPU 1)       │               │              │
└─────┬───────┘              └────────┬─────────┘               └──────┬───────┘
      │                               │                                │
      │ 1. Frame Update               │                                │
      │    (fast, <10ms)              │                                │
      │                               │                                │
      │ 2. NotifyFrameReady()         │                                │
      │    (non-blocking)             │                                │
      ├──────────────────────────────>│                                │
      │                               │                                │
      │ 3. Continue GUI               │ 4. JPEG Encode                 │
      │    (no blocking!)             │    (200-400ms)                 │
      │                               │                                │
      │                               │ 5. Send Frame                  │
      │                               ├───────────────────────────────>│
      │                               │    (TCP Send)                  │
      │                               │                                │
      │                               │ 6. Wait for                    │
      │                               │    next frame                  │
      │                               │                                │
```

### Task Configuration

**WebSocket Broadcast Task** (`websocket_handler.cpp`)

- **Task Name:** `WS_Broadcast`
- **Stack Size:** 4096 Bytes
- **Priority:** 5 (lower than GUI Task priority 10)
- **CPU Affinity:** Core 1 (Network CPU)
- **Queue Length:** 1 (only latest frame notification)

**Key Design Decisions:**

1. **Separate Task** - Prevents GUI blocking during encoding/transmission
2. **CPU Pinning** - Network operations on Core 1, GUI rendering on Core 0
3. **Queue-based** - Frame notifications are queued, not frames themselves
4. **Mutex Optimization** - Released during I/O operations to prevent deadlocks

## Protocol

### WebSocket Endpoint

```sh
ws://<device-ip>/ws
```

### Message Format

All messages use JSON format with the following structure:

```json
{
  "type": "command|response|event",
  "name": "message_name",
  "payload": { }
}
```

### Commands

#### Stream Control

**Start Streaming:**

```json
{
  "type": "command",
  "name": "stream.start",
  "payload": {
    "format": "jpeg|png|raw",
    "fps": 8
  }
}
```

- **format**: Image encoding format
  - `jpeg` - JPEG compressed (default, recommended)
  - `png` - PNG compressed (larger, lossless)
  - `raw` - Raw RGB888 data (fastest, largest)
- **fps**: Frame rate (1-30, default: 8)

**Stop Streaming:**

```json
{
  "type": "command",
  "name": "stream.stop"
}
```

#### Telemetry Control

**Subscribe to Telemetry:**

```json
{
  "type": "command",
  "name": "telemetry.subscribe",
  "payload": {
    "interval_ms": 1000
  }
}
```

- **interval_ms**: Update interval in milliseconds (minimum: 100ms)

**Unsubscribe from Telemetry:**

```json
{
  "type": "command",
  "name": "telemetry.unsubscribe"
}
```

#### LED Control

**Set LED State:**

```json
{
  "type": "command",
  "name": "led.set",
  "payload": {
    "state": "on|off|blink",
    "blink_ms": 500
  }
}
```

### Responses

All commands receive an acknowledgment response:

```json
{
  "type": "response",
  "name": "stream.start",
  "payload": {
    "status": "streaming|stopped|subscribed|unsubscribed"
  }
}
```

### Events

#### Telemetry Event

Sent periodically when subscribed:

```json
{
  "type": "event",
  "name": "telemetry",
  "payload": {
    "sensor_temp_c": 25.5,
    "core_temp_c": 45.2
  }
}
```

#### Binary Frame Data

Image frames are sent as binary WebSocket messages (OPCODE 0x02):

- **JPEG Format:** Binary JPEG data
- **PNG Format:** Binary PNG data
- **RAW Format:** RGB888 pixel data (width × height × 3 bytes)

## API Reference

### Initialization

```cpp
/* Initialize WebSocket handler */
esp_err_t WebSocket_Handler_Init(const Network_Server_Config_t *p_Config);

/* Register handler with HTTP server */
esp_err_t WebSocket_Handler_Register(httpd_handle_t p_ServerHandle);

/* Start broadcast task */
esp_err_t WebSocket_Handler_StartTask(void);
```

### Frame Broadcasting

```cpp
/* Set thermal frame data source */
void WebSocket_Handler_SetThermalFrame(Network_Thermal_Frame_t *p_Frame);

/* Notify that new frame is ready (non-blocking) */
esp_err_t WebSocket_Handler_NotifyFrameReady(void);
```

**Usage Example:**

```cpp
// In GUI task, after frame update:
if (WebSocket_Handler_HasClients()) {
    WebSocket_Handler_NotifyFrameReady();  // Non-blocking notification
}
```

### Telemetry Broadcasting

```cpp
/* Broadcast telemetry to subscribed clients */
esp_err_t WebSocket_Handler_BroadcastTelemetry(void);
```

### Client Management

```cpp
/* Get number of connected clients */
uint8_t WebSocket_Handler_GetClientCount(void);

/* Check if any clients are connected */
bool WebSocket_Handler_HasClients(void);

/* Send ping to all clients */
esp_err_t WebSocket_Handler_PingAll(void);
```

### Cleanup

```cpp
/* Stop broadcast task */
void WebSocket_Handler_StopTask(void);

/* Deinitialize handler */
void WebSocket_Handler_Deinit(void);
```

## JavaScript Client Example

```javascript
const ws = new WebSocket('ws://192.168.1.100/ws');

ws.binaryType = 'arraybuffer';

ws.onopen = () => {
    // Start streaming at 8 FPS
    ws.send(JSON.stringify({
        cmd: 'start',
        data: { fps: 8 }
    }));
};

ws.onmessage = (event) => {
    if (event.data instanceof ArrayBuffer) {
        // JPEG frame
        const blob = new Blob([event.data], { type: 'image/jpeg' });
        const url = URL.createObjectURL(blob);
        document.getElementById('image').src = url;
    } else {
        // JSON event
        const data = JSON.parse(event.data);
        console.log('Event:', data);
    }
};

ws.onerror = (error) => {
    console.error('WebSocket error:', error);
};

ws.onclose = () => {
    console.log('WebSocket closed');
};
```

## Architecture

### Broadcast Task

- **Single Encoding**: Each frame is encoded once and sent to all clients
- **CPU Pinning**: Runs on Core 1 (network CPU) to avoid GUI blocking
- **Retry Logic**: 3 retries with backoff on send failures
- **Auto-Cleanup**: Failed clients are automatically removed

### Performance

- **Encoding**: ~200-400ms per JPEG frame
- **FPS Limit**: Client-specific (1-30 FPS)
- **Memory**: Single encoded frame shared across all clients
- **Latency**: <100ms from frame capture to client receipt
