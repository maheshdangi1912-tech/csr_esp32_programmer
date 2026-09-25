/*
 * spi.c - Production Driver Backend (Clean & High Speed)
 */

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "spi.h"

#define ESP32_DEFAULT_PORT     "COM5"
#define ESP32_BAUD             921600
#define READ_TIMEOUT_MS        500
#define MAX_XFER_BYTES         2048

#define SYNC_BYTE       0xA5
#define CMD_PING        0x01
#define CMD_SET_CLK     0x02
#define CMD_CS_LOW      0x03
#define CMD_CS_HIGH     0x04
#define CMD_XFER        0x05
#define CMD_GET_STATUS  0x06
#define RESP_OK_FLAG    0x80

static HANDLE hSerial = INVALID_HANDLE_VALUE;
static char err_buf_storage[256];
static char *err_buf = err_buf_storage;
static size_t err_buf_sz = sizeof(err_buf_storage);
static unsigned long current_clock = 50;
static char com_port_name[32] = ESP32_DEFAULT_PORT;
static int port_is_open = 0;

static uint8_t pending_write[MAX_XFER_BYTES];
static int pending_write_len = 0;

static void set_err(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err_buf, err_buf_sz, fmt, ap);
    va_end(ap);
}

static int raw_write(const void *data, size_t len) {
    DWORD written = 0;
    if (hSerial == INVALID_HANDLE_VALUE) return -1;
    if (!WriteFile(hSerial, data, (DWORD)len, &written, NULL) || written != len) {
        set_err("raw_write failed, GetLastError=%lu", GetLastError());
        return -1;
    }
    return 0;
}

static int raw_read_exact(void *dest, size_t len) {
    uint8_t *ptr = (uint8_t *)dest;
    size_t total = 0;
    DWORD start = GetTickCount();

    if (hSerial == INVALID_HANDLE_VALUE) return -1;

    while (total < len) {
        DWORD nread = 0;
        if (!ReadFile(hSerial, ptr + total, (DWORD)(len - total), &nread, NULL)) {
            set_err("raw_read failed, GetLastError=%lu", GetLastError());
            return -1;
        }
        total += nread;
        if (nread == 0) {
            if (GetTickCount() - start > READ_TIMEOUT_MS) {
                set_err("raw_read timeout");
                return -1;
            }
            Sleep(1);
        }
    }
    return 0;
}

static int send_command(uint8_t opcode, const void *payload, uint16_t len, void *rx_dest, uint16_t *rx_len) {
    uint8_t hdr[4] = {
        SYNC_BYTE,
        opcode,
        (uint8_t)(len >> 8),
        (uint8_t)(len & 0xFF)
    };

    if (raw_write(hdr, 4) < 0) return -1;
    if (len > 0 && payload != NULL) {
        if (raw_write(payload, len) < 0) return -1;
    }

    uint8_t b = 0;
    DWORD start = GetTickCount();
    while (b != SYNC_BYTE) {
        if (raw_read_exact(&b, 1) < 0) return -1;
        if (GetTickCount() - start > READ_TIMEOUT_MS) {
            set_err("Timeout hunting for SYNC_BYTE");
            return -1;
        }
    }

    uint8_t rest[3];
    if (raw_read_exact(rest, 3) < 0) return -1;

    uint8_t resp_opcode = rest[0];
    uint16_t payload_in = ((uint16_t)rest[1] << 8) | rest[2];

    if (resp_opcode != (opcode | RESP_OK_FLAG)) {
        set_err("Opcode mismatch: exp 0x%02X got 0x%02X", (opcode | RESP_OK_FLAG), resp_opcode);
        return -1;
    }

    if (rx_dest && rx_len) {
        if (payload_in > 0) {
            if (raw_read_exact(rx_dest, payload_in) < 0) return -1;
        }
        *rx_len = payload_in;
    } else if (payload_in > 0) {
        uint8_t discard[64];
        while (payload_in > 0) {
            uint16_t chunk = payload_in > sizeof(discard) ? sizeof(discard) : payload_in;
            raw_read_exact(discard, chunk);
            payload_in -= chunk;
        }
    }
    return 0;
}

void spi_set_err_buf(char *buf, size_t sz) {
    err_buf = buf;
    err_buf_sz = sz;
}

void spi_set_pinout(enum spi_pinouts pinout) { (void)pinout; }

int spi_set_interface(const char *intf) {
    if (intf && strnicmp(intf, "COM", 3) == 0) {
        strncpy(com_port_name, intf, sizeof(com_port_name) - 1);
        com_port_name[sizeof(com_port_name) - 1] = '\0';
    } else {
        char envbuf[32];
        DWORD n = GetEnvironmentVariableA("SPICOMPORT", envbuf, sizeof(envbuf));
        if (n > 0 && n < sizeof(envbuf)) {
            strncpy(com_port_name, envbuf, sizeof(com_port_name) - 1);
            com_port_name[sizeof(com_port_name) - 1] = '\0';
        }
    }
    return 0;
}

int spi_set_clock(unsigned long spi_clk) {
    if (spi_clk > 50) spi_clk = 50;
    if (spi_clk < 10) spi_clk = 10;
    current_clock = spi_clk;

    if (hSerial == INVALID_HANDLE_VALUE) return 0;

    uint32_t freq_hz = spi_clk * 1000UL;
    uint8_t payload[4] = {
        (uint8_t)(freq_hz >> 24),
        (uint8_t)(freq_hz >> 16),
        (uint8_t)(freq_hz >> 8),
        (uint8_t)(freq_hz & 0xFF)
    };
    return send_command(CMD_SET_CLK, payload, 4, NULL, NULL);
}

int spi_init(void) {
    char portpath[40];
    DCB dcb = {0};
    COMMTIMEOUTS timeouts = {0};

    snprintf(portpath, sizeof(portpath), "\\\\.\\%s", com_port_name);
    hSerial = CreateFileA(portpath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        set_err("Failed to open %s, err=%lu", com_port_name, GetLastError());
        return -1;
    }

    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hSerial, &dcb)) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
        return -1;
    }

    dcb.BaudRate = ESP32_BAUD;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

    if (!SetCommState(hSerial, &dcb)) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
        return -1;
    }

    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = READ_TIMEOUT_MS;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 500;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hSerial, &timeouts);

    Sleep(50);
    PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    int ping_ok = 0;
    for (int retry = 0; retry < 3; retry++) {
        if (send_command(CMD_PING, NULL, 0, NULL, NULL) == 0) {
            ping_ok = 1;
            break;
        }
        Sleep(50);
        PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
    }

    if (!ping_ok) {
        set_err("ESP32 binary ping failed");
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
        return -1;
    }

    spi_set_clock(current_clock);
    return 0;
}

int spi_deinit(void) {
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }
    port_is_open = 0;
    return 0;
}

/* Ports list branding */
int spi_get_port_list(struct spi_port **pportlist, int *pnports) {
    static struct spi_port fake_port;
    memset(&fake_port, 0, sizeof(fake_port));
    fake_port.vid = 0x0A12;
    fake_port.pid = 0x0001;
    strncpy(fake_port.manuf, "MD_spi_bridge", sizeof(fake_port.manuf) - 1);
    strncpy(fake_port.desc, "MD_spi_bridge", sizeof(fake_port.desc) - 1);
    strncpy(fake_port.serial, "1", sizeof(fake_port.serial) - 1);
    strncpy(fake_port.name, "MD_spi_bridge", sizeof(fake_port.name) - 1);
    *pportlist = &fake_port;
    *pnports = 1;
    return 0;
}

int spi_open(int nport) {
    (void)nport;
    port_is_open = 1;
    return 0;
}

int spi_isopen(void) { return port_is_open; }

int spi_close(void) {
    port_is_open = 0;
    return 0;
}

void spi_set_max_clock(unsigned long clk) { (void)clk; }

int spi_clock_slowdown(void) {
    if (current_clock > 10) current_clock /= 2;
    return spi_set_clock(current_clock);
}

unsigned long spi_get_max_clock(void) { return 50; }
unsigned long spi_get_clock(void) { return current_clock; }

int spi_xfer_begin(int get_status) {
    pending_write_len = 0;

    if (get_status) {
        uint8_t status_byte = 0;
        uint16_t got = 0;
        if (send_command(CMD_GET_STATUS, NULL, 0, &status_byte, &got) == 0 && got == 1) {
            return status_byte ? SPI_CPU_STOPPED : SPI_CPU_RUNNING;
        }
        return SPI_CPU_STOPPED;
    }

    send_command(CMD_CS_HIGH, NULL, 0, NULL, NULL);
    if (send_command(CMD_CS_LOW, NULL, 0, NULL, NULL) < 0) {
        return -1;
    }
    return 0;
}

int spi_xfer(int cmd, int iosize, void *buf, int size) {
    static uint8_t tx_bytes[MAX_XFER_BYTES];
    static uint8_t rx_bytes[MAX_XFER_BYTES];
    int byte_count = (iosize == 16) ? (size * 2) : size;
    int i;

    if ((cmd & SPI_XFER_WRITE) && !(cmd & SPI_XFER_READ)) {
        if (pending_write_len + byte_count > MAX_XFER_BYTES) return -1;
        if (iosize == 8) {
            memcpy(&pending_write[pending_write_len], buf, byte_count);
        } else {
            uint16_t *src = (uint16_t *)buf;
            for (i = 0; i < size; i++) {
                pending_write[pending_write_len + i * 2]     = (uint8_t)(src[i] >> 8);
                pending_write[pending_write_len + i * 2 + 1] = (uint8_t)(src[i] & 0xFF);
            }
        }
        pending_write_len += byte_count;
        return size;
    }

    int total_bytes = pending_write_len + byte_count;
    if (total_bytes > MAX_XFER_BYTES) return -1;

    if (pending_write_len > 0) {
        memcpy(tx_bytes, pending_write, pending_write_len);
    }

    if (cmd & SPI_XFER_WRITE) {
        if (iosize == 8) {
            memcpy(&tx_bytes[pending_write_len], buf, byte_count);
        } else {
            uint16_t *src = (uint16_t *)buf;
            for (i = 0; i < size; i++) {
                tx_bytes[pending_write_len + i * 2]     = (uint8_t)(src[i] >> 8);
                tx_bytes[pending_write_len + i * 2 + 1] = (uint8_t)(src[i] & 0xFF);
            }
        }
    } else {
        memset(&tx_bytes[pending_write_len], 0x00, byte_count);
    }

    uint16_t rx_got = 0;
    if (send_command(CMD_XFER, tx_bytes, (uint16_t)total_bytes, rx_bytes, &rx_got) < 0) {
        return -1;
    }

    uint8_t *read_ptr = &rx_bytes[pending_write_len];
    pending_write_len = 0;

    if (cmd & SPI_XFER_READ) {
        if (iosize == 8) {
            uint8_t *dest = (uint8_t *)buf;
            for (i = 0; i < size; i++) dest[i] = read_ptr[i];
        } else {
            uint16_t *dest = (uint16_t *)buf;
            for (i = 0; i < size; i++) {
                dest[i] = ((uint16_t)read_ptr[i * 2] << 8) | (uint16_t)read_ptr[i * 2 + 1];
            }
        }
    }
    return size;
}

int spi_xfer_end(void) {
    if (pending_write_len > 0) {
        uint16_t dummy = 0;
        send_command(CMD_XFER, pending_write, (uint16_t)pending_write_len, NULL, &dummy);
        pending_write_len = 0;
    }
    return send_command(CMD_CS_HIGH, NULL, 0, NULL, NULL);
}