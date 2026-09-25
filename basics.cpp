/*
 * basics.cpp - Verified BCCMD Transport Wrapper for PSTool GUI
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>

#include "dllmain.h"
#include "spifns.h"
#include "spi.h"
#include "compat.h"
#include "logging.h"

extern "C" {

#define VARLIST_SPIPORT          0
#define VARLIST_SPIMUL           1
#define VARLIST_SPICLOCK         3
#define VARLIST_SPICMDBITS       4
#define VARLIST_SPICMDREADBITS   5
#define VARLIST_SPICMDWRITEBITS  6
#define VARLIST_SPIMAXCLOCK      7
#define VARLIST_FTDI_BASE_CLOCK  8
#define VARLIST_FTDI_LOG_LEVEL   9
#define VARLIST_FTDI_LOG_FILE    10
#define VARLIST_FTDI_PINOUT      11
#define VARLIST_FTDI_INTERFACE   12

const SPIVARDEF g_pVarList[] = {
    {"SPIPORT", "1", 1},
    {"SPIMUL", "0", 0},
    {"SPISHIFTPERIOD", "0", 0},
    {"SPICLOCK", "50", 0},
    {"SPICMDBITS", "0", 0},
    {"SPICMDREADBITS", "0", 0},
    {"SPICMDWRITEBITS", "0", 0},
    {"SPIMAXCLOCK", "50", 0},
    {"FTDI_BASE_CLOCK", "1000000", 0},
    {"FTDI_LOG_LEVEL", "warn", 0},
    {"FTDI_LOG_FILE", "stderr", 0},
    {"FTDI_PINOUT", "0", 0},
    {"FTDI_INTERFACE", "A", 0}
};

int g_nSpiPort = 1;
char g_szErrorString[256] = "No error";
unsigned int g_nError = SPIERR_NO_ERROR;
unsigned short g_nErrorAddress = 0;
static uint32_t spifns_api_version = 0;

static int g_lookup_table_completed = 0;

#define STREAM      ((spifns_stream_t)0)
#define NSTREAMS    1

#define SET_ERROR(n, s) do { \
        g_nError = (n); \
        strncpy(g_szErrorString, (s), sizeof(g_szErrorString) - 1); \
        g_szErrorString[sizeof(g_szErrorString) - 1] = '\0'; \
    } while (0)

static void log_debug(const char *fmt, ...) {
    FILE *fp = fopen("C:\\Users\\Public\\csr_debug.log", "a");
    if (!fp) fp = fopen("C:\\csr_debug.log", "a");
    if (!fp) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(fp, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    fprintf(fp, "\n");
    va_end(ap);

    fflush(fp);
    fclose(fp);
}

static int spifns_sequence_setvar(const char *szName, const char *szValue);

DLLEXPORT void spifns_getvarlist(const SPIVARDEF **ppList, unsigned int *pnCount) {
    *ppList = g_pVarList;
    *pnCount = sizeof(g_pVarList) / sizeof(*g_pVarList);
}

static int spifns_init_vars_from_env(void) {
    const char *var, *val;
    for (unsigned int ii = 0; ii < sizeof(g_pVarList) / sizeof(g_pVarList[0]); ii++) {
        var = g_pVarList[ii].szName;
        val = getenv(var);
        if (val != NULL && val[0] != '\0') {
            if (spifns_sequence_setvar(var, val) != 0)
                return -1;
        }
    }
    return 0;
}

DLLEXPORT int spifns_init() {
    spi_set_err_buf(g_szErrorString, sizeof(g_szErrorString));
    g_lookup_table_completed = 0;

    if (spifns_init_vars_from_env() < 0)
        return -1;

    if (!spifns_api_version) {
        if (pttrans_api_version)
            spifns_api_version = pttrans_api_version;
        else
            spifns_api_version = SPIFNS_API_1_4;
    }

    if (spi_init() < 0)
        return -1;

    log_debug("=== Session Started (Transport MD_spi_bridge Ready) ===");
    return 0;
}

DLLEXPORT const char * spifns_getvar(const char *szName) {
    if (!szName) return "";
    if (stricmp(szName, "SPIPORT") == 0) {
        static char szReturn[20];
        snprintf(szReturn, sizeof(szReturn), "%d", g_nSpiPort);
        return szReturn;
    } else if (stricmp(szName, "SPIMUL") == 0) {
        return "-1";
    } else if (stricmp(szName, "SPISHIFTPERIOD") == 0) {
        return "1";
    } else if (stricmp(szName, "SPICLOCK") == 0) {
        static char szReturn[64];
        snprintf(szReturn, sizeof(szReturn), "%lu", spi_get_clock());
        return szReturn;
    } else if (stricmp(szName, "SPIMAXCLOCK") == 0) {
        static char szReturn[24];
        snprintf(szReturn, sizeof(szReturn), "%lu", spi_get_max_clock());
        return szReturn;
    }
    return "";
}

DLLEXPORT unsigned int spifns_get_last_error(unsigned short *pnErrorAddress, const char **pszErrorString) {
    if (pnErrorAddress) *pnErrorAddress = g_nErrorAddress;
    if (pszErrorString) *pszErrorString = g_szErrorString;
    return g_nError;
}

DLLEXPORT void spifns_clear_last_error(void) {
    static const char szError[] = "No error";
    memcpy(g_szErrorString, szError, sizeof(szError));
    g_nErrorAddress = 0;
    g_nError = SPIERR_NO_ERROR;
}

DLLEXPORT void spifns_set_debug_callback(spifns_debug_callback pCallback) {
    (void)pCallback;
}

DLLEXPORT uint32_t spifns_get_version() {
    if (!spifns_api_version) {
        if (pttrans_api_version)
            spifns_api_version = pttrans_api_version;
        else
            spifns_api_version = SPIFNS_API_1_4;
    }
    return spifns_api_version;
}

DLLEXPORT void spifns_close() {
    log_debug("=== Session Closed ===");
    spi_close();
}

DLLEXPORT void spifns_chip_select(int nChip) {
    (void)nChip;
}

DLLEXPORT const char* spifns_command(const char *szCmd) {
    if (szCmd && stricmp(szCmd, "SPISLOWER") == 0) {
        spi_clock_slowdown();
    }
    return 0;
}

DLLEXPORT void spifns_enumerate_ports(spifns_enumerate_ports_callback pCallback, void *pData) {
    pCallback(1, "MD_spi_bridge", pData);
}

static bool spifns_sequence_setvar_spiport(int nPort) {
    if (spi_isopen()) spi_close();
    if (spi_open(nPort - 1) < 0) return false;
    g_nSpiPort = nPort;
    return true;
}

static int spifns_sequence_write(unsigned short nAddress, unsigned short nLength, unsigned short *pnInput) {
    char datastr[128] = "";
    if (pnInput && nLength > 0) {
        int plen = nLength > 12 ? 12 : nLength;
        char *p = datastr;
        for (int i = 0; i < plen; i++) {
            p += snprintf(p, sizeof(datastr) - (p - datastr), "%04X ", pnInput[i]);
        }
    }
    log_debug("[WRITE] addr=0x%04X len=%d: %s", nAddress, nLength, datastr);

    uint8_t outbuf1[] = {
        0x02,
        (uint8_t)(nAddress >> 8),
        (uint8_t)(nAddress & 0xff),
    };

    if (!spi_isopen()) {
        SET_ERROR(SPIERR_NO_LPT_PORT_SELECTED, "No SPI port open");
        return 1;
    }

    if (spi_xfer_begin(0) < 0) {
        SET_ERROR(SPIERR_READ_FAILED, "Unable to begin write transfer");
        return 1;
    }

    if (spi_xfer(SPI_XFER_WRITE, 8, outbuf1, 3) < 0) {
        spi_xfer_end();
        SET_ERROR(SPIERR_READ_FAILED, "Unable to start write");
        return 1;
    }

    if (spi_xfer(SPI_XFER_WRITE, 16, pnInput, nLength) < 0) {
        spi_xfer_end();
        SET_ERROR(SPIERR_READ_FAILED, "Unable to write buffer");
        return 1;
    }

    if (spi_xfer_end() < 0) {
        SET_ERROR(SPIERR_READ_FAILED, "Unable to end write transfer");
        return 1;
    }

    return 0;
}

static int spifns_sequence_setvar(const char *szName, const char *szValue) {
    if (!szName || !szValue) return 1;
    long nValue = strtol(szValue, 0, 0);

    for (unsigned int i = 0; i < (sizeof(g_pVarList) / sizeof(*g_pVarList)); i++) {
        if (stricmp(szName, g_pVarList[i].szName) == 0) {
            switch (i) {
            case VARLIST_SPIPORT:
                if (!spifns_sequence_setvar_spiport(nValue)) return 1;
                break;
            case VARLIST_SPICLOCK:
            case VARLIST_SPIMAXCLOCK:
                if (nValue > 0) spi_set_clock((unsigned long)nValue);
                break;
            case VARLIST_FTDI_INTERFACE:
                if (spi_set_interface(szValue) < 0) return 1;
                break;
            }
        }
    }
    return 0;
}

static int spifns_sequence_read(unsigned short nAddress, unsigned short nLength, unsigned short *pnOutput) {
    if (nAddress == 0xFF9A) {
        if (pnOutput && nLength > 0) {
            pnOutput[0] = 0x002A;
        }
        return 0;
    }

    uint8_t outbuf[] = {
        0x03,
        (uint8_t)(nAddress >> 8),
        (uint8_t)(nAddress & 0xff),
    };
    uint8_t inbuf1[2] = {0, 0};

    if (!spi_isopen()) {
        SET_ERROR(SPIERR_NO_LPT_PORT_SELECTED, "No SPI port open");
        return 1;
    }

    int read_ok = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (spi_xfer_begin(0) < 0) continue;

        if (spi_xfer(SPI_XFER_WRITE, 8, outbuf, 3) < 0) {
            spi_xfer_end();
            continue;
        }

        if (spi_xfer(SPI_XFER_READ, 8, inbuf1, 2) < 0) {
            spi_xfer_end();
            continue;
        }

        if (inbuf1[0] == 0x03) {
            read_ok = 1;
            break;
        }

        spi_xfer_end();
        Sleep(1);
    }

    if (!read_ok) {
        g_nErrorAddress = nAddress;
        SET_ERROR(SPIERR_READ_FAILED, "Unable to start read (invalid control data)");
        return 1;
    }

    if (spi_xfer(SPI_XFER_READ, 16, pnOutput, nLength) < 0) {
        spi_xfer_end();
        SET_ERROR(SPIERR_READ_FAILED, "Unable to read payload buffer");
        return 1;
    }

    if (spi_xfer_end() < 0) {
        SET_ERROR(SPIERR_READ_FAILED, "Unable to end transfer");
        return 1;
    }

    if (nAddress == 0xFFE9 || nAddress == 0xF8DD || nAddress >= 0xBF00) {
        g_lookup_table_completed = 1;
    }

    if (nAddress >= 0xBF00 || nAddress == 0x006B || (nAddress >= 0xDFE0 && nAddress <= 0xDFFF)) {
        char datastr[128] = "";
        if (pnOutput && nLength > 0) {
            int plen = nLength > 12 ? 12 : nLength;
            char *p = datastr;
            for (int i = 0; i < plen; i++) {
                p += snprintf(p, sizeof(datastr) - (p - datastr), "%04X ", pnOutput[i]);
            }
        }
        log_debug("[READ ] addr=0x%04X len=%d: %s", nAddress, nLength, datastr);
    }

    return 0;
}

DLLEXPORT int spifns_sequence(SPISEQ *pSequence, unsigned int nCount) {
    int nRetval = 0;
    while (nCount--) {
        switch (pSequence->nType) {
        case SPISEQ::TYPE_READ:
            if (spifns_sequence_read(pSequence->rw.nAddress, pSequence->rw.nLength, pSequence->rw.pnData) != 0)
                nRetval = 1;
            break;
        case SPISEQ::TYPE_WRITE:
            if (spifns_sequence_write(pSequence->rw.nAddress, pSequence->rw.nLength, pSequence->rw.pnData) != 0)
                nRetval = 1;
            break;
        case SPISEQ::TYPE_SETVAR:
            if (spifns_sequence_setvar(pSequence->setvar.szName, pSequence->setvar.szValue) != 0)
                nRetval = 1;
            break;
        default:
            break;
        }
        pSequence++;
    }
    return nRetval;
}

DLLEXPORT int spifns_stream_sequence(spifns_stream_t stream, SPISEQ_1_4 *pSequence, int nCount) {
    (void)stream;
    int nRetval = 0;
    while (nCount--) {
        switch (pSequence->nType) {
        case SPISEQ_1_4::TYPE_READ:
            if (spifns_sequence_read(pSequence->rw.nAddress, pSequence->rw.nLength, pSequence->rw.pnData) != 0)
                nRetval = 1;
            break;
        case SPISEQ_1_4::TYPE_WRITE:
            if (spifns_sequence_write(pSequence->rw.nAddress, pSequence->rw.nLength, pSequence->rw.pnData) != 0)
                nRetval = 1;
            break;
        case SPISEQ_1_4::TYPE_SETVAR:
            if (spifns_sequence_setvar(pSequence->setvar.szName, pSequence->setvar.szValue) != 0)
                nRetval = 1;
            break;
        default:
            break;
        }
        pSequence++;
    }
    return nRetval;
}

DLLEXPORT int spifns_bluecore_xap_stopped() {
    int ret = g_lookup_table_completed ? SPIFNS_XAP_RUNNING : SPIFNS_XAP_STOPPED;
    return ret;
}

DLLEXPORT int spifns_stream_init(spifns_stream_t *p_stream) {
    int rc = spifns_init();
    if (rc == 0) *p_stream = STREAM;
    return rc;
}

DLLEXPORT void spifns_stream_close(spifns_stream_t stream) {
    if (SPIFNS_STREAMS_EQUAL(stream, STREAM)) spifns_close();
}

DLLEXPORT unsigned int spifns_count_streams(void) {
    return NSTREAMS;
}

DLLEXPORT const char* spifns_stream_command(spifns_stream_t stream, const char *command) {
    (void)stream;
    return spifns_command(command);
}

DLLEXPORT const char* spifns_stream_getvar(spifns_stream_t stream, const char *var) {
    (void)stream;
    return spifns_getvar(var);
}

DLLEXPORT void spifns_stream_chip_select(spifns_stream_t stream, int which) {
    (void)stream;
    spifns_chip_select(which);
}

DLLEXPORT int spifns_stream_bluecore_xap_stopped(spifns_stream_t stream) {
    (void)stream;
    return spifns_bluecore_xap_stopped();
}

DLLEXPORT int spifns_get_last_error32(uint32_t *addr, const char ** buf) {
    unsigned short saddr;
    int rc = spifns_get_last_error(&saddr, buf);
    if (addr) *addr = saddr;
    return rc;
}

DLLEXPORT void spifns_stream_set_debug_callback(spifns_stream_t stream, spifns_debug_callback fn, void *pvcontext) {
    (void)stream;
    (void)pvcontext;
    spifns_set_debug_callback(fn);
}

DLLEXPORT int spifns_stream_get_device_id(spifns_stream_t stream, char *buf, size_t length) {
    (void)stream;
    snprintf(buf, length, "MD_spi_bridge");
    return 0;
}

DLLEXPORT int spifns_stream_lock(spifns_stream_t stream, uint32_t timeout) {
    (void)stream;
    (void)timeout;
    return 0;
}

DLLEXPORT void spifns_stream_unlock(spifns_stream_t stream) {
    (void)stream;
}

} /* extern "C" */