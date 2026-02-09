/*
** EditInterface
** A MUI 3.8 application for editing Roadshow network interface files
** on AmigaOS 3.x (68k)
**
** Usage: EditInterface <interface_name>
**
** The interface file is expected at:
**   DEVS:NetInterfaces/<interface_name>
**
** Compiled with SAS/C 6.x or VBCC for AmigaOS 3.x
*/

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <libraries/mui.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <libraries/gadtools.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Version string                                                     */
/* ------------------------------------------------------------------ */
static const char VER[] = "$VER: EditInterface 1.2 (09.02.2026) by Renaud Schweingruber";

/* ------------------------------------------------------------------ */
/* MUI support macros                                                 */
/* ------------------------------------------------------------------ */
#ifndef MAKE_ID
#define MAKE_ID(a,b,c,d) ((ULONG)(a)<<24|(ULONG)(b)<<16|(ULONG)(c)<<8|(ULONG)(d))
#endif

/* ------------------------------------------------------------------ */
/* Library bases                                                      */
/* ------------------------------------------------------------------ */
struct Library    *MUIMasterBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;
extern struct DosLibrary *DOSBase;

/* ------------------------------------------------------------------ */
/* Configure mode constants                                           */
/* ------------------------------------------------------------------ */
#define CFG_NONE     0
#define CFG_DHCP     1
#define CFG_AUTO     2
#define CFG_FASTAUTO 3

static const char *ConfigureLabels[] =
{
    "Static IP",
    "DHCP",
    "Auto (ZeroConf)",
    "Fast Auto (Wireless)",
    NULL
};

/* ------------------------------------------------------------------ */
/* Filter mode constants                                              */
/* ------------------------------------------------------------------ */
#define FLT_NONE       0
#define FLT_LOCAL      1
#define FLT_IPANDARP   2
#define FLT_EVERYTHING 3

static const char *FilterLabels[] =
{
    "Disabled",
    "Local",
    "IP and ARP",
    "Everything",
    NULL
};

/* ------------------------------------------------------------------ */
/* Interface data structure                                           */
/* ------------------------------------------------------------------ */
#define MAX_STR 256

struct InterfaceConfig
{
    char device[MAX_STR];
    LONG unit;
    char address[MAX_STR];
    char netmask[MAX_STR];
    LONG configureMode;  /* index into ConfigureLabels */
    BOOL debug;
    LONG ipRequests;
    LONG writeRequests;
    LONG filterMode;     /* index into FilterLabels */
    BOOL requiresInitDelay;
};

/* ------------------------------------------------------------------ */
/* Network-wide settings (from routes and name_resolution)            */
/* ------------------------------------------------------------------ */
struct NetworkConfig
{
    char gateway[MAX_STR];
    char dns1[MAX_STR];
    char dns2[MAX_STR];
    char dns3[MAX_STR];
    char domain[MAX_STR];
};

/* ------------------------------------------------------------------ */
/* GUI object pointers                                                */
/* ------------------------------------------------------------------ */
static APTR app          = NULL;
static APTR win          = NULL;
static APTR str_Device   = NULL;
static APTR str_Unit     = NULL;
static APTR str_Address  = NULL;
static APTR str_Netmask  = NULL;
static APTR cyc_Configure = NULL;
static APTR str_Gateway  = NULL;
static APTR str_DNS1     = NULL;
static APTR str_DNS2     = NULL;
static APTR str_DNS3     = NULL;
static APTR str_Domain   = NULL;
static APTR chk_Debug    = NULL;
static APTR str_IpReq    = NULL;
static APTR str_WriteReq = NULL;
static APTR cyc_Filter   = NULL;
static APTR chk_InitDelay = NULL;
static APTR btn_Save     = NULL;
static APTR btn_Cancel   = NULL;
static APTR mnu_About    = NULL;
static APTR mnu_AboutMUI = NULL;
static APTR mnu_Quit     = NULL;

/* ------------------------------------------------------------------ */
/* Globals                                                            */
/* ------------------------------------------------------------------ */
static char g_FilePath[512];
static char g_InterfaceName[128];
static struct InterfaceConfig g_Config;
static struct NetworkConfig g_NetConfig;
static BOOL g_IsLive = FALSE;

static const char *ROUTES_PATH = "DEVS:Internet/routes";
static const char *NAMERES_PATH = "DEVS:Internet/name_resolution";

/* ------------------------------------------------------------------ */
/* Utility: trim whitespace from both ends                            */
/* ------------------------------------------------------------------ */
static char *TrimString(char *s)
{
    char *end;

    while (*s == ' ' || *s == '\t')
        s++;

    if (*s == '\0')
        return s;

    end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r'))
        end--;
    *(end + 1) = '\0';

    return s;
}

/* ------------------------------------------------------------------ */
/* Parse the interface configuration file                             */
/* ------------------------------------------------------------------ */
static BOOL ParseConfigFile(const char *path, struct InterfaceConfig *cfg)
{
    FILE *fh;
    char line[512];
    char *key, *value, *eq;

    /* Set defaults */
    memset(cfg, 0, sizeof(*cfg));
    cfg->unit          = 0;
    cfg->configureMode = CFG_NONE;
    cfg->filterMode    = FLT_NONE;
    cfg->debug         = FALSE;
    cfg->ipRequests    = 32;
    cfg->writeRequests = 32;
    cfg->requiresInitDelay = FALSE;

    fh = fopen(path, "r");
    if (!fh)
    {
        return FALSE;
    }

    while (fgets(line, sizeof(line), fh))
    {
        key = TrimString(line);

        /* Skip empty lines */
        if (*key == '\0')
            continue;

        /* Handle commented lines: only parse #address= and #netmask= */
        if (*key == '#')
        {
            char *ck = TrimString(key + 1);
            char *ceq = strchr(ck, '=');

            if (ceq)
            {
                *ceq = '\0';
                ck = TrimString(ck);

                if (stricmp(ck, "address") == 0 && cfg->address[0] == '\0')
                    strncpy(cfg->address, TrimString(ceq + 1), MAX_STR - 1);
                else if (stricmp(ck, "netmask") == 0 && cfg->netmask[0] == '\0')
                    strncpy(cfg->netmask, TrimString(ceq + 1), MAX_STR - 1);
            }

            continue;
        }

        /* Find '=' separator */
        eq = strchr(key, '=');
        if (!eq)
            continue;

        *eq = '\0';
        value = TrimString(eq + 1);
        key   = TrimString(key);

        if (stricmp(key, "device") == 0)
        {
            strncpy(cfg->device, value, MAX_STR - 1);
        }
        else if (stricmp(key, "unit") == 0)
        {
            cfg->unit = atol(value);
        }
        else if (stricmp(key, "address") == 0)
        {
            strncpy(cfg->address, value, MAX_STR - 1);
        }
        else if (stricmp(key, "netmask") == 0)
        {
            strncpy(cfg->netmask, value, MAX_STR - 1);
        }
        else if (stricmp(key, "configure") == 0)
        {
            if (stricmp(value, "dhcp") == 0)
                cfg->configureMode = CFG_DHCP;
            else if (stricmp(value, "auto") == 0)
                cfg->configureMode = CFG_AUTO;
            else if (stricmp(value, "fastauto") == 0)
                cfg->configureMode = CFG_FASTAUTO;
        }
        else if (stricmp(key, "debug") == 0)
        {
            cfg->debug = (stricmp(value, "yes") == 0);
        }
        else if (stricmp(key, "iprequests") == 0)
        {
            cfg->ipRequests = atol(value);
        }
        else if (stricmp(key, "writerequests") == 0)
        {
            cfg->writeRequests = atol(value);
        }
        else if (stricmp(key, "filter") == 0)
        {
            if (stricmp(value, "local") == 0)
                cfg->filterMode = FLT_LOCAL;
            else if (stricmp(value, "ipandarp") == 0)
                cfg->filterMode = FLT_IPANDARP;
            else if (stricmp(value, "everything") == 0)
                cfg->filterMode = FLT_EVERYTHING;
        }
        else if (stricmp(key, "requiresinitdelay") == 0)
        {
            cfg->requiresInitDelay = (stricmp(value, "yes") == 0);
        }
    }

    fclose(fh);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Parse DEVS:Internet/routes                                         */
/* ------------------------------------------------------------------ */
static void ParseRoutesFile(const char *path, struct NetworkConfig *net)
{
    FILE *fh;
    char line[512];
    char *p;

    net->gateway[0] = '\0';

    fh = fopen(path, "r");
    if (!fh)
        return;

    while (fgets(line, sizeof(line), fh))
    {
        p = TrimString(line);

        /* Skip empty lines and comments */
        if (*p == '\0' || *p == '#')
            continue;

        /* Look for "DEFAULT <gateway>" or "default <gateway>" */
        if (strnicmp(p, "DEFAULT", 7) == 0 && (p[7] == ' ' || p[7] == '\t'))
        {
            p = TrimString(p + 7);
            strncpy(net->gateway, p, MAX_STR - 1);
            break;
        }
    }

    fclose(fh);
}

/* ------------------------------------------------------------------ */
/* Write DEVS:Internet/routes                                         */
/* ------------------------------------------------------------------ */
static BOOL WriteRoutesFile(const char *path, const struct NetworkConfig *net)
{
    FILE *fh;

    fh = fopen(path, "w");
    if (!fh)
        return FALSE;

    fprintf(fh, "# Routes configuration\n");
    fprintf(fh, "# Edited by EditInterface 1.2\n");
    fprintf(fh, "\n");

    if (net->gateway[0] != '\0')
        fprintf(fh, "DEFAULT %s\n", net->gateway);
    else
        fprintf(fh, "#DEFAULT localhost\n");

    fclose(fh);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Parse DEVS:Internet/name_resolution                                */
/* ------------------------------------------------------------------ */
static void ParseNameResFile(const char *path, struct NetworkConfig *net)
{
    FILE *fh;
    char line[512];
    char *p;
    int dnsCount = 0;

    net->dns1[0] = '\0';
    net->dns2[0] = '\0';
    net->dns3[0] = '\0';
    net->domain[0] = '\0';

    fh = fopen(path, "r");
    if (!fh)
        return;

    while (fgets(line, sizeof(line), fh))
    {
        p = TrimString(line);

        /* Skip empty lines and comments */
        if (*p == '\0' || *p == '#')
            continue;

        /* nameserver <ip> */
        if (strnicmp(p, "nameserver", 10) == 0 && (p[10] == ' ' || p[10] == '\t'))
        {
            p = TrimString(p + 10);
            if (dnsCount == 0)
                strncpy(net->dns1, p, MAX_STR - 1);
            else if (dnsCount == 1)
                strncpy(net->dns2, p, MAX_STR - 1);
            else if (dnsCount == 2)
                strncpy(net->dns3, p, MAX_STR - 1);
            dnsCount++;
        }
        /* domain <name> */
        else if (strnicmp(p, "domain", 6) == 0 && (p[6] == ' ' || p[6] == '\t'))
        {
            p = TrimString(p + 6);
            strncpy(net->domain, p, MAX_STR - 1);
        }
    }

    fclose(fh);
}

/* ------------------------------------------------------------------ */
/* Write DEVS:Internet/name_resolution                                */
/* ------------------------------------------------------------------ */
static BOOL WriteNameResFile(const char *path, const struct NetworkConfig *net)
{
    FILE *fh;

    fh = fopen(path, "w");
    if (!fh)
        return FALSE;

    fprintf(fh, "# Name resolution configuration\n");
    fprintf(fh, "# Edited by EditInterface 1.2\n");
    fprintf(fh, "\n");

    if (net->dns1[0] != '\0')
        fprintf(fh, "nameserver %s\n", net->dns1);
    else
        fprintf(fh, "#nameserver\n");

    if (net->dns2[0] != '\0')
        fprintf(fh, "nameserver %s\n", net->dns2);
    else
        fprintf(fh, "#nameserver\n");

    if (net->dns3[0] != '\0')
        fprintf(fh, "nameserver %s\n", net->dns3);
    else
        fprintf(fh, "#nameserver\n");

    fprintf(fh, "\n");

    if (net->domain[0] != '\0')
        fprintf(fh, "domain %s\n", net->domain);
    else
        fprintf(fh, "#domain\n");

    fclose(fh);
    return TRUE;
}
static BOOL WriteConfigFile(const char *path, const struct InterfaceConfig *cfg)
{
    FILE *fh;

    fh = fopen(path, "w");
    if (!fh)
        return FALSE;

    /* Header */
    fprintf(fh, "# Configuration for interface: %s\n", g_InterfaceName);
    fprintf(fh, "# Edited by EditInterface 1.2\n");
    fprintf(fh, "\n");

    /* Device (mandatory) */
    fprintf(fh, "# The device name is mandatory\n");
    fprintf(fh, "device=%s\n", cfg->device);
    fprintf(fh, "\n");

    /* Unit */
    if (cfg->unit != 0)
        fprintf(fh, "unit=%ld\n", (long)cfg->unit);
    else
        fprintf(fh, "#unit=0\n");
    fprintf(fh, "\n");

    /* IP Configuration */
    fprintf(fh, "# IP address configuration\n");
    if (cfg->address[0] != '\0')
    {
        if (cfg->configureMode == CFG_DHCP)
            fprintf(fh, "#address=%s\n", cfg->address);
        else
            fprintf(fh, "address=%s\n", cfg->address);
    }
    else
        fprintf(fh, "#address=\n");

    if (cfg->netmask[0] != '\0')
    {
        if (cfg->configureMode == CFG_DHCP)
            fprintf(fh, "#netmask=%s\n", cfg->netmask);
        else
            fprintf(fh, "netmask=%s\n", cfg->netmask);
    }
    else
        fprintf(fh, "#netmask=\n");

    fprintf(fh, "\n");

    /* Configure mode */
    switch (cfg->configureMode)
    {
        case CFG_DHCP:
            fprintf(fh, "configure=dhcp\n");
            break;
        case CFG_AUTO:
            fprintf(fh, "configure=auto\n");
            break;
        case CFG_FASTAUTO:
            fprintf(fh, "configure=fastauto\n");
            break;
        default:
            fprintf(fh, "#configure=dhcp\n");
            break;
    }
    fprintf(fh, "\n");

    /* Debug */
    if (cfg->debug)
        fprintf(fh, "debug=yes\n");
    else
        fprintf(fh, "#debug=yes\n");
    fprintf(fh, "\n");

    /* Buffers */
    fprintf(fh, "# Network buffer configuration\n");
    if (cfg->ipRequests != 32)
        fprintf(fh, "iprequests=%ld\n", (long)cfg->ipRequests);
    else
        fprintf(fh, "#iprequests=32\n");

    if (cfg->writeRequests != 32)
        fprintf(fh, "writerequests=%ld\n", (long)cfg->writeRequests);
    else
        fprintf(fh, "#writerequests=32\n");
    fprintf(fh, "\n");

    /* Filter */
    fprintf(fh, "# Traffic capture filter\n");
    switch (cfg->filterMode)
    {
        case FLT_LOCAL:
            fprintf(fh, "filter=local\n");
            break;
        case FLT_IPANDARP:
            fprintf(fh, "filter=ipandarp\n");
            break;
        case FLT_EVERYTHING:
            fprintf(fh, "filter=everything\n");
            break;
        default:
            fprintf(fh, "#filter=local\n");
            break;
    }
    fprintf(fh, "\n");

    /* Requires init delay */
    if (cfg->requiresInitDelay)
        fprintf(fh, "requiresinitdelay=yes\n");
    else
        fprintf(fh, "requiresinitdelay=no\n");

    fclose(fh);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Helper: check if a string looks like an IPv4 address (x.x.x.x)    */
/* ------------------------------------------------------------------ */
static BOOL IsIPv4Address(const char *s)
{
    int dots = 0;
    const char *p = s;

    if (!s || !*s)
        return FALSE;

    while (*p)
    {
        if (*p == '.')
            dots++;
        else if (*p < '0' || *p > '9')
            return FALSE;
        p++;
    }

    return (BOOL)(dots == 3);
}

/* ------------------------------------------------------------------ */
/* Query live network parameters by parsing ShowNetStatus output      */
/* If the interface is up and DHCP is configured, fill in the actual  */
/* IP address, netmask, gateway and DNS from the running stack.       */
/*                                                                    */
/* Two commands are used:                                              */
/*   ShowNetStatus          -> gateway, DNS (summary view)            */
/*   ShowNetStatus <ifname> -> IP address, netmask (detail view)      */
/*                                                                    */
/* Parsing is locale-independent: we look for '=' separators and      */
/* the interface name in quotes rather than matching label text.       */
/* ------------------------------------------------------------------ */
static BOOL QueryLiveInterface(const char *ifname,
                               struct InterfaceConfig *cfg,
                               struct NetworkConfig *net)
{
    FILE *fh;
    char line[512];
    char *p, *q;
    char tmpFile[256];
    char cmd[512];
    char ifCheck[128];
    int lineAfterFound;
    BOOL found = FALSE;

    /* Only query if configure mode is DHCP */
    if (cfg->configureMode != CFG_DHCP)
        return FALSE;

    /* ---- Phase 1: ShowNetStatus <ifname> for IP and netmask ---- */
    strcpy(tmpFile, "T:EditInterface_sns.tmp");
    sprintf(cmd, "ShowNetStatus %s >%s", ifname, tmpFile);

    if (SystemTagList(cmd, NULL) == 0)
    {
        fh = fopen(tmpFile, "r");
        if (fh)
        {
            BOOL gotAddress = FALSE;

            while (fgets(line, sizeof(line), fh))
            {
                char *eq = strchr(line, '=');
                char *val;

                if (!eq)
                    continue;

                val = TrimString(eq + 1);

                if (!gotAddress)
                {
                    /* Look for the first line where value is a bare IP */
                    if (IsIPv4Address(val))
                    {
                        strncpy(cfg->address, val, MAX_STR - 1);
                        g_IsLive = TRUE;
                        found = TRUE;
                        gotAddress = TRUE;
                    }
                }
                else
                {
                    /* The very next bare IP line is the netmask */
                    if (IsIPv4Address(val))
                    {
                        strncpy(cfg->netmask, val, MAX_STR - 1);
                        break;
                    }
                }
            }

            fclose(fh);
        }
    }

    if (!found)
    {
        DeleteFile(tmpFile);
        return FALSE;
    }

    /* ---- Phase 2: ShowNetStatus (summary) for gateway and DNS ---- */
    sprintf(cmd, "ShowNetStatus >%s", tmpFile);

    if (SystemTagList(cmd, NULL) != 0)
    {
        DeleteFile(tmpFile);
        return found;
    }

    fh = fopen(tmpFile, "r");
    if (!fh)
    {
        DeleteFile(tmpFile);
        return found;
    }

    /* Prepare interface name check string: 'V4Net' */
    sprintf(ifCheck, "'%s'", ifname);
    lineAfterFound = 0;

    while (fgets(line, sizeof(line), fh))
    {
        char *eq;

        p = TrimString(line);
        eq = strchr(p, '=');
        if (!eq)
            continue;

        if (lineAfterFound == 0)
        {
            /* Look for the line containing our interface name in quotes */
            if (strstr(p, ifCheck))
                lineAfterFound = 1;
        }
        else if (lineAfterFound == 1)
        {
            /* First '=' line after address: gateway */
            char *gw = TrimString(eq + 1);
            strncpy(net->gateway, gw, MAX_STR - 1);
            lineAfterFound = 2;
        }
        else if (lineAfterFound == 2)
        {
            /* Second '=' line after address: DNS servers */
            char *dns = TrimString(eq + 1);
            char *comma;
            int count = 0;

            while (*dns && count < 3)
            {
                comma = strchr(dns, ',');
                if (comma)
                    *comma = '\0';

                q = TrimString(dns);

                if (count == 0)
                    strncpy(net->dns1, q, MAX_STR - 1);
                else if (count == 1)
                    strncpy(net->dns2, q, MAX_STR - 1);
                else if (count == 2)
                    strncpy(net->dns3, q, MAX_STR - 1);

                count++;

                if (comma)
                    dns = comma + 1;
                else
                    break;
            }

            break; /* Done parsing */
        }
    }

    fclose(fh);
    DeleteFile(tmpFile);

    return found;
}

/* ------------------------------------------------------------------ */
/* Enable or disable network fields based on configure mode           */
/* In DHCP mode, address/netmask/gateway/DNS/domain are read-only     */
/* ------------------------------------------------------------------ */
static void UpdateNetFieldsState(ULONG mode)
{
    BOOL disabled = (mode == CFG_DHCP) ? TRUE : FALSE;

    set(str_Address, MUIA_Disabled, disabled);
    set(str_Netmask, MUIA_Disabled, disabled);
    set(str_Gateway, MUIA_Disabled, disabled);
    set(str_DNS1,    MUIA_Disabled, disabled);
    set(str_DNS2,    MUIA_Disabled, disabled);
    set(str_DNS3,    MUIA_Disabled, disabled);
    set(str_Domain,  MUIA_Disabled, disabled);
}

/* ------------------------------------------------------------------ */
/* Populate GUI fields from config structure                          */
/* ------------------------------------------------------------------ */
static void ConfigToGUI(const struct InterfaceConfig *cfg)
{
    char buf[32];

    set(str_Device,    MUIA_String_Contents, (ULONG)cfg->device);

    sprintf(buf, "%ld", (long)cfg->unit);
    set(str_Unit,      MUIA_String_Contents, (ULONG)buf);

    set(str_Address,   MUIA_String_Contents, (ULONG)cfg->address);
    set(str_Netmask,   MUIA_String_Contents, (ULONG)cfg->netmask);
    set(cyc_Configure, MUIA_Cycle_Active,    cfg->configureMode);
    set(chk_Debug,     MUIA_Selected,        cfg->debug);

    sprintf(buf, "%ld", (long)cfg->ipRequests);
    set(str_IpReq,     MUIA_String_Contents, (ULONG)buf);

    sprintf(buf, "%ld", (long)cfg->writeRequests);
    set(str_WriteReq,  MUIA_String_Contents, (ULONG)buf);

    set(cyc_Filter,    MUIA_Cycle_Active,    cfg->filterMode);
    set(chk_InitDelay, MUIA_Selected,        cfg->requiresInitDelay);

    set(str_Gateway,   MUIA_String_Contents, (ULONG)g_NetConfig.gateway);
    set(str_DNS1,      MUIA_String_Contents, (ULONG)g_NetConfig.dns1);
    set(str_DNS2,      MUIA_String_Contents, (ULONG)g_NetConfig.dns2);
    set(str_DNS3,      MUIA_String_Contents, (ULONG)g_NetConfig.dns3);
    set(str_Domain,    MUIA_String_Contents, (ULONG)g_NetConfig.domain);

    /* Update field states based on configure mode */
    UpdateNetFieldsState(cfg->configureMode);
}

/* ------------------------------------------------------------------ */
/* Read GUI fields back into config structure                         */
/* ------------------------------------------------------------------ */
static void GUIToConfig(struct InterfaceConfig *cfg)
{
    char *s;
    LONG val;

    get(str_Device,    MUIA_String_Contents, &s);
    strncpy(cfg->device, s ? s : "", MAX_STR - 1);

    get(str_Unit,      MUIA_String_Contents, &s);
    cfg->unit = s ? atol(s) : 0;

    get(str_Address,   MUIA_String_Contents, &s);
    strncpy(cfg->address, s ? s : "", MAX_STR - 1);

    get(str_Netmask,   MUIA_String_Contents, &s);
    strncpy(cfg->netmask, s ? s : "", MAX_STR - 1);

    get(cyc_Configure, MUIA_Cycle_Active,    &val);
    cfg->configureMode = val;

    get(chk_Debug,     MUIA_Selected,        &val);
    cfg->debug = val ? TRUE : FALSE;

    get(str_IpReq,     MUIA_String_Contents, &s);
    cfg->ipRequests = s ? atol(s) : 32;

    get(str_WriteReq,  MUIA_String_Contents, &s);
    cfg->writeRequests = s ? atol(s) : 32;

    get(cyc_Filter,    MUIA_Cycle_Active,    &val);
    cfg->filterMode = val;

    get(chk_InitDelay, MUIA_Selected,        &val);
    cfg->requiresInitDelay = val ? TRUE : FALSE;

    get(str_Gateway,   MUIA_String_Contents, &s);
    strncpy(g_NetConfig.gateway, s ? s : "", MAX_STR - 1);

    get(str_DNS1,      MUIA_String_Contents, &s);
    strncpy(g_NetConfig.dns1, s ? s : "", MAX_STR - 1);

    get(str_DNS2,      MUIA_String_Contents, &s);
    strncpy(g_NetConfig.dns2, s ? s : "", MAX_STR - 1);

    get(str_DNS3,      MUIA_String_Contents, &s);
    strncpy(g_NetConfig.dns3, s ? s : "", MAX_STR - 1);

    get(str_Domain,    MUIA_String_Contents, &s);
    strncpy(g_NetConfig.domain, s ? s : "", MAX_STR - 1);
}

/* ------------------------------------------------------------------ */
/* Register (tab) labels                                              */
/* ------------------------------------------------------------------ */
static const char *TabLabels[] =
{
    "General",
    "Advanced",
    NULL
};

/* ------------------------------------------------------------------ */
/* Build the MUI GUI                                                  */
/* ------------------------------------------------------------------ */
static BOOL CreateGUI(void)
{
    static char winTitle[256];

    sprintf(winTitle, "EditInterface: %s", g_InterfaceName);

    app = ApplicationObject,
        MUIA_Application_Title,       (ULONG)"EditInterface",
        MUIA_Application_Version,     (ULONG)VER + 6,
        MUIA_Application_Copyright,   (ULONG)"\xA9 2026 Renaud Schweingruber",
        MUIA_Application_Author,      (ULONG)"Renaud Schweingruber",
        MUIA_Application_Description, (ULONG)"Roadshow interface config editor",
        MUIA_Application_Base,        (ULONG)"EDITINTERFACE",

        MUIA_Application_Menustrip, MenustripObject,
            Child, MenuObject,
                MUIA_Menu_Title, (ULONG)"Project",
                Child, mnu_About = MenuitemObject,
                    MUIA_Menuitem_Title, (ULONG)"About...",
                    MUIA_Menuitem_Shortcut, (ULONG)"?",
                End,
                Child, mnu_AboutMUI = MenuitemObject,
                    MUIA_Menuitem_Title, (ULONG)"About MUI...",
                End,
                Child, MenuitemObject,
                    MUIA_Menuitem_Title, (ULONG)NM_BARLABEL,
                End,
                Child, mnu_Quit = MenuitemObject,
                    MUIA_Menuitem_Title, (ULONG)"Quit",
                    MUIA_Menuitem_Shortcut, (ULONG)"Q",
                End,
            End,
        End,

        SubWindow, win = WindowObject,
            MUIA_Window_Title,  (ULONG)winTitle,
            MUIA_Window_ID,     MAKE_ID('M','A','I','N'),
            MUIA_Window_Width,  MUIV_Window_Width_Visible(15),

            WindowContents, VGroup,

                /* ===== Tabbed Register ===== */
                Child, RegisterGroup(TabLabels),

                    /* ===== Tab 1: General ===== */
                    Child, VGroup,

                        /* Device Group */
                        Child, VGroup, GroupFrameT("Device"),

                            Child, HGroup,
                                Child, Label2("Device:"),
                                Child, str_Device = StringObject, StringFrame,
                                    MUIA_String_MaxLen, MAX_STR,
                                    MUIA_ShortHelp, (ULONG)"SANA-II device driver name (e.g. v4net.device)",
                                End,
                            End,

                            Child, HGroup,
                                Child, Label2("Unit:"),
                                Child, str_Unit = StringObject, StringFrame,
                                    MUIA_String_Accept, (ULONG)"0123456789",
                                    MUIA_String_MaxLen, 8,
                                    MUIA_ShortHelp, (ULONG)"Device unit number (default: 0)",
                                End,
                            End,

                        End, /* Device group */

                        /* IP Configuration Group */
                        Child, VGroup, GroupFrameT("IP Configuration"),

                            Child, HGroup,
                                Child, Label2("Configure:"),
                                Child, cyc_Configure = CycleObject,
                                    MUIA_Cycle_Entries, (ULONG)ConfigureLabels,
                                    MUIA_ShortHelp, (ULONG)"IP address assignment method",
                                End,
                            End,

                            Child, HGroup,
                                Child, Label2("Address:"),
                                Child, str_Address = StringObject, StringFrame,
                                    MUIA_String_Accept, (ULONG)"0123456789.",
                                    MUIA_String_MaxLen, 16,
                                    MUIA_ShortHelp, (ULONG)"Static IPv4 address (e.g. 192.168.10.10)",
                                End,
                            End,

                            Child, HGroup,
                                Child, Label2("Netmask:"),
                                Child, str_Netmask = StringObject, StringFrame,
                                    MUIA_String_Accept, (ULONG)"0123456789.",
                                    MUIA_String_MaxLen, 16,
                                    MUIA_ShortHelp, (ULONG)"Subnet mask (e.g. 255.255.255.0)",
                                End,
                            End,

                        End, /* IP group */

                        /* Network Group */
                        Child, VGroup, GroupFrameT("Network"),

                            Child, HGroup,
                                Child, Label2("Gateway:"),
                                Child, str_Gateway = StringObject, StringFrame,
                                    MUIA_String_Accept, (ULONG)"0123456789.",
                                    MUIA_String_MaxLen, 16,
                                    MUIA_ShortHelp, (ULONG)"Default gateway IP address",
                                End,
                            End,

                            Child, HGroup,
                                Child, Label2("DNS 1:"),
                                Child, str_DNS1 = StringObject, StringFrame,
                                    MUIA_String_Accept, (ULONG)"0123456789.",
                                    MUIA_String_MaxLen, 16,
                                    MUIA_ShortHelp, (ULONG)"Primary DNS server",
                                End,
                            End,

                            Child, HGroup,
                                Child, Label2("DNS 2:"),
                                Child, str_DNS2 = StringObject, StringFrame,
                                    MUIA_String_Accept, (ULONG)"0123456789.",
                                    MUIA_String_MaxLen, 16,
                                    MUIA_ShortHelp, (ULONG)"Secondary DNS server",
                                End,
                            End,

                            Child, HGroup,
                                Child, Label2("DNS 3:"),
                                Child, str_DNS3 = StringObject, StringFrame,
                                    MUIA_String_Accept, (ULONG)"0123456789.",
                                    MUIA_String_MaxLen, 16,
                                    MUIA_ShortHelp, (ULONG)"Tertiary DNS server",
                                End,
                            End,

                            Child, HGroup,
                                Child, Label2("Domain:"),
                                Child, str_Domain = StringObject, StringFrame,
                                    MUIA_String_MaxLen, MAX_STR,
                                    MUIA_ShortHelp, (ULONG)"Default domain name for lookups",
                                End,
                            End,

                        End, /* Network group */

                    End, /* Tab 1: General */

                    /* ===== Tab 2: Advanced ===== */
                    Child, VGroup,

                        /* Performance Group */
                        Child, VGroup, GroupFrameT("Performance"),

                            Child, HGroup,
                                Child, Label2("IP Requests:"),
                                Child, str_IpReq = StringObject, StringFrame,
                                    MUIA_String_Accept, (ULONG)"0123456789",
                                    MUIA_String_MaxLen, 8,
                                    MUIA_ShortHelp, (ULONG)"Number of inbound buffers (default: 32)",
                                End,
                            End,

                            Child, HGroup,
                                Child, Label2("Write Requests:"),
                                Child, str_WriteReq = StringObject, StringFrame,
                                    MUIA_String_Accept, (ULONG)"0123456789",
                                    MUIA_String_MaxLen, 8,
                                    MUIA_ShortHelp, (ULONG)"Number of outbound buffers (default: 32)",
                                End,
                            End,

                        End, /* Performance group */

                        /* Diagnostics Group */
                        Child, VGroup, GroupFrameT("Diagnostics"),

                            Child, HGroup,
                                Child, Label2("Debug:"),
                                Child, HGroup,
                                    Child, chk_Debug = MUI_MakeObject(MUIO_Checkmark, NULL),
                                    Child, HSpace(0),
                                End,
                            End,

                            Child, HGroup,
                                Child, Label2("Filter:"),
                                Child, cyc_Filter = CycleObject,
                                    MUIA_Cycle_Entries, (ULONG)FilterLabels,
                                    MUIA_ShortHelp, (ULONG)"Traffic capture filter for monitoring",
                                End,
                            End,

                        End, /* Diagnostics group */

                        /* Compatibility Group */
                        Child, VGroup, GroupFrameT("Compatibility"),

                            Child, HGroup,
                                Child, Label2("Init Delay:"),
                                Child, HGroup,
                                    Child, chk_InitDelay = MUI_MakeObject(MUIO_Checkmark, NULL),
                                    Child, HSpace(0),
                                End,
                            End,

                        End, /* Compatibility group */

                        Child, VSpace(0),

                    End, /* Tab 2: Advanced */

                End, /* RegisterGroup */

                /* ===== Buttons ===== */
                Child, HGroup, MUIA_Group_SameSize, TRUE,
                    Child, btn_Save = SimpleButton("_Save"),
                    Child, HSpace(0),
                    Child, btn_Cancel = SimpleButton("_Cancel"),
                End,

            End, /* WindowContents VGroup */
        End, /* WindowObject */
    End; /* ApplicationObject */

    return (BOOL)(app != NULL);
}

/* ------------------------------------------------------------------ */
/* Setup MUI notifications                                            */
/* ------------------------------------------------------------------ */
static void SetupNotifications(void)
{
    /* Window close gadget */
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
        (ULONG)app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

    /* Cancel button */
    DoMethod(btn_Cancel, MUIM_Notify, MUIA_Pressed, FALSE,
        (ULONG)app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

    /* Save button => custom return ID */
    DoMethod(btn_Save, MUIM_Notify, MUIA_Pressed, FALSE,
        (ULONG)app, 2, MUIM_Application_ReturnID, MAKE_ID('S','A','V','E'));

    /* Menu: About */
    DoMethod(mnu_About, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
        (ULONG)app, 2, MUIM_Application_ReturnID, MAKE_ID('A','B','O','T'));

    /* Menu: About MUI */
    DoMethod(mnu_AboutMUI, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
        (ULONG)app, 2, MUIM_Application_AboutMUI, (ULONG)win);

    /* Menu: Quit */
    DoMethod(mnu_Quit, MUIM_Notify, MUIA_Menuitem_Trigger, MUIV_EveryTime,
        (ULONG)app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);

    /* Configure mode cycle => update field states */
    DoMethod(cyc_Configure, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
        (ULONG)app, 2, MUIM_Application_ReturnID, MAKE_ID('C','F','G','M'));
}

/* ------------------------------------------------------------------ */
/* Main event loop                                                    */
/* ------------------------------------------------------------------ */
static void EventLoop(void)
{
    ULONG signals;
    ULONG id;
    BOOL running = TRUE;

    set(win, MUIA_Window_Open, TRUE);

    while (running)
    {
        id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&signals);

        switch (id)
        {
            case MUIV_Application_ReturnID_Quit:
                running = FALSE;
                break;

            case MAKE_ID('C','F','G','M'):
                {
                    ULONG mode = 0;
                    get(cyc_Configure, MUIA_Cycle_Active, &mode);
                    UpdateNetFieldsState(mode);
                }
                break;

            case MAKE_ID('S','A','V','E'):
                /* Read GUI into config */
                GUIToConfig(&g_Config);

                /* Validate device name */
                if (g_Config.device[0] == '\0')
                {
                    MUI_Request(app, win, 0,
                        (char *)"Error",
                        (char *)"*_OK",
                        (char *)"Device name is mandatory!",
                        NULL);
                    break;
                }

                /* Warn if the interface is currently active */
                if (g_IsLive)
                {
                    LONG answer;
                    answer = MUI_Request(app, win, 0,
                        (char *)"Warning",
                        (char *)"*_Save|_Cancel",
                        (char *)"The interface is currently active.\n"
                                "Changes will only take effect after\n"
                                "restarting the network stack.",
                        NULL);
                    if (answer == 0)
                        break;
                }

                /* Write the files */
                {
                    BOOL ok;

                    ok = WriteConfigFile(g_FilePath, &g_Config);

                    /* In DHCP mode, don't overwrite network-wide files */
                    if (ok && g_Config.configureMode != CFG_DHCP)
                    {
                        ok = WriteRoutesFile(ROUTES_PATH, &g_NetConfig) &&
                             WriteNameResFile(NAMERES_PATH, &g_NetConfig);
                    }

                    if (ok)
                    {
                        MUI_Request(app, win, 0,
                            (char *)"EditInterface",
                            (char *)"*_OK",
                            (char *)"Configuration saved successfully.",
                            NULL);
                    }
                    else
                    {
                        MUI_Request(app, win, 0,
                            (char *)"Error",
                            (char *)"*_OK",
                            (char *)"Failed to write configuration file!",
                            NULL);
                    }
                }
                break;

            case MAKE_ID('A','B','O','T'):
                {
                    LONG result;
                    result = MUI_Request(app, win, 0,
                        (char *)"About EditInterface",
                        (char *)"*_OK|_GitHub",
                        (char *)"EditInterface 1.2\n"
                                "\n"
                                "Roadshow network interface editor\n"
                                "\n"
                                "Author: Renaud Schweingruber\n"
                                "Email: renaud.schweingruber@protonmail.com\n"
                                "GitHub: https://github.com/TuKo1982/EditInterface",
                        NULL);

                    if (result == 0)
                    {
                        SystemTagList("OpenURL https://github.com/TuKo1982/EditInterface", NULL);
                    }
                }
                break;
        }

        if (running && signals)
            Wait(signals);
    }

    set(win, MUIA_Window_Open, FALSE);
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    int rc = RETURN_FAIL;
    struct RDArgs *rdargs;
    LONG args[1] = { 0 };

    /* Parse CLI arguments: INTERFACE/A */
    rdargs = ReadArgs("INTERFACE/A", args, NULL);
    if (!rdargs)
    {
        PrintFault(IoErr(), "EditInterface");
        Printf("Usage: EditInterface <interface_name>\n");
        return RETURN_FAIL;
    }

    strncpy(g_InterfaceName, (char *)args[0], sizeof(g_InterfaceName) - 1);
    FreeArgs(rdargs);

    /* If argument contains ':' or '/', treat it as a full path.
       Otherwise, build path from DEVS:NetInterfaces/ */
    if (strchr(g_InterfaceName, ':') || strchr(g_InterfaceName, '/'))
    {
        char *name;
        strncpy(g_FilePath, g_InterfaceName, sizeof(g_FilePath) - 1);

        /* Extract just the interface name from the path for the window title */
        name = strrchr(g_InterfaceName, '/');
        if (!name)
            name = strrchr(g_InterfaceName, ':');
        if (name)
            memmove(g_InterfaceName, name + 1, strlen(name + 1) + 1);
    }
    else
    {
        sprintf(g_FilePath, "DEVS:NetInterfaces/%s", g_InterfaceName);
    }

    /* Open Intuition (needed by MUI) */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    if (!IntuitionBase)
    {
        Printf("Cannot open intuition.library v39+\n");
        return RETURN_FAIL;
    }

    /* Open MUI */
    MUIMasterBase = OpenLibrary("muimaster.library", 19);
    if (!MUIMasterBase)
    {
        Printf("Cannot open muimaster.library v19+\n");
        CloseLibrary((struct Library *)IntuitionBase);
        return RETURN_FAIL;
    }

    /* Parse existing config file */
    if (!ParseConfigFile(g_FilePath, &g_Config))
    {
        Printf("Error: Cannot read %s\n", (ULONG)g_FilePath);
        Printf("Interface \"%s\" does not exist.\n", (ULONG)g_InterfaceName);
        rc = RETURN_ERROR;
        goto cleanup;
    }

    /* Parse network-wide config files */
    ParseRoutesFile(ROUTES_PATH, &g_NetConfig);
    ParseNameResFile(NAMERES_PATH, &g_NetConfig);

    /* If DHCP is active, try to get live parameters from the stack */
    QueryLiveInterface(g_InterfaceName, &g_Config, &g_NetConfig);

    /* Build GUI */
    if (!CreateGUI())
    {
        Printf("Cannot create MUI application!\n");
        goto cleanup;
    }

    /* Populate GUI from config data */
    ConfigToGUI(&g_Config);

    /* Wire up notifications */
    SetupNotifications();

    /* Run event loop */
    EventLoop();

    rc = RETURN_OK;

cleanup:
    if (app)
        MUI_DisposeObject(app);

    if (MUIMasterBase)
        CloseLibrary(MUIMasterBase);

    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);

    return rc;
}
