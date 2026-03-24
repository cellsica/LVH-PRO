# Mission: Core-Bridge IPC Foundation (Shared Memory & Synchronization)

## Current Status
- `LVH-Bridge` is successfully loading VST3 plugins and displaying GUIs as a separate process.
- We need a robust, low-latency communication channel between `LVH-PRO` (Core) and `LVH-Bridge` (Child).

## Task Briefing
Implement the IPC foundation using JUCE's capabilities and/or custom shared memory for performance.

### 1. Architectural Decision
Pro-version requires low-latency audio processing.
- **Control/Messaging**: Named Pipes or Sockets (via `juce::InterprocessConnection`).
- **Audio/MIDI Data**: Shared Memory (via `juce::SharedResourcePointer` or raw OS shared memory) with lock-free ring buffers.

### 2. Immediate Steps for Shizuku
1. **Establish Control Connection**:
   - Create a basic handshake between Core and Bridge.
2. **Implement Simple MIDI Send**:
   - Core should send MIDI messages to the Bridge.
   - For now, let the Bridge just log the received MIDI to verify connection.
3. **Data Sharing Structure**:
   - Define the shared memory layout for Sample Rate, Buffer Size, and Audio Buffers.

## Files to touch
- `Source/Main.cpp` (Core side logic)
- `Source/BridgeMain.cpp` (Bridge side logic)
- (New) `Source/IPCManager.h/cpp` (Shared logic)

---
Kaede: 「しずくちゃん、あとは任せたよ！なべの期待に応えてね。」
