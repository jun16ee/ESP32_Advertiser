# ESP32 BLE Advertiser - UART Controlled

This project configures the ESP32 as a BLE advertising sender. It receives text commands from a PC via UART and uses the Raw HCI interface to send BLE advertising packets containing precise countdown timers.

## System Internal Workflow

This section explains the lifecycle of a command from the moment it leaves the PC until it is broadcasted over Bluetooth.

### 1. Data Ingestion (UART Layer)

* **PC Side**: Sends a CSV formatted string ending with `\n` (e.g., `1,5000,0,FFFF,0,0,0\n`).
* **ESP32 ISR**: The `uart_event_task` receives the data via interrupt and passes it to `process_byte`.
* **Buffering**: Characters are stored in `packet_buf` until a newline `\n` is detected.

### 2. Parsing & Dispatch (Application Layer)

* **Parsing**: `sscanf` extracts the 7 parameters (Cmd, Delay, Prep, Mask, Data[3]).
* **Immediate ACK**: The ESP32 immediately sends `ACK:OK` back to the PC to confirm receipt.
* **Task Creation**: A `bt_sender_config_t` struct is created, and `bt_sender_add_task()` is called.

### 3. Scheduling (Sender Layer)

* **Slot Allocation**: The scheduler looks for an empty slot in the `s_tasks[16]` array.
* **Timestamping**: It calculates the **Absolute End Time**:
`end_time_us = now_us + delay_us`
* **Special Handling**:
* If **CANCEL (0x06)** is received: It immediately removes the target task from the scheduler (`bt_sender_remove_task`).
* If **CHECK (0x07)** is received: It spawns a `check_sequence_task` which waits 600ms (broadcast phase) before triggering the scan.



### 4. Broadcasting (Round-Robin Loop)

A dedicated FreeRTOS task `broadcast_scheduler_task` runs every **20ms**:

1. **Check Mode**: If `is_checking` is true (Scanning), it skips broadcasting.
2. **Cleanup**: Checks if any tasks have passed their `end_time_us`. If so, marks them inactive.
3. **Selection**: Uses a **Round-Robin** algorithm to pick the *next* active task in the list (ensuring fair bandwidth for multiple concurrent commands).
4. **Dynamic Calculation**: Calculates the fresh remaining time:
`remain = end_time_us - now_us - TX_OFFSET_US`
5. **HCI Transmission**: Sends the raw HCI packet to the Bluetooth Controller.
* Broadcasting lasts for approx. 10ms.



---

## Project Structure

```text
├── adv_esp/
│   ├── CMakeLists.txt      
│   └── main/
│       ├── CMakeLists.txt  
│       ├── main.c          # UART parsing, ACK sending, Task creation
│       ├── bt_sender.c     # HCI commands, Round-Robin Scheduler, Scanning logic
│       └── bt_sender.h     # Data structures
```

## UART Protocol

* **Baud Rate**: `115200`
* **Data bits**: 8, **Stop bits**: 1, **Parity**: None

### 1. Command Format (PC -> ESP32)

Commands must be sent as a CSV string terminated by a newline `\n`.

```text
cmd_in,delay_us,prep_led_us,target_mask,in_data[0],in_data[1],in_data[2]
```

| Parameter | Type | Description |
| --- | --- | --- |
| **cmd_in** | `int` | 4 bits Command_ID + 4 bits command type |
| **delay_us** | `long` | Execution delay in microseconds. |
| **prep_led_us** | `long` | Preparation LED duration. |
| **target_mask** | `hex` | 64-bit mask. |
| **in_data[0-2]** | `int` | Payload data (R, G, B or Target ID for Cancel). |

#### Supported Command Types (Low 4 bits of `cmd_in`)

| Command | Hex Code | Description | Data Parameter Usage |
| --- | --- | --- | --- |
| **PLAY** | `0x01` | Start timeline/playback. | None |
| **PAUSE** | `0x02` | Pause playback. | None |
| **STOP** | `0x03` | Stop and reset position. | None |
| **RELEASE** | `0x04` | Release memory/Unload. | None |
| **TEST** | `0x05` | Test Mode / LED Color. | `[R, G, B]` (0-255) or `[0,0,0]` for default pattern. |
| **CANCEL** | `0x06` | Cancel a pending command. | `[cmd_id]` (Use the ID returned by send_burst) |
| **CHECK** | `0x07` | Trigger Broadcast+Scan | None |
| **UPLOAD** | `0x08` | Enter System Upload Mode. | None |
| **RESET** | `0x09` | System Reboot. | None |

### 2. Response Format (ESP32 -> PC)

* **ACK**: `ACK:OK\n` (Sent immediately upon valid parse).
* **NAK**: `NAK:ParseError\n` or `NAK:Overflow\n`.
* **Check Result**: `FOUND:<target_id>,<cmd_id>,<cmd_type>,<delay>,<state>\n` (Streamed during scan).
* **Check End**: `CHECK_DONE\n`.

## BLE Advertising Packet Structure

The packet is constructed in `hci_cmd_send_ble_set_adv_data`. It uses Manufacturer Specific Data (0xFF).

| Offset | Length | Value | Description |
| --- | --- | --- | --- |
| **0** | 3 | `0xFF, 0xFF, 0xFF` | Manufacturer ID |
| **3** | 2 | `0x4C, 0x44` | unique code (LD) |
| **5** | 1 | `cmd_type` | Command Type |
| **6** | 8 | `target_mask` | 64-bit Target Mask |
| **14** | 4 | `delay_us` | **Dynamic** Remaining Time (Big Endian) |

The remaining bytes: 
* `PLAY`

| Offset | Length | Value | Description |
| --- | --- | --- | --- |
| **18** | 4 | `prep_led_us` | Preparation Time (Big Endian) |

* `TEST`

| Offset | Length | Value | Description |
| --- | --- | --- | --- |
| **18** | 3 | `data[3]` | Extra Data (e.g., RGB) |
| **21** | 1 | `0` | padding |

* `CANCEL`

| Offset | Length | Value | Description |
| --- | --- | --- | --- |
| **18** | 1 | `cmd_id` | the cmd id that you want to cancel |
| **19** | 3 | `0` | padding |

**Total Length**: 22 Bytes of Manufacturer Data.

## Operating Principles

### Round-Robin Scheduler

The sender maintains a list of up to **16 active tasks**.

* It does **not** broadcast all tasks simultaneously.
* Every **20ms**, it selects the *next* task in the list to broadcast.
* This allows the sender to handle multiple pending commands (e.g., a `PAUSE` for device A and a `PLAY` for device B) by interleaving their packets.

### The Check Sequence (Hybrid Mode)

When `CHECK` (0x07) is received:

1. **Broadcast Phase (600ms)**: The ESP32 adds the CHECK command to the scheduler. Receivers wake up and prepare to ACK.
2. **Scan Phase (2000ms)**:
* The ESP32 **stops** all advertising (Radio Blind Spot).
* It switches the HCI Controller to **Scan Mode**.
* It listens for packets with Type `0x07` (ACK) from receivers.
* Any `FOUND` devices are reported via UART.


3. **Resume**: After 2s, scanning stops, and the scheduler resumes broadcasting any remaining tasks.