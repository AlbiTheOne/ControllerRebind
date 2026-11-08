// =====================================================================
//  ControllerRebind.asi  -  Free controller button layout for
//  GTA IV: The Complete Edition (1.2.0.59) with FusionFix
//
//  Every controller button (and both triggers) can do what another one does,
//  with one layout in vehicles and one on foot. Set up in a menu in game
//  (F8, or LS + RS clicked together), saved in ControllerRebind.ini.
//  Works by changing what the game reads from XInput; the game's own menus
//  always get the normal layout.
//
//  Byte patterns come from the open-source project FusionFix (ThirteenAG),
//  structure layouts from the IV-SDK (Zolika1351).
// =====================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "third/stb/stb_image.h"

#include "gen/assets.h"       // controller pictures (graphics/make_controllers.ps1, graphics/embed_pngs.ps1)
#include "gen/pad_layout.h"   // where the buttons are on them
#include "gen/pad_hl.h"       // the outline of every button (drawn blue when chosen / pointed at)
#include "gen/icons.h"        // small button icons (graphics/make_icons.ps1)

// ---------------------------------------------------------------------
//  Log
// ---------------------------------------------------------------------
static const char* const kVersion = "1.0";   // shown at the bottom right of the menu and in the log

static std::string g_dir;
static FILE* g_log = nullptr;

static void Log(const char* fmt, ...)
{
    if (!g_log) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
}

static float Clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// ---------------------------------------------------------------------
//  Controller buttons. Index order = order in the menu and in the INI.
// ---------------------------------------------------------------------
enum { kNumBtn = 16, kBtnLT = 6, kBtnRT = 7, kOff = -1 };
static const char* const kBtnName[kNumBtn] = {
    "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Back", "Start", "Up", "Down", "Left", "Right" };
static const char* const kBtnLabel[kNumBtn] = {
    "A / Cross", "B / Circle", "X / Square", "Y / Triangle", "LB / L1", "RB / R1", "LT / L2", "RT / R2",
    "LS click / L3", "RS click / R3", "Back / Share", "Start / Options",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right" };
static const WORD kBtnBit[kNumBtn] = {
    0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0, 0, 0x0040, 0x0080, 0x0020, 0x0010, 0x0001, 0x0002, 0x0004, 0x0008 };
static const BYTE kTriggerPressed = 30;   // XINPUT_GAMEPAD_TRIGGER_THRESHOLD

enum { kProfVehicle = 0, kProfFoot = 1 };
static const char* const kProfSection[2] = { "Vehicle", "OnFoot" };

// map[profile][physical button] = what the game gets instead (button index), kOff = nothing
static volatile signed char g_map[2][kNumBtn];
// keyboard key (virtual-key code) a physical button presses instead of a game action, per page; 0 = none.
// With a key the game gets nothing from that button (its g_map entry is kOff).
static volatile int g_keyBind[2][kNumBtn];

// "A" -> 0 ... ; "Off"/"None" -> kOff; unknown -> -2
static int ParseBtn(const char* raw)
{
    char s[32] = {};
    int n = 0;
    for (const char* p = raw; *p && n < 31; p++) if (*p != ' ' && *p != '\t') s[n++] = *p;
    if (!n) return -2;
    if (_stricmp(s, "Off") == 0 || _stricmp(s, "None") == 0) return kOff;
    for (int i = 0; i < kNumBtn; i++) if (_stricmp(s, kBtnName[i]) == 0) return i;
    // a few other names people may write
    static const struct { const char* n; int i; } alias[] = {
        { "Cross", 0 }, { "Circle", 1 }, { "Square", 2 }, { "Triangle", 3 }, { "L1", 4 }, { "R1", 5 }, { "L2", 6 }, { "R2", 7 },
        { "L3", 8 }, { "R3", 9 }, { "Select", 10 }, { "Share", 10 }, { "Options", 11 },
        { "DpadUp", 12 }, { "DpadDown", 13 }, { "DpadLeft", 14 }, { "DpadRight", 15 } };
    for (auto& a : alias) if (_stricmp(s, a.n) == 0) return a.i;
    return -2;
}

// ---------------------------------------------------------------------
//  Settings (ControllerRebind.ini)
// ---------------------------------------------------------------------
struct Config
{
    int  keyMenu = VK_PRIOR;   // opens / closes the menu (Page Up)
    int  style = 0;         // controller shown in the menu: 0 = Xbox, 1 = PlayStation
} cfg;

static const char* kIniName = "ControllerRebind.ini";

static const char* kDefaultIni =
"; Controller Rebind - Settings\r\n"
"; Free controller button layout for GTA IV: The Complete Edition (requires FusionFix)\r\n"
";\r\n"
"; Easiest: open the menu in game with Page Up and change the buttons there with the mouse.\r\n"
";\r\n"
"; Buttons: A, B, X, Y, LB, RB, LT, RT, LS, RS, Back, Start, Up, Down, Left, Right (D-pad). Off = does nothing.\r\n"
"; (A = Cross, B = Circle, X = Square, Y = Triangle, LB = L1, RB = R1, LT = L2, RT = R2, LS = L3, RS = R3,\r\n"
";  Back = Share / Select, Start = Options)\r\n"
"\r\n"
"[Menu]\r\n"
"; Key that opens and closes the menu in game (Esc closes it as well)\r\n"
"Key=PageUp\r\n"
"; Controller shown in the menu: Xbox or PlayStation (also switched in the menu)\r\n"
"Style=Xbox\r\n"
"; Language of the menu: Auto (the game's language, from FusionFix), English, German, French, Italian or Spanish\r\n"
"Language=Auto\r\n"
"\r\n"
"; In a vehicle: button you press = what the game gets\r\n"
"; e.g. RB=A: RB does what A does in the game. Two buttons can do the same thing.\r\n"
"; Key:F9 = the button presses keyboard key F9 instead (for mods that only listen to the keyboard). Key names:\r\n"
";   A ... Z, 0 ... 9, F1 ... F24, Num0 ... Num9, Space, Enter, Tab, Backspace, Esc, LShift, RShift, LCtrl, RCtrl,\r\n"
";   LAlt, RAlt, Up, Down, Left, Right, Insert, Delete, Home, End, PageUp, PageDown, NumPlus, NumMinus, or 0x4A\r\n"
"[Vehicle]\r\n"
"A=A\r\nB=B\r\nX=X\r\nY=Y\r\nLB=LB\r\nRB=RB\r\nLT=LT\r\nRT=RT\r\nLS=LS\r\nRS=RS\r\nBack=Back\r\nStart=Start\r\n"
"Up=Up\r\nDown=Down\r\nLeft=Left\r\nRight=Right\r\n"
"\r\n"
"; On foot, the same way\r\n"
"[OnFoot]\r\n"
"A=A\r\nB=B\r\nX=X\r\nY=Y\r\nLB=LB\r\nRB=RB\r\nLT=LT\r\nRT=RT\r\nLS=LS\r\nRS=RS\r\nBack=Back\r\nStart=Start\r\n"
"Up=Up\r\nDown=Down\r\nLeft=Left\r\nRight=Right\r\n";

static const struct { const char* name; int vk; } kKeyNames[] = {
    { "Space", VK_SPACE }, { "Enter", VK_RETURN }, { "Tab", VK_TAB }, { "Backspace", VK_BACK }, { "Esc", VK_ESCAPE },
    { "LShift", VK_LSHIFT }, { "RShift", VK_RSHIFT }, { "LCtrl", VK_LCONTROL }, { "RCtrl", VK_RCONTROL },
    { "LAlt", VK_LMENU }, { "RAlt", VK_RMENU }, { "Shift", VK_SHIFT }, { "Ctrl", VK_CONTROL }, { "Alt", VK_MENU },
    { "Up", VK_UP }, { "Down", VK_DOWN }, { "Left", VK_LEFT }, { "Right", VK_RIGHT },
    { "Insert", VK_INSERT }, { "Delete", VK_DELETE }, { "Home", VK_HOME }, { "End", VK_END },
    { "PageUp", VK_PRIOR }, { "PageDown", VK_NEXT }, { "CapsLock", VK_CAPITAL },
    { "NumPlus", VK_ADD }, { "NumMinus", VK_SUBTRACT }, { "NumMultiply", VK_MULTIPLY }, { "NumDivide", VK_DIVIDE },
    { "NumDecimal", VK_DECIMAL }, { ",", VK_OEM_COMMA }, { ".", VK_OEM_PERIOD }, { "-", VK_OEM_MINUS }, { "+", VK_OEM_PLUS } };

// Key name or code -> Windows virtual-key code (0 = no key, -1 = unknown)
static int ParseKey(const char* raw)
{
    char k[32] = {};
    int n = 0;
    for (const char* p = raw; *p && n < 31; p++)
        if (*p != ' ' && *p != '\t') k[n++] = (char)toupper((unsigned char)*p);
    if (!n) return -1;
    if (k[0] == '0' && k[1] == 'X') return (int)strtol(k, nullptr, 16);
    if (n == 1 && ((k[0] >= 'A' && k[0] <= 'Z') || (k[0] >= '0' && k[0] <= '9'))) return k[0];
    if (k[0] == 'F' && n >= 2 && n <= 3 && isdigit((unsigned char)k[1]))
    {
        int f = atoi(k + 1);
        if (f >= 1 && f <= 24) return VK_F1 + f - 1;
    }
    if (strncmp(k, "NUM", 3) == 0 && n == 4 && isdigit((unsigned char)k[3])) return VK_NUMPAD0 + (k[3] - '0');
    if (strcmp(k, "NONE") == 0 || strcmp(k, "OFF") == 0) return 0;
    for (auto& e : kKeyNames) { char u[32]; size_t i = 0; for (; e.name[i] && i < 31; i++) u[i] = (char)toupper((unsigned char)e.name[i]); u[i] = 0; if (strcmp(k, u) == 0) return e.vk; }
    return -1;
}

// Windows virtual-key code -> name as written in the INI and shown in the menu
static const char* KeyName(int vk)
{
    static char buf[16];
    if (vk <= 0) return "None";
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { buf[0] = (char)vk; buf[1] = 0; return buf; }
    if (vk >= VK_F1 && vk <= VK_F24) { snprintf(buf, sizeof(buf), "F%d", vk - VK_F1 + 1); return buf; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { snprintf(buf, sizeof(buf), "Num%d", vk - VK_NUMPAD0); return buf; }
    for (auto& e : kKeyNames) if (e.vk == vk) return e.name;
    snprintf(buf, sizeof(buf), "0x%02X", vk);
    return buf;
}

static std::string IniPath() { return g_dir + kIniName; }

static void LoadConfig()
{
    std::string path = IniPath();
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        FILE* f = fopen(path.c_str(), "wb");
        if (f) { fputs(kDefaultIni, f); fclose(f); }
    }
    const char* p = path.c_str();
    char buf[64];
    GetPrivateProfileStringA("Menu", "Key", "PageUp", buf, sizeof(buf), p);
    int vk = ParseKey(buf);
    if (vk < 0) Log("Unknown menu key '%s' - using Page Up", buf);
    cfg.keyMenu = vk <= 0 ? VK_PRIOR : vk;
    GetPrivateProfileStringA("Menu", "Style", "Xbox", buf, sizeof(buf), p);
    cfg.style = (_strnicmp(buf, "PlayStation", 4) == 0 || _strnicmp(buf, "PS", 2) == 0) ? 1 : 0;
    for (int pr = 0; pr < 2; pr++)
        for (int i = 0; i < kNumBtn; i++)
        {
            GetPrivateProfileStringA(kProfSection[pr], kBtnName[i], kBtnName[i], buf, sizeof(buf), p);
            g_keyBind[pr][i] = 0;
            if (_strnicmp(buf, "Key:", 4) == 0)
            {
                const int vk = ParseKey(buf + 4);
                if (vk > 0) { g_keyBind[pr][i] = vk; g_map[pr][i] = kOff; continue; }
                Log("[%s] %s: unknown key '%s' - unchanged", kProfSection[pr], kBtnName[i], buf + 4);
                g_map[pr][i] = (signed char)i;
                continue;
            }
            int t = ParseBtn(buf);
            if (t == -2) { Log("[%s] %s: unknown button '%s' - unchanged", kProfSection[pr], kBtnName[i], buf); t = i; }
            g_map[pr][i] = (signed char)t;
        }
    int keys = 0;
    for (int pr = 0; pr < 2; pr++) for (int i = 0; i < kNumBtn; i++) if (g_keyBind[pr][i]) keys++;
    int changed = 0;
    for (int pr = 0; pr < 2; pr++) for (int i = 0; i < kNumBtn; i++) if (g_map[pr][i] != i) changed++;
    Log("Menu key 0x%X | %d buttons changed, %d press a keyboard key", cfg.keyMenu, changed, keys);
}

static void SaveMaps()
{
    std::string path = IniPath();
    for (int pr = 0; pr < 2; pr++)
        for (int i = 0; i < kNumBtn; i++)
        {
            int t = g_map[pr][i];
            char key[32];
            if (g_keyBind[pr][i]) snprintf(key, sizeof(key), "Key:%s", KeyName(g_keyBind[pr][i]));
            WritePrivateProfileStringA(kProfSection[pr], kBtnName[i], g_keyBind[pr][i] ? key : t < 0 ? "Off" : kBtnName[t], path.c_str());
        }
    Log("Layout saved");
}

// ---------------------------------------------------------------------
//  Pattern search in the game code
// ---------------------------------------------------------------------
static std::vector<int> ParsePattern(const char* s)
{
    std::vector<int> out;
    while (*s)
    {
        if (*s == ' ') { s++; continue; }
        if (*s == '?') { out.push_back(-1); s++; if (*s == '?') s++; continue; }
        out.push_back((int)strtol(s, (char**)&s, 16));
    }
    return out;
}

static std::vector<uint8_t*> FindAll(const char* pattern)
{
    std::vector<uint8_t*> res;
    auto pat = ParsePattern(pattern);
    if (pat.empty()) return res;
    auto base = (uint8_t*)GetModuleHandleA(nullptr);
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
    {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t* start = base + sec->VirtualAddress;
        size_t size = sec->Misc.VirtualSize;
        if (size < pat.size()) continue;
        for (size_t o = 0; o + pat.size() <= size; o++)
        {
            size_t k = 0;
            for (; k < pat.size(); k++)
                if (pat[k] >= 0 && start[o + k] != (uint8_t)pat[k]) break;
            if (k == pat.size()) res.push_back(start + o);
        }
    }
    return res;
}

static uint8_t* FindFirst(std::initializer_list<const char*> pats)
{
    for (auto p : pats)
    {
        auto v = FindAll(p);
        if (!v.empty()) return v[0];
    }
    return nullptr;
}

static uint8_t* CallDest(uint8_t* p) { return p + 5 + *(int32_t*)(p + 1); }

static bool IsReadable(const void* p, size_t len)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
    return (uint8_t*)p + len <= (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
}

static bool IsWritable(const void* p, size_t len)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (!(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
    return (uint8_t*)p + len <= (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
}

// ---------------------------------------------------------------------
//  Game addresses and natives (same method as FusionFix)
// ---------------------------------------------------------------------
static void* (__stdcall* g_getNativeAddress)(uint32_t) = nullptr;
static bool* g_userPause = nullptr;
static bool* g_scriptPause = nullptr;
static bool* g_menuActive = nullptr;
static IDirect3DDevice9** g_ppDevice = nullptr;

struct NVec3 { float x, y, z; };
struct NVec4 { float x, y, z, w; };
struct NativeCtx
{
    void*    ret;
    uint32_t argCount;
    void*    args;
    uint32_t dataCount;
    NVec3*   orig[4];
    NVec4    tmp[4];
    uint8_t  stack[16 * 4];
};
typedef void (__cdecl* NativeFn)(NativeCtx*);

static NativeFn GetNative(uint32_t hash)
{
    static uint32_t hashes[16];
    static NativeFn fns[16];
    static int count = 0;
    for (int i = 0; i < count; i++) if (hashes[i] == hash) return fns[i];
    NativeFn f = g_getNativeAddress ? (NativeFn)g_getNativeAddress(hash) : nullptr;
    if (f && count < 16) { hashes[count] = hash; fns[count] = f; count++; }
    return f;
}

struct NCall
{
    NativeCtx c;
    NCall() { memset(&c, 0, sizeof(c)); c.ret = c.stack; c.args = c.stack; }
    template<class T> void Push(T v)
    {
        static_assert(sizeof(T) <= 4, "arg");
        *(uint32_t*)(c.stack + 4 * c.argCount) = 0;
        memcpy(c.stack + 4 * c.argCount, &v, sizeof(T));
        c.argCount++;
    }
};

template<class R, class... A>
static R Native(uint32_t hash, A... a)
{
    NCall n;
    (n.Push(a), ...);
    NativeFn f = GetNative(hash);
    if (!f) return R();
    f(&n.c);
    return *(R*)n.c.stack;
}
template<class... A>
static bool NativeOk(uint32_t hash, A... a)
{
    NCall n;
    (n.Push(a), ...);
    NativeFn f = GetNative(hash);
    if (!f) return false;
    f(&n.c);
    return true;
}

namespace H
{
    const uint32_t GET_PLAYER_ID              = 0x62E319C6;
    const uint32_t CONVERT_INT_TO_PLAYERINDEX = 0x5996315E;
    const uint32_t GET_PLAYER_CHAR            = 0x511454A9;
    const uint32_t IS_PLAYER_PLAYING          = 0x08274BA4;
    const uint32_t IS_CHAR_IN_ANY_CAR         = 0x71184DA3;
    const uint32_t IS_PAUSE_MENU_ACTIVE       = 0x6C4568A7;
}

// ---------------------------------------------------------------------
//  XInput: what the game reads
// ---------------------------------------------------------------------
struct PadState { DWORD packet; WORD buttons; BYTE leftTrigger, rightTrigger; SHORT lx, ly, rx, ry; };
typedef DWORD (WINAPI* XInputGetStateFn)(DWORD, PadState*);

// The function the game calls next (XInput, or Manual Shift's filter that sits right on XInput)
static XInputGetStateFn volatile g_next = nullptr;
// Manual Shift (from the version that works with Controller Rebind) asks for this and puts its filter in there,
// so it sees the real buttons and takes its shift buttons away before they are remapped here.
extern "C" __declspec(dllexport) void* __cdecl ControllerRebind_NextSlot() { return (void*)&g_next; }

static volatile bool  g_inVehicle = false;   // player is in a vehicle: vehicle layout
static volatile bool  g_frontend = true;     // game paused or in its menus: normal layout
static volatile bool  g_blockAll = false;    // our menu is open (or its buttons are still held): the game gets nothing
static volatile DWORD g_stateTick = 0;       // last update from the game frame; stale = everything passes unchanged
static volatile LONG  g_gameReads = 0;

static void Remap(PadState* st, int prof)
{
    BYTE val[kNumBtn];
    for (int i = 0; i < kNumBtn; i++) val[i] = (st->buttons & kBtnBit[i]) ? 255 : 0;
    val[kBtnLT] = st->leftTrigger;
    val[kBtnRT] = st->rightTrigger;
    WORD buttons = st->buttons & ~(WORD)0xF3FF;   // keep bits that are no button here (0x0400 / 0x0800, unused)
    BYTE lt = 0, rt = 0;
    for (int i = 0; i < kNumBtn; i++)
    {
        const int t = g_map[prof][i];
        if (t < 0 || !val[i]) continue;
        // A trigger counts as pressed from the usual threshold when it does what a button does
        const bool pressed = (i == kBtnLT || i == kBtnRT) ? val[i] > kTriggerPressed : true;
        if (t == kBtnLT) lt = (std::max)(lt, val[i]);
        else if (t == kBtnRT) rt = (std::max)(rt, val[i]);
        else if (pressed) buttons |= kBtnBit[t];
    }
    st->buttons = buttons;
    st->leftTrigger = lt;
    st->rightTrigger = rt;
}

static DWORD WINAPI RebindGetState(DWORD index, PadState* st)
{
    XInputGetStateFn next = g_next;
    if (!next) return ERROR_DEVICE_NOT_CONNECTED;
    DWORD r = next(index, st);
    if (r != ERROR_SUCCESS || !st) return r;
    InterlockedIncrement(&g_gameReads);
    if (GetTickCount() - g_stateTick > 300) return r;   // the mod is not running right now (loading): unchanged
    if (g_blockAll)
    {
        st->buttons = 0; st->leftTrigger = st->rightTrigger = 0;
        st->lx = st->ly = st->rx = st->ry = 0;
        return r;
    }
    if (!g_frontend) Remap(st, g_inVehicle ? kProfVehicle : kProfFoot);
    return r;
}

// The game loads XInput at run time and keeps the XInputGetState address in a variable (not in the import table)
static XInputGetStateFn volatile* g_gameGetStateSlot = nullptr;

static void FindPadSlot()
{
    // push "XInputGetState"; push eax; call esi; push "XInputSetState"; push [handle]; mov [slot],eax
    uint8_t* p = nullptr;
    for (uint8_t* hit : FindAll("68 ? ? ? ? 50 FF D6 68 ? ? ? ? FF 35 ? ? ? ? A3"))
    {
        const char* name = *(const char**)(hit + 1);
        if (IsReadable(name, 15) && strcmp(name, "XInputGetState") == 0) { p = hit; break; }
    }
    if (!p) { Log("Controller: code not found - buttons cannot be remapped"); return; }
    auto slot = *(XInputGetStateFn**)(p + 20);
    if (!IsWritable(slot, sizeof(*slot))) { Log("Controller: variable not writable - buttons cannot be remapped"); return; }
    g_gameGetStateSlot = slot;
    Log("Controller: game variable at %p", (void*)slot);
}

static const char* ModuleBaseName(const void* addr, HMODULE* out = nullptr)
{
    static char path[MAX_PATH];
    HMODULE mod = nullptr;
    path[0] = 0;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &mod) || !mod)
        return "unknown";
    if (out) *out = mod;
    GetModuleFileNameA(mod, path, MAX_PATH);
    const char* s = strrchr(path, '\\');
    return s ? s + 1 : path;
}

// Every frame: the game fills the variable when it starts its input, possibly after the mod was loaded.
// Controller Rebind goes right on XInput, or on a Manual Shift that knows it (that one then moves below it).
// An older Manual Shift puts its filter in front of whatever is there each frame: two such filters on top of
// each other would call each other for ever, so Controller Rebind never goes on top of that one.
static void UpdatePadHook()
{
    if (!g_gameGetStateSlot) return;
    XInputGetStateFn cur = *g_gameGetStateSlot;
    if (!cur || cur == &RebindGetState) return;
    static XInputGetStateFn reported = nullptr;
    if (g_next)
    {
        // Already in: another mod went in front of Controller Rebind. It still sees the remapped buttons.
        if (cur != reported) { reported = cur; Log("Controller: %s is in front of Controller Rebind", ModuleBaseName((void*)cur)); }
        return;
    }
    HMODULE mod = nullptr;
    const char* name = ModuleBaseName((void*)cur, &mod);
    const bool xinput = _strnicmp(name, "xinput", 6) == 0;
    const bool newManualShift = mod && GetProcAddress(mod, "ManualShift_ReloadController");
    if (!xinput && !newManualShift)
    {
        if (cur != reported)
        {
            reported = cur;
            Log("Controller: %s already filters the controller - buttons cannot be remapped.%s", name,
                _strnicmp(name, "ManualShift", 11) == 0 ? " This Manual Shift is too old for Controller Rebind: update Manual Shift." : "");
        }
        return;
    }
    g_next = cur;
    *g_gameGetStateSlot = &RebindGetState;
    Log("Controller: remapping active (on %s)", name);
}

// ---------------------------------------------------------------------
//  The real controller, read directly (for the menu)
// ---------------------------------------------------------------------
static WORD  g_phys = 0;        // pressed buttons, bit = button index (triggers from the usual threshold)
static bool  g_padConnected = false;

static void PollPad()
{
    static XInputGetStateFn getState = nullptr;
    static bool tried = false;
    static int slot = -1;
    static DWORD nextSearch = 0;
    if (!tried)
    {
        tried = true;
        static const char* const dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
        for (auto d : dlls)
        {
            HMODULE m = LoadLibraryA(d);
            if (m) getState = (XInputGetStateFn)GetProcAddress(m, "XInputGetState");
            if (getState) { Log("Menu reads the controller through %s", d); break; }
        }
        if (!getState) Log("No XInput found - the menu works with the keyboard only");
    }
    g_phys = 0;
    g_padConnected = false;
    if (!getState) return;
    PadState st;
    if (slot >= 0 && getState((DWORD)slot, &st) != ERROR_SUCCESS) slot = -1;
    if (slot < 0)
    {
        DWORD now = GetTickCount();
        if ((LONG)(now - nextSearch) < 0) return;
        nextSearch = now + 1000;   // asking for an empty slot is slow
        for (int i = 0; i < 4 && slot < 0; i++)
            if (getState((DWORD)i, &st) == ERROR_SUCCESS) slot = i;
        if (slot < 0) return;
    }
    g_padConnected = true;
    for (int i = 0; i < kNumBtn; i++) if (kBtnBit[i] && (st.buttons & kBtnBit[i])) g_phys |= 1 << i;
    if (st.leftTrigger > kTriggerPressed) g_phys |= 1 << kBtnLT;
    if (st.rightTrigger > kTriggerPressed) g_phys |= 1 << kBtnRT;
}

static bool KeyDown(int vk) { return vk > 0 && (GetAsyncKeyState(vk) & 0x8000) != 0; }

static bool GameHasFocus()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// ---------------------------------------------------------------------
//  Manual Shift's shift buttons (page 3 of the menu, only with a Manual Shift that can reload them)
// ---------------------------------------------------------------------
typedef void (__cdecl* MsReloadFn)();
static MsReloadFn g_msReload = nullptr;
static bool g_msFound = false;   // ManualShift.asi is loaded (with or without g_msReload)
static std::string g_msIni;
static volatile int g_msUp = 0, g_msDown = 1;   // button index, kOff = none
static bool g_msChanged = false;

static void FindManualShift()
{
    HMODULE ms = GetModuleHandleA("ManualShift.asi");
    g_msFound = ms != nullptr;
    if (!ms) return;
    g_msReload = (MsReloadFn)GetProcAddress(ms, "ManualShift_ReloadController");
    if (!g_msReload) { Log("Manual Shift found, but it is too old to change its buttons here"); return; }
    char path[MAX_PATH];
    GetModuleFileNameA(ms, path, MAX_PATH);
    g_msIni = path;
    g_msIni = g_msIni.substr(0, g_msIni.find_last_of("\\/") + 1) + "ManualShift.ini";
    char buf[32];
    const bool swap = GetPrivateProfileIntA("Controller", "SwapShift", 0, g_msIni.c_str()) != 0;
    GetPrivateProfileStringA("Controller", "ShiftUp", swap ? "B" : "A", buf, sizeof(buf), g_msIni.c_str());
    int b = ParseBtn(buf); g_msUp = (b == -2 || b == kBtnLT || b == kBtnRT) ? (swap ? 1 : 0) : b;
    GetPrivateProfileStringA("Controller", "ShiftDown", swap ? "A" : "B", buf, sizeof(buf), g_msIni.c_str());
    b = ParseBtn(buf); g_msDown = (b == -2 || b == kBtnLT || b == kBtnRT) ? (swap ? 0 : 1) : b;
    Log("Manual Shift found: shift up %s, shift down %s", g_msUp < 0 ? "None" : kBtnName[g_msUp], g_msDown < 0 ? "None" : kBtnName[g_msDown]);
}

static void SaveManualShift()
{
    if (!g_msReload || !g_msChanged) return;
    g_msChanged = false;
    WritePrivateProfileStringA("Controller", "ShiftUp", g_msUp < 0 ? "None" : kBtnName[g_msUp], g_msIni.c_str());
    WritePrivateProfileStringA("Controller", "ShiftDown", g_msDown < 0 ? "None" : kBtnName[g_msDown], g_msIni.c_str());
    g_msReload();
    Log("Manual Shift buttons saved: up %s, down %s", g_msUp < 0 ? "None" : kBtnName[g_msUp], g_msDown < 0 ? "None" : kBtnName[g_msDown]);
}

// ---------------------------------------------------------------------
//  Menu (state in the game thread, drawn in the render thread)
// ---------------------------------------------------------------------
static volatile bool g_menuOpen = false;
static volatile int  g_tab = 0, g_row = 0;   // pages: 0 = on foot, 1 = combat, 2 = in a vehicle, 3 = Manual Shift
static volatile WORD g_menuPhys = 0;         // pressed buttons for the menu's live light
static volatile DWORD g_menuTick = 0;
static bool g_mapsChanged = false;
static void ReadGameLanguage();               // the menu's language (with the font, further down)
static volatile bool g_picker = false;       // the window with all actions as tiles is open (for g_row)
static volatile int g_style = 0;             // look of the menu's controller: 0 = Xbox, 1 = PlayStation
static volatile bool g_styleChanged = false;

enum { kTabFoot = 0, kTabCombat = 1, kTabVehicle = 2 };
static int TabCount() { return 3; }
static int RowCount(int) { return kNumBtn; }
// Combat is on foot with a weapon: the game cannot be told apart from on foot, so it uses the on-foot layout
static int TabProfile(int tab) { return tab == kTabVehicle ? kProfVehicle : kProfFoot; }
static volatile bool g_capture = false;   // the tile window waits for a keyboard key (KEYBOARD KEY tile)

// next / previous choice (arrow keys): Off, then all 16 buttons
static int Step(int v, int dir)
{
    v += dir;
    if (v > kNumBtn - 1) v = kOff;
    if (v < kOff) v = kNumBtn - 1;
    return v;
}

static void ChangeRow(int tab, int row, int dir)
{
    const int pr = TabProfile(tab);
    g_keyBind[pr][row] = 0;
    g_map[pr][row] = (signed char)Step(g_map[pr][row], dir);
    g_mapsChanged = true;
}

static void ResetRow(int tab, int row, bool wholePage)
{
    const int pr = TabProfile(tab);
    for (int i = 0; i < kNumBtn; i++)
        if (wholePage || i == row) { g_map[pr][i] = (signed char)i; g_keyBind[pr][i] = 0; }
    g_mapsChanged = true;
}

// Chosen in the tile window: a game action (button index) or Off
static void SetRow(int tab, int row, int v)
{
    const int pr = TabProfile(tab);
    g_keyBind[pr][row] = 0;
    g_map[pr][row] = (signed char)v;
    g_mapsChanged = true;
}

// KEYBOARD KEY tile: the button presses this key, the game gets nothing from it
static void SetRowKey(int tab, int row, int vk)
{
    const int pr = TabProfile(tab);
    g_keyBind[pr][row] = vk;
    g_map[pr][row] = kOff;
    g_mapsChanged = true;
}

static bool RowChanged(int tab, int row)
{
    const int pr = TabProfile(tab);
    return g_keyBind[pr][row] != 0 || g_map[pr][row] != row;
}

// What the layout was when the menu was opened: closing without saving puts it back
static signed char g_openMap[2][kNumBtn];
static int g_openKeys[2][kNumBtn];
static int g_openStyle = 0;

static void RememberOpenState()
{
    for (int pr = 0; pr < 2; pr++)
        for (int i = 0; i < kNumBtn; i++) { g_openMap[pr][i] = g_map[pr][i]; g_openKeys[pr][i] = g_keyBind[pr][i]; }
    g_openStyle = g_style;
}

// save = SAVE AND CLOSE. Page Up / Esc close without saving: every change made since opening is undone.
static void CloseMenu(bool save)
{
    g_menuOpen = false;
    if (save)
    {
        if (g_mapsChanged) SaveMaps();
        if (g_styleChanged) WritePrivateProfileStringA("Menu", "Style", g_style ? "PlayStation" : "Xbox", IniPath().c_str());
        Log("Menu closed - saved");
    }
    else
    {
        for (int pr = 0; pr < 2; pr++)
            for (int i = 0; i < kNumBtn; i++) { g_map[pr][i] = g_openMap[pr][i]; g_keyBind[pr][i] = g_openKeys[pr][i]; }
        g_style = g_openStyle;
        Log((g_mapsChanged || g_styleChanged) ? "Menu closed without saving - changes undone" : "Menu closed");
    }
    g_mapsChanged = false;
    g_styleChanged = false;
}
// ---------------------------------------------------------------------
//  Mouse and keyboard: the game reads them through DirectInput 8. While the menu is open the game gets nothing from
//  them (the camera stays still, no shots, Esc does not open the pause menu); the mouse movement it would have got
//  moves the menu's own pointer instead. Hooked in the device vtables of dinput8.dll, so the game's devices are covered
//  whenever they were created.
// ---------------------------------------------------------------------
static volatile bool g_diBlock = false;      // the game gets no mouse and keyboard
static volatile LONG g_mouseDX = 0, g_mouseDY = 0, g_mouseDZ = 0;   // movement collected for the menu's pointer
static volatile LONG g_mouseButtons = 0;     // bit 0 left, bit 1 right (as the game would have got them)
static volatile LONG g_mouseSeen = 0;        // DirectInput mouse reads while blocked

typedef HRESULT (__stdcall* DiStateFn)(void* self, DWORD cb, void* data);
typedef HRESULT (__stdcall* DiDataFn)(void* self, DWORD cb, void* items, DWORD* inOut, DWORD flags);
typedef HRESULT (__stdcall* DiCapsFn)(void* self, void* caps);
static DiStateFn g_origDiState[2] = {};
static DiDataFn g_origDiData[2] = {};

// 0x12 = mouse, 0x13 = keyboard (DI8DEVTYPE), 0 = something else. Asked once per device.
static int DiDevType(void* self)
{
    static void* devs[32];
    static int types[32];
    static int count = 0;
    for (int i = 0; i < count; i++) if (devs[i] == self) return types[i];
    DWORD caps[11] = {};
    caps[0] = sizeof(caps);
    int type = 0;
    if (SUCCEEDED(((DiCapsFn)(*(void***)self)[3])(self, caps))) type = (int)(caps[2] & 0xFF);
    if (type != 0x12 && type != 0x13) type = 0;
    if (count < 32) { devs[count] = self; types[count] = type; count++; }
    Log("Input device %p: %s", self, type == 0x12 ? "mouse" : type == 0x13 ? "keyboard" : "other");
    return type;
}

template<int K>
static HRESULT __stdcall DiStateHook(void* self, DWORD cb, void* data)
{
    HRESULT hr = g_origDiState[K](self, cb, data);
    if (!g_diBlock || FAILED(hr) || !data) return hr;
    const int type = DiDevType(self);
    if (type == 0x12 && cb >= 16)
    {
        const LONG* m = (const LONG*)data;
        const BYTE* b = (const BYTE*)data + 12;
        InterlockedExchangeAdd(&g_mouseDX, m[0]);
        InterlockedExchangeAdd(&g_mouseDY, m[1]);
        InterlockedExchangeAdd(&g_mouseDZ, m[2]);
        g_mouseButtons = ((b[0] & 0x80) ? 1 : 0) | ((b[1] & 0x80) ? 2 : 0);
        InterlockedIncrement(&g_mouseSeen);
        memset(data, 0, cb);
    }
    else if (type == 0x13) memset(data, 0, cb);
    return hr;
}

template<int K>
static HRESULT __stdcall DiDataHook(void* self, DWORD cb, void* items, DWORD* inOut, DWORD flags)
{
    HRESULT hr = g_origDiData[K](self, cb, items, inOut, flags);
    if (!g_diBlock || FAILED(hr) || !inOut) return hr;
    const int type = DiDevType(self);
    if (type != 0x12 && type != 0x13) return hr;
    if (type == 0x12 && items && cb >= 8)
    {
        // DIDEVICEOBJECTDATA: offset (DIMOFS_X 0, Y 4, Z 8, BUTTON0 12, BUTTON1 13), data
        for (DWORD i = 0; i < *inOut; i++)
        {
            const DWORD* it = (const DWORD*)((const uint8_t*)items + i * cb);
            switch (it[0])
            {
            case 0: InterlockedExchangeAdd(&g_mouseDX, (LONG)it[1]); break;
            case 4: InterlockedExchangeAdd(&g_mouseDY, (LONG)it[1]); break;
            case 8: InterlockedExchangeAdd(&g_mouseDZ, (LONG)it[1]); break;
            case 12: g_mouseButtons = (it[1] & 0x80) ? (g_mouseButtons | 1) : (g_mouseButtons & ~1); break;
            case 13: g_mouseButtons = (it[1] & 0x80) ? (g_mouseButtons | 2) : (g_mouseButtons & ~2); break;
            }
        }
        InterlockedIncrement(&g_mouseSeen);
    }
    *inOut = 0;   // the game gets no events
    return hr;
}

static void InstallInputHooks()
{
    static bool done = false;
    if (done) return;
    done = true;
    typedef HRESULT (__stdcall* CreateFn)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
    typedef HRESULT (__stdcall* CreateDevFn)(void* self, REFGUID, void** dev, IUnknown*);
    HMODULE m = GetModuleHandleA("dinput8.dll");
    if (!m) m = LoadLibraryA("dinput8.dll");
    CreateFn create = m ? (CreateFn)GetProcAddress(m, "DirectInput8Create") : nullptr;
    if (!create) { Log("DirectInput not found - the game keeps getting mouse and keyboard while the menu is open"); return; }
    static const GUID kIid[2] = {
        { 0xBF798030, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 } },    // IDirectInput8A
        { 0xBF798031, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 } } };  // IDirectInput8W
    static const GUID kSysMouse = { 0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };
    void** patched[2] = {};
    for (int k = 0; k < 2; k++)
    {
        void* di = nullptr;
        if (FAILED(create(GetModuleHandleA(nullptr), 0x0800, kIid[k], &di, nullptr)) || !di) continue;
        void* dev = nullptr;
        if (SUCCEEDED(((CreateDevFn)(*(void***)di)[3])(di, kSysMouse, &dev, nullptr)) && dev)
        {
            void** vt = *(void***)dev;
            if (vt != patched[0])   // the A and W devices may share one table: patch it only once
            {
                DWORD old;
                if (VirtualProtect(&vt[9], 2 * sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
                {
                    g_origDiState[k] = (DiStateFn)vt[9];
                    g_origDiData[k] = (DiDataFn)vt[10];
                    vt[9] = k ? (void*)&DiStateHook<1> : (void*)&DiStateHook<0>;
                    vt[10] = k ? (void*)&DiDataHook<1> : (void*)&DiDataHook<0>;
                    VirtualProtect(&vt[9], 2 * sizeof(void*), old, &old);
                    patched[k] = vt;
                }
            }
            ((IUnknown*)dev)->Release();
        }
        ((IUnknown*)di)->Release();
    }
    Log("Mouse and keyboard hook: %s", patched[0] || patched[1] ? "active" : "failed");
}

static volatile bool g_closeRequest = false;   // the menu's close button was clicked (render thread)

static void UpdateMenu(bool focus, bool frontend)
{
    PollPad();   // only for the live light of the controller buttons
    const WORD phys = g_phys;

    // Keyboard, only while the game window is in front (read directly, the game does not get it while the menu is open)
    static bool prevKeys[9] = { true, true, true, true, true, true, true, true, true };   // a key held at start does not count
    const int keys[9] = { cfg.keyMenu, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_TAB, VK_BACK, VK_DELETE, VK_ESCAPE };
    bool kNow[9], kHit[9];
    for (int i = 0; i < 9; i++)
    {
        const bool down = KeyDown(keys[i]);   // not tied to focus: coming back to the game is no new press
        kNow[i] = focus && down;
        kHit[i] = focus && down && !prevKeys[i];
        prevKeys[i] = down;
    }


    // Keyboard page: while its window is open, the next key pressed is taken (Esc: cancel). Every key is watched all the
    // time, so a key that is already held does not count.
    static bool prevAll[256] = {};
    bool captured = false;
    const bool capturing = g_menuOpen && g_picker && g_capture;
    for (int vk = 0x08; vk < 0xFF; vk++)
    {
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;   // their left / right keys are used instead
        const bool now = focus && KeyDown(vk);
        const bool hit = now && !prevAll[vk];
        prevAll[vk] = now;
        if (capturing && hit && !captured)
        {
            captured = true;
            if (vk != VK_ESCAPE)
            {
                SetRowKey(g_tab, g_row, vk);
                Log("[%s] %s presses keyboard key %s", kProfSection[TabProfile(g_tab)], kBtnName[g_row], KeyName(vk));
                g_picker = false;
            }
            g_capture = false;   // Esc: back to the tiles
        }
    }
    if (captured) { for (int i = 0; i < 9; i++) kHit[i] = false; for (int i = 1; i < 9; i++) kNow[i] = false; }

    if (!g_menuOpen)
    {
        g_closeRequest = false;
        if (!frontend && kHit[0])
        {
            g_menuOpen = true;
            g_picker = false;
            g_capture = false;
            g_tab = g_inVehicle ? kTabVehicle : kTabFoot;
            g_row = 0;
            g_mouseDX = 0; g_mouseDY = 0; g_mouseDZ = 0;
            if (g_msReload) FindManualShift();   // the INI may have been edited by hand
            RememberOpenState();
            ReadGameLanguage();
            Log("Menu opened");
        }
    }
    else if (frontend) CloseMenu(false);
    else
    {
        // Arrow keys with repeat while held
        auto repeat = [](bool held, DWORD& since, DWORD& next) -> bool
        {
            const DWORD now = GetTickCount();
            if (!held) { since = 0; return false; }
            if (!since) { since = now; next = now + 350; return true; }
            if ((LONG)(now - next) >= 0) { next = now + 90; return true; }
            return false;
        };
        static DWORD s[4] = {}, n[4] = {};
        const bool up    = repeat(kNow[1], s[0], n[0]);
        const bool down  = repeat(kNow[2], s[1], n[1]);
        const bool left  = repeat(kNow[3], s[2], n[2]);
        const bool right = repeat(kNow[4], s[3], n[3]);
        const int rows = RowCount(g_tab);
        if (rows > 0)
        {
            if (up)   g_row = (g_row + rows - 1) % rows;
            if (down) g_row = (g_row + 1) % rows;
            if (left) ChangeRow(g_tab, g_row, -1);
            if (right) ChangeRow(g_tab, g_row, +1);
        }
        if (kHit[5]) { g_tab = (g_tab + 1) % TabCount(); if (g_row >= RowCount(g_tab)) g_row = 0; }
        if (rows > 0 && kHit[6]) ResetRow(g_tab, g_row, false);   // Backspace: this button
        if (rows > 0 && kHit[7]) ResetRow(g_tab, g_row, true);    // Delete: the whole page
        if (kHit[8] && g_picker) g_picker = false;   // Esc: first closes the tile window
        else if (g_closeRequest) { g_closeRequest = false; g_picker = false; CloseMenu(true); }   // SAVE AND CLOSE
        else if (kHit[0] || kHit[8]) { g_picker = false; CloseMenu(false); }                     // Page Up / Esc: no saving
    }

    // While the menu is open, and after it until the keys and buttons that closed it are released, the game gets
    // nothing from the controller, mouse and keyboard (or it would see them pressed: Esc would open the pause menu)
    // Mouse buttons: as the game gets them through DirectInput. Windows' own button state (GetAsyncKeyState) can stay
    // "pressed" while the game holds the mouse exclusively, which kept the mouse from the game for ever (seen in game).
    // Never longer than 1.5 s after closing in any case.
    static bool waitPad = false, waitKeys = false;
    static DWORD closedAt = 0;
    if (g_menuOpen) { waitPad = waitKeys = true; closedAt = 0; }
    else if (!closedAt) closedAt = GetTickCount();
    const bool tooLong = closedAt && GetTickCount() - closedAt > 1500;
    if (!g_menuOpen && (!phys || tooLong)) waitPad = false;
    const bool mouseHeld = g_mouseSeen > 0 ? g_mouseButtons != 0 : (focus && (KeyDown(VK_LBUTTON) || KeyDown(VK_RBUTTON)));
    const bool keysHeld = mouseHeld || kNow[0] || kNow[8];
    if (!g_menuOpen && (!keysHeld || tooLong)) waitKeys = false;
    const bool diBlock = g_menuOpen || waitKeys;
    if (diBlock != g_diBlock)
        Log(diBlock ? "Mouse and keyboard: kept from the game" : "Mouse and keyboard: back to the game%s", tooLong ? " (after 1.5 s)" : "");
    g_blockAll = g_menuOpen || waitPad;
    g_diBlock = diBlock;
    g_menuPhys = phys;
    g_menuTick = GetTickCount();
}

// ---------------------------------------------------------------------
//  Keyboard keys pressed by controller buttons (SendInput, so the game and other mods see a real key)
// ---------------------------------------------------------------------
static void SendKey(int vk, bool down)
{
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    const UINT sc = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    const bool ext = vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_INSERT || vk == VK_DELETE ||
                     vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_RCONTROL || vk == VK_RMENU ||
                     vk == VK_DIVIDE;
    in.ki.wVk = (WORD)vk;
    in.ki.wScan = (WORD)sc;
    in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (sc ? KEYEVENTF_SCANCODE : 0) | (ext ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in, sizeof(in));
}

// Every frame: a held button holds its key down; released (or menu open, game not in front): the key is let go
static void UpdateKeyBinds(bool active)
{
    static int held[kNumBtn] = {};   // key currently held down for this button
    for (int b = 0; b < kNumBtn; b++)
    {
        const int want = active && ((g_phys >> b) & 1) ? g_keyBind[g_inVehicle ? kProfVehicle : kProfFoot][b] : 0;
        if (held[b] == want) continue;
        if (held[b]) SendKey(held[b], false);
        if (want) SendKey(want, true);
        held[b] = want;
    }
}
// ---------------------------------------------------------------------
//  Game frame
// ---------------------------------------------------------------------
static void OnGameFrame()
{
    UpdatePadHook();

    static int fusionFix = -1;
    if (fusionFix < 0)
    {
        fusionFix = GetModuleHandleA("GTAIV.EFLC.FusionFix.asi") ? 1 : 0;
        if (!fusionFix) Log("FusionFix not found - Controller Rebind requires FusionFix and stays inactive");
    }
    if (!fusionFix) { g_frontend = true; g_stateTick = GetTickCount(); return; }

    const bool paused = (g_userPause && *g_userPause) || (g_scriptPause && *g_scriptPause);
    const bool frontend = paused || (g_menuActive && *g_menuActive) || Native<uint8_t>(H::IS_PAUSE_MENU_ACTIVE) != 0;

    bool inVehicle = false;
    if (!frontend)
    {
        uint32_t pid = Native<uint32_t>(H::GET_PLAYER_ID);
        int player = Native<int>(H::CONVERT_INT_TO_PLAYERINDEX, pid);
        if (Native<uint8_t>(H::IS_PLAYER_PLAYING, player))
        {
            int ped = 0;
            NativeOk(H::GET_PLAYER_CHAR, player, &ped);
            inVehicle = ped && Native<uint8_t>(H::IS_CHAR_IN_ANY_CAR, ped) != 0;
        }
    }
    static int lastCtx = -1;
    const int ctx = frontend ? 2 : (inVehicle ? 0 : 1);
    if (ctx != lastCtx)
    {
        lastCtx = ctx;
        static int ctxLogs = 0;
        if (ctxLogs++ < 40) Log("Layout: %s", ctx == 2 ? "normal (game menu / pause)" : ctx == 0 ? "vehicle" : "on foot");
    }
    g_inVehicle = inVehicle;
    g_frontend = frontend;
    InstallInputHooks();

    const bool focus = GameHasFocus();
    UpdateMenu(focus, frontend);
    UpdateKeyBinds(focus && !frontend && !g_menuOpen && !g_blockAll);
    g_stateTick = GetTickCount();
}

// ---------------------------------------------------------------------
//  Graphics (Direct3D 9, in the render thread)
// ---------------------------------------------------------------------
struct Tex { IDirect3DTexture9* t = nullptr; int w = 0, h = 0; };
static Tex g_texWhite, g_texFont;
static Tex g_texPad[2];   // controller pictures: Xbox, PlayStation
static Tex g_texHl[2][16];   // outline of every button on them
static Tex g_texIcon[2][16]; // small button icons for the tile and key windows
static bool g_texTried = false;

// Font atlas: ASCII 32..127, 16 x 8 cells of kCell px, made with Windows' own font renderer when the menu first shows
// Characters 32 ... 255 (Latin-1: also the accents and umlauts of French, German, Italian and Spanish)
static const int kCell = 64, kCols = 16, kRows = 14;
static const int kGlyphX = 8;            // where in its cell a character starts
static float g_adv[256] = {};            // advance of each character in px of the atlas

// Menu texts are UTF-8 in the code. Next character as a Latin-1 code (0 = end, '?' = not in the font).
static int NextChar(const char*& s)
{
    const unsigned char c = (unsigned char)*s;
    if (!c) return 0;
    if (c < 0x80) { s++; return c; }
    int cp = '?', extra = 0;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    s++;
    for (int k = 0; k < extra && (*s & 0xC0) == 0x80; k++, s++) cp = (cp << 6) | (*s & 0x3F);
    return (cp >= 32 && cp < 127) || (cp >= 160 && cp < 256) ? cp : '?';
}

// ---- Language of the menu: the game's language ----
// FusionFix keeps the game's language in GTAIV.EFLC.FusionFix.cfg, [LANGUAGEOVERRIDE] Language (the game's own numbers:
// 0 English, 1 French, 2 German, 3 Italian, 4 Spanish, 5 Russian, 6 Japanese). Russian and Japanese need other letters
// than the menu font has, so they get English. [Menu] Language in ControllerRebind.ini can set it by hand.
enum { kLangEn = 0, kLangDe, kLangFr, kLangIt, kLangEs, kLangCount };
static volatile int g_lang = kLangEn;

static int LangFromName(const char* s)
{
    if (!_strnicmp(s, "Ger", 3) || !_strnicmp(s, "Deu", 3) || !_strnicmp(s, "de", 2)) return kLangDe;
    if (!_strnicmp(s, "Fre", 3) || !_strnicmp(s, "Fra", 3) || !_strnicmp(s, "fr", 2)) return kLangFr;
    if (!_strnicmp(s, "Ita", 3) || !_strnicmp(s, "it", 2)) return kLangIt;
    if (!_strnicmp(s, "Spa", 3) || !_strnicmp(s, "Esp", 3) || !_strnicmp(s, "es", 2)) return kLangEs;
    if (!_strnicmp(s, "Eng", 3) || !_strnicmp(s, "en", 2)) return kLangEn;
    return -1;
}

static void ReadGameLanguage()
{
    static const char* const kName[kLangCount] = { "English", "German", "French", "Italian", "Spanish" };
    char buf[32];
    GetPrivateProfileStringA("Menu", "Language", "Auto", buf, sizeof(buf), IniPath().c_str());
    int lang = LangFromName(buf);
    const char* from = "ControllerRebind.ini";
    if (lang < 0)
    {
        const std::string ff = g_dir + "GTAIV.EFLC.FusionFix.cfg";
        const int game = GetPrivateProfileIntA("LANGUAGEOVERRIDE", "Language", -1, ff.c_str());
        static const int kGame[7] = { kLangEn, kLangFr, kLangDe, kLangIt, kLangEs, kLangEn, kLangEn };
        if (game >= 0 && game < 7) { lang = kGame[game]; from = "the game (FusionFix)"; }
        else
        {
            const LANGID ui = PRIMARYLANGID(GetUserDefaultUILanguage());
            lang = ui == LANG_GERMAN ? kLangDe : ui == LANG_FRENCH ? kLangFr : ui == LANG_ITALIAN ? kLangIt : ui == LANG_SPANISH ? kLangEs : kLangEn;
            from = "Windows";
        }
    }
    if (lang != g_lang || !g_texTried) Log("Menu language: %s (from %s)", kName[lang], from);
    g_lang = lang;
}

// English text -> the same text in the menu's language (UTF-8). Not in the table: shown in English.
static const char* const kTr[][kLangCount] = {
    // English                      German                         French                          Italian                         Spanish
    { "CONTROLLER",                 "CONTROLLER",                  "MANETTE",                      "CONTROLLER",                   "MANDO" },
    { "ON FOOT",                    "ZU FUSS",                     "À PIED",                       "A PIEDI",                      "A PIE" },
    { "COMBAT",                     "KAMPF",                       "COMBAT",                       "COMBATTIMENTO",                "COMBATE" },
    { "IN VEHICLE",                 "IM FAHRZEUG",                 "EN VÉHICULE",                  "IN VEICOLO",                   "EN VEHÍCULO" },
    { "Combat uses the on-foot layout: changes here change on foot as well.",
                                    "Kampf nutzt die Belegung zu Fuß: Änderungen hier gelten auch zu Fuß.",
                                                                   "Le combat utilise la configuration à pied : les changements valent aussi à pied.",
                                                                                                   "Il combattimento usa la configurazione a piedi: le modifiche valgono anche a piedi.",
                                                                                                                                   "El combate usa la configuración a pie: los cambios también valen a pie." },
    { "Click a button on the controller or its label to choose what it does.",
                                    "Klicke auf einen Knopf am Controller oder seine Beschriftung, um festzulegen, was er macht.",
                                                                   "Clique sur un bouton de la manette ou son texte pour choisir ce qu'il fait.",
                                                                                                   "Clicca un tasto del controller o la sua scritta per scegliere cosa fa.",
                                                                                                                                   "Haz clic en un botón del mando o en su texto para elegir qué hace." },
    { "RESET ALL",                  "ALLES ZURÜCKSETZEN",          "TOUT RÉINITIALISER",           "RIPRISTINA TUTTO",             "RESTABLECER TODO" },
    { "RESET PAGE",                 "SEITE ZURÜCKSETZEN",          "RÉINITIALISER LA PAGE",        "RIPRISTINA PAGINA",            "RESTABLECER PÁGINA" },
    { "%s / ESC: CLOSE WITHOUT SAVING", "%s / ESC: SCHLIESSEN OHNE SPEICHERN", "%s / ÉCHAP : FERMER SANS ENREGISTRER", "%s / ESC: CHIUDI SENZA SALVARE", "%s / ESC: CERRAR SIN GUARDAR" },
    { "Page Up",                    "Bild auf",                    "Page préc.",                   "Pag su",                       "Re Pág" },
    { "Page Down",                  "Bild ab",                     "Page suiv.",                   "Pag giù",                      "Av Pág" },
    { "SAVE AND CLOSE",             "SPEICHERN UND SCHLIESSEN",    "ENREGISTRER ET FERMER",        "SALVA E CHIUDI",               "GUARDAR Y CERRAR" },
    { "PRESS A KEY ON THE KEYBOARD", "DRÜCKE EINE TASTE AUF DER TASTATUR", "APPUIE SUR UNE TOUCHE DU CLAVIER", "PREMI UN TASTO SULLA TASTIERA", "PULSA UNA TECLA DEL TECLADO" },
    { "NOW: %s",                    "JETZT: %s",                   "ACTUEL : %s",                  "ORA: %s",                      "AHORA: %s" },
    { "NO KEY",                     "KEINE TASTE",                 "AUCUNE TOUCHE",                "NESSUN TASTO",                 "NINGUNA TECLA" },
    { "ESC: BACK",                  "ESC: ZURÜCK",                 "ÉCHAP : RETOUR",               "ESC: INDIETRO",                "ESC: VOLVER" },
    { "BACK",                       "ZURÜCK",                      "RETOUR",                       "INDIETRO",                     "VOLVER" },
    { "CANCEL",                     "ABBRECHEN",                   "ANNULER",                      "ANNULLA",                      "CANCELAR" },
    { "Keyboard key: %s",           "Tastatur-Taste: %s",          "Touche clavier : %s",          "Tasto tastiera: %s",           "Tecla del teclado: %s" },
    { "Keyboard key ...",           "Tastatur-Taste ...",          "Touche clavier ...",           "Tasto tastiera ...",           "Tecla del teclado ..." },
    { "Key: %s",                    "Taste: %s",                   "Touche : %s",                  "Tasto: %s",                    "Tecla: %s" },
    { "OFF - DOES NOTHING",         "AUS - MACHT NICHTS",          "DÉSACTIVÉ - AUCUNE ACTION",    "DISATTIVATO - NESSUNA AZIONE", "DESACTIVADO - NO HACE NADA" },
    { "= STANDARD",                 "= STANDARD",                  "= PAR DÉFAUT",                 "= PREDEFINITO",                "= PREDETERMINADO" },
    { "SET TO STANDARD",            "AUF STANDARD",                "PAR DÉFAUT",                   "PREDEFINITO",                  "PREDETERMINADO" },
    { "Off",                        "Aus",                         "Désactivé",                    "Disattivato",                  "Desactivado" },
    // Actions, on foot
    { "Sprint / answer phone",      "Sprinten / Anruf annehmen",   "Sprint / répondre",            "Scatto / rispondi",            "Esprintar / contestar" },
    { "Reload / hang up",           "Nachladen / auflegen",        "Recharger / raccrocher",       "Ricarica / riaggancia",        "Recargar / colgar" },
    { "Jump / climb",               "Springen / klettern",         "Sauter / grimper",             "Salta / arrampicati",          "Saltar / trepar" },
    { "Enter vehicle / ladder",     "Einsteigen / Leiter",         "Monter / échelle",             "Sali / scala",                 "Subir / escalera" },
    { "Interact / pick up",         "Benutzen / aufheben",         "Interagir / ramasser",         "Interagisci / raccogli",       "Interactuar / recoger" },
    { "Enter / exit cover",         "Deckung",                     "À couvert",                    "Copertura",                    "Cobertura" },
    { "Lock on / free aim",         "Anvisieren / frei zielen",    "Verrouiller / viser",          "Aggancia / mira libera",       "Fijar / apuntar" },
    { "Fire weapon",                "Schießen",                    "Tirer",                        "Spara",                        "Disparar" },
    { "Crouch",                     "Ducken",                      "S'accroupir",                  "Accovacciati",                 "Agacharse" },
    { "Look behind / zoom",         "Zurückschauen / Zoom",        "Regard arrière / zoom",        "Guarda dietro / zoom",         "Mirar atrás / zoom" },
    { "Change camera",              "Kamera wechseln",             "Changer de caméra",            "Cambia visuale",               "Cambiar cámara" },
    { "Pause menu",                 "Pausenmenü",                  "Menu pause",                   "Menu di pausa",                "Menú de pausa" },
    { "Open phone",                 "Handy öffnen",                "Ouvrir le téléphone",          "Apri telefono",                "Abrir teléfono" },
    { "Skip call / zoom radar",     "Anruf weg / Radar-Zoom",      "Passer appel / radar",         "Salta chiamata / radar",       "Saltar llamada / radar" },
    { "Previous weapon",            "Vorherige Waffe",             "Arme précédente",              "Arma precedente",              "Arma anterior" },
    { "Next weapon",                "Nächste Waffe",               "Arme suivante",                "Arma successiva",              "Arma siguiente" },
    // Actions, combat
    { "Block / dodge",              "Blocken / ausweichen",        "Bloquer / esquiver",           "Para / schiva",                "Bloquear / esquivar" },
    { "Punch / counter",            "Schlag / Konter",             "Frapper / contrer",            "Pugno / contrattacco",         "Golpe / contraataque" },
    { "Kick",                       "Tritt",                       "Coup de pied",                 "Calcio",                       "Patada" },
    { "Alt. punch / counter",       "2. Schlag / Konter",          "Autre coup / contrer",         "Pugno 2 / contrattacco",       "Golpe 2 / contraataque" },
    { "Lock on / aim",              "Anvisieren / zielen",         "Verrouiller / viser",          "Aggancia / mira",              "Fijar / apuntar" },
    { "Look behind",                "Zurückschauen",               "Regarder derrière",            "Guarda dietro",                "Mirar atrás" },
    { "Zoom radar out",             "Radar rauszoomen",            "Dézoomer le radar",            "Riduci zoom radar",            "Alejar radar" },
    // Actions, in a vehicle
    { "Handbrake",                  "Handbremse",                  "Frein à main",                 "Freno a mano",                 "Freno de mano" },
    { "Cinematic camera",           "Kinokamera",                  "Caméra cinéma",                "Visuale cinematica",           "Cámara cinemática" },
    { "Weapon / lights (hold)",     "Waffe / Licht (halten)",      "Arme / phares (maintenir)",    "Arma / luci (tieni)",          "Arma / luces (mantener)" },
    { "Exit vehicle",               "Aussteigen",                  "Sortir du véhicule",           "Scendi dal veicolo",           "Salir del vehículo" },
    { "Drive-by shooting",          "Aus dem Auto schießen",       "Tir en voiture",               "Spara dal veicolo",            "Disparo en marcha" },
    { "Brake / reverse",            "Bremse / rückwärts",          "Frein / marche arrière",       "Freno / retromarcia",          "Frenar / marcha atrás" },
    { "Accelerate",                 "Gas geben",                   "Accélérer",                    "Accelera",                     "Acelerar" },
    { "Horn / siren",               "Hupe / Sirene",               "Klaxon / sirène",              "Clacson / sirena",             "Claxon / sirena" },
    { "Use phone",                  "Handy benutzen",              "Téléphone",                    "Usa telefono",                 "Usar teléfono" },
    { "Previous station",           "Vorheriger Sender",           "Station précédente",           "Stazione precedente",          "Emisora anterior" },
    { "Next station",               "Nächster Sender",             "Station suivante",             "Stazione successiva",          "Emisora siguiente" } };

static const char* Tr(const char* en)
{
    const int lang = g_lang;
    if (lang == kLangEn || !en) return en;
    for (auto& row : kTr) if (strcmp(row[0], en) == 0) return row[lang] ? row[lang] : en;
    return en;
}


// lvl: RGBA with premultiplied alpha; the mip levels are built from it
static bool UploadTex(IDirect3DDevice9* dev, std::vector<uint8_t>& lvl, int w, int h, Tex& out)
{
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex)
    {
        tex = nullptr;
        if (FAILED(dev->CreateTexture(w, h, 0, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex, nullptr)) || !tex) return false;
        Log("Managed textures not available - using dynamic textures");
    }
    DWORD levels = tex->GetLevelCount();
    int cw = w, chh = h;
    for (DWORD l = 0; l < levels; l++)
    {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(tex->LockRect(l, &lr, nullptr, 0)))
        {
            for (int y = 0; y < chh; y++)
            {
                uint8_t* dst = (uint8_t*)lr.pBits + y * lr.Pitch;
                const uint8_t* src = &lvl[y * cw * 4];
                for (int x = 0; x < cw; x++)
                {
                    dst[x * 4 + 0] = src[x * 4 + 2];
                    dst[x * 4 + 1] = src[x * 4 + 1];
                    dst[x * 4 + 2] = src[x * 4 + 0];
                    dst[x * 4 + 3] = src[x * 4 + 3];
                }
            }
            tex->UnlockRect(l);
        }
        int nw = cw > 1 ? cw / 2 : 1, nh = chh > 1 ? chh / 2 : 1;
        std::vector<uint8_t> next(nw * nh * 4);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++)
                for (int c = 0; c < 4; c++)
                {
                    int x0 = (std::min)(x * 2, cw - 1), x1 = (std::min)(x * 2 + 1, cw - 1);
                    int y0 = (std::min)(y * 2, chh - 1), y1 = (std::min)(y * 2 + 1, chh - 1);
                    int s = lvl[(y0 * cw + x0) * 4 + c] + lvl[(y0 * cw + x1) * 4 + c] + lvl[(y1 * cw + x0) * 4 + c] + lvl[(y1 * cw + x1) * 4 + c];
                    next[(y * nw + x) * 4 + c] = (uint8_t)((s + 2) / 4);
                }
        lvl.swap(next); cw = nw; chh = nh;
    }
    out.t = tex; out.w = w; out.h = h;
    return true;
}

static bool CreateFontTex(IDirect3DDevice9* dev, Tex& out)
{
    const int W = kCell * kCols, Hh = kCell * kRows;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -Hh;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { DeleteDC(dc); return false; }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    memset(bits, 0, (size_t)W * Hh * 4);
    HFONT font = CreateFontA(-44, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Bahnschrift");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    for (int c = 32; c < 256; c++)
    {
        const int i = c - 32, cx = (i % kCols) * kCell, cy = (i / kCols) * kCell;
        char ch = (char)c;   // Windows-1252 = Latin-1 from 160 on
        if (c < 127 || c >= 160) TextOutA(dc, cx + kGlyphX, cy + 4, &ch, 1);
        SIZE sz = {};
        GetTextExtentPoint32A(dc, &ch, 1, &sz);
        g_adv[c] = (float)sz.cx;
    }
    GdiFlush();
    std::vector<uint8_t> lvl((size_t)W * Hh * 4);
    const uint8_t* src = (const uint8_t*)bits;
    for (int p = 0; p < W * Hh; p++)
    {
        const uint8_t a = (std::max)((std::max)(src[p * 4 + 0], src[p * 4 + 1]), src[p * 4 + 2]);
        memset(&lvl[p * 4], a, 4);   // white, premultiplied
    }
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);
    return UploadTex(dev, lvl, W, Hh, out);
}

static bool CreatePngTex(IDirect3DDevice9* dev, const unsigned char* png, int len, Tex& out)
{
    int w, h, ch;
    unsigned char* px = stbi_load_from_memory(png, len, &w, &h, &ch, 4);
    if (!px) return false;
    std::vector<uint8_t> lvl(px, px + w * h * 4);
    stbi_image_free(px);
    for (int i = 0; i < w * h; i++)   // premultiplied alpha
    {
        uint8_t* p = &lvl[i * 4];
        p[0] = (uint8_t)(p[0] * p[3] / 255); p[1] = (uint8_t)(p[1] * p[3] / 255); p[2] = (uint8_t)(p[2] * p[3] / 255);
    }
    return UploadTex(dev, lvl, w, h, out);
}

static void LoadTextures(IDirect3DDevice9* dev)
{
    g_texTried = true;
    std::vector<uint8_t> white(4 * 4 * 4, 255);
    const bool ok = UploadTex(dev, white, 4, 4, g_texWhite) && CreateFontTex(dev, g_texFont);
    const bool pads = CreatePngTex(dev, kPng_pad_xbox, (int)sizeof(kPng_pad_xbox), g_texPad[0])
                   && CreatePngTex(dev, kPng_pad_ps, (int)sizeof(kPng_pad_ps), g_texPad[1]);
    int outlines = 0;
    for (int s = 0; s < 2; s++)
        for (int b = 0; b < kNumBtn; b++)
        {
            if (CreatePngTex(dev, kPadHl[s][b].png, kPadHl[s][b].len, g_texHl[s][b])) outlines++;
            CreatePngTex(dev, kIcon[s][b].png, kIcon[s][b].len, g_texIcon[s][b]);
        }
    Log(ok && pads ? "Menu graphics ready (%d button outlines)" : "Menu graphics could not be made", outlines);
}

struct Vtx { float x, y, z, rhw; D3DCOLOR c; float u, v; };
static const DWORD kFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

static D3DCOLOR Premul(int a, int r, int g, int b)
{
    return D3DCOLOR_ARGB(a, r * a / 255, g * a / 255, b * a / 255);
}

// Top-left corner, width, height
static void DrawRect(IDirect3DDevice9* dev, IDirect3DTexture9* tex, float x, float y, float w, float h,
                     float u0, float v0, float u1, float v1, D3DCOLOR col)
{
    Vtx v[4] = {
        { x - 0.5f,     y - 0.5f,     0.f, 1.f, col, u0, v0 },
        { x + w - 0.5f, y - 0.5f,     0.f, 1.f, col, u1, v0 },
        { x - 0.5f,     y + h - 0.5f, 0.f, 1.f, col, u0, v1 },
        { x + w - 0.5f, y + h - 0.5f, 0.f, 1.f, col, u1, v1 } };
    dev->SetTexture(0, tex);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vtx));
}

static void FillRect(IDirect3DDevice9* dev, float x, float y, float w, float h, D3DCOLOR col)
{
    DrawRect(dev, g_texWhite.t, x, y, w, h, 0.5f, 0.5f, 0.5f, 0.5f, col);
}

static float TextWidth(const char* s, float size)
{
    float w = 0.f;
    for (int c; (c = NextChar(s)) != 0; ) w += g_adv[c];
    return w * size / kCell;
}

// size = height of a cell in px; y = top of the text line. align: 0 left, 1 centre, 2 right
static void MenuText(IDirect3DDevice9* dev, const char* s, float x, float y, float size, D3DCOLOR col, int align = 0)
{
    const float scale = size / kCell;
    if (align) x -= TextWidth(s, size) * (align == 1 ? 0.5f : 1.f);
    x = floorf(x + 0.5f); y = floorf(y + 0.5f);
    for (int c; (c = NextChar(s)) != 0; )
    {
        const int i = c - 32;
        if (c != ' ')
        {
            const float u0 = (float)(i % kCols) / kCols, v0 = (float)(i / kCols) / kRows;
            DrawRect(dev, g_texFont.t, x - kGlyphX * scale, y, kCell * scale, kCell * scale, u0, v0, u0 + 1.f / kCols, v0 + 1.f / kRows, col);
        }
        x += g_adv[c] * scale;
    }
}

// Which drawing way fits: Vulkan with DLSSIV needs the drawing in the game's scene at EndScene and a full state block;
// real D3D9 flickers with exactly that and needs it before Present with only the changed states restored.
static bool IsVulkanGame()
{
    static int result = -1;
    if (result < 0)
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string exeDir = exePath;
        exeDir = exeDir.substr(0, exeDir.find_last_of("\\/") + 1);
        const std::string d3dCfg = exeDir + "d3d9.cfg", ffCfg = g_dir + "GTAIV.EFLC.FusionFix.cfg";
        const int ffApi = GetPrivateProfileIntA("MAIN", "GraphicsAPI", 0, ffCfg.c_str());
        const int api = GetPrivateProfileIntA("MAIN", "API", ffApi, d3dCfg.c_str());
        result = api != 0 ? 1 : 0;
        Log("Graphics: %s (d3d9.cfg API %d, FusionFix GraphicsAPI %d)", result ? "Vulkan way" : "D3D9 way", api, ffApi);
    }
    return result == 1;
}

static const char* TargetText(int t) { return t < 0 ? "Off" : kBtnLabel[t]; }

// ---- The menu's mouse pointer (render thread) ----
// It is moved by the mouse movement the game would have got (DirectInput); without that, Windows' own cursor is used.
struct UiInput { float x, y; bool click, rclick; int wheel; };
static float g_curX = -1.f, g_curY = -1.f;

static bool Inside(const UiInput& in, float x, float y, float w, float h)
{
    return in.x >= x && in.x < x + w && in.y >= y && in.y < y + h;
}

static UiInput ReadMouse(float W, float Hh)
{
    static DWORD lastFrame = 0;
    static int prevButtons = 0;
    UiInput in = {};
    const DWORD now = GetTickCount();
    const LONG dx = InterlockedExchange(&g_mouseDX, 0), dy = InterlockedExchange(&g_mouseDY, 0), dz = InterlockedExchange(&g_mouseDZ, 0);
    const bool directInput = g_mouseSeen > 0;
    int buttons = directInput ? (int)g_mouseButtons : ((KeyDown(VK_LBUTTON) ? 1 : 0) | (KeyDown(VK_RBUTTON) ? 2 : 0));
    if (g_curX < 0.f || now - lastFrame > 500)
    {
        // Menu just opened: pointer in the middle, a button that is still held does not click
        g_curX = W * 0.5f; g_curY = Hh * 0.5f;
        prevButtons = buttons;
    }
    lastFrame = now;
    if (directInput)
    {
        const float sens = Hh / 1080.f;
        g_curX += dx * sens;
        g_curY += dy * sens;
    }
    else
    {
        POINT p; RECT rc;
        HWND w = GetForegroundWindow();
        if (w && GetCursorPos(&p) && ScreenToClient(w, &p) && GetClientRect(w, &rc) && rc.right > 0 && rc.bottom > 0)
        {
            g_curX = p.x * W / rc.right;
            g_curY = p.y * Hh / rc.bottom;
        }
    }
    g_curX = Clamp(g_curX, 0.f, W - 1.f);
    g_curY = Clamp(g_curY, 0.f, Hh - 1.f);
    in.x = g_curX; in.y = g_curY;
    in.click = (buttons & 1) && !(prevButtons & 1);
    in.rclick = (buttons & 2) && !(prevButtons & 2);
    prevButtons = buttons;
    in.wheel = dz > 0 ? -1 : (dz < 0 ? 1 : 0);   // wheel up = back, down = next
    return in;
}

static void DrawTris(IDirect3DDevice9* dev, const float* pts, const int* idx, int tris, float ox, float oy, float sc, D3DCOLOR col)
{
    Vtx v[48];
    if (tris > 16) tris = 16;
    for (int t = 0; t < tris * 3; t++)
    {
        const int k = idx[t];
        v[t] = { ox + pts[k * 2] * sc - 0.5f, oy + pts[k * 2 + 1] * sc - 0.5f, 0.f, 1.f, col, 0.5f, 0.5f };
    }
    dev->SetTexture(0, g_texWhite.t);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, tris, v, sizeof(Vtx));
}

static void DrawPointer(IDirect3DDevice9* dev, float x, float y, float s)
{
    // Classic arrow: tip at (0,0)
    static const float pts[] = { 0, 0,  0, 17,  4, 13,  7, 20,  10, 19,  7, 12,  12, 12 };
    static const int idx[] = { 0, 1, 2,  0, 2, 5,  0, 5, 6,  2, 3, 4,  2, 4, 5 };
    const float sc = 1.35f * s, o = 1.6f * s;
    for (int k = 0; k < 8; k++)
    {
        static const int dxs[8] = { -1, 0, 1, -1, 1, -1, 0, 1 }, dys[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
        DrawTris(dev, pts, idx, 5, x + dxs[k] * o, y + dys[k] * o, sc, Premul(255, 0, 0, 0));
    }
    DrawTris(dev, pts, idx, 5, x, y, sc, Premul(255, 240, 244, 250));
}

// A flat button; returns true when it was clicked
static bool UiButton(IDirect3DDevice9* dev, const UiInput& in, const char* label, float x, float y, float w, float h, float s,
                     D3DCOLOR fill, D3DCOLOR fillHover, D3DCOLOR textCol, float textSize)
{
    const bool hover = Inside(in, x, y, w, h);
    FillRect(dev, x, y, w, h, hover ? fillHover : fill);
    MenuText(dev, label, x + w * 0.5f, y + (h - textSize) * 0.5f - 1.f * s, textSize, textCol, 1);
    return hover && in.click;
}

// ---- What the buttons do in GTA IV with the game's standard controller layout ----
// Pages 0 = on foot, 1 = combat, 2 = in a vehicle; index order as kBtnName. On foot is taken from the game's own
// controller pictures (Xbox and PlayStation show different parts of it, both are combined here).
static const char* const kAction[3][kNumBtn] = {
    { "Sprint / answer phone", "Reload / hang up", "Jump / climb", "Enter vehicle / ladder", "Interact / pick up", "Enter / exit cover",
      "Lock on / free aim", "Fire weapon", "Crouch", "Look behind / zoom", "Change camera", "Pause menu",
      "Open phone", "Skip call / zoom radar", "Previous weapon", "Next weapon" },
    { "Block / dodge", "Punch / counter", "Kick", "Alt. punch / counter", "Interact / pick up", "Enter / exit cover",
      "Lock on / aim", "Fire weapon", "Crouch", "Look behind", "Change camera", "Pause menu",
      "Open phone", "Zoom radar out", "Previous weapon", "Next weapon" },
    { "Handbrake", "Cinematic camera", "Weapon / lights (hold)", "Exit vehicle", "Drive-by shooting", "Handbrake",
      "Brake / reverse", "Accelerate", "Horn / siren", "Look behind", "Change camera", "Pause menu",
      "Use phone", "Skip call / zoom radar", "Previous station", "Next station" } };
// The sticks themselves (not buttons, not changed by the mod), shown like in the game's pictures
static const char* const kStickInfo[3][2] = {
    { "Move", "Look around / aim" },
    { "Move", "Aim / switch targets" },
    { "Steer", "Look around / aim" } };
static int ActionPage(int tab) { return tab > 2 ? 0 : tab; }
static const char* ActionText(int tab, int t) { return t < 0 ? Tr("Off") : Tr(kAction[ActionPage(tab)][t]); }

// Button names as printed on the controller
static const char* const kPadName[2][kNumBtn] = {
    { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Back", "Start", "Up", "Down", "Left", "Right" },
    { "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Share", "Options", "Up", "Down", "Left", "Right" } };

// Upper case of a UTF-8 text, also for the Latin-1 letters (ä -> Ä, é -> É; ß stays)
static void Upper(char* dst, const char* src, size_t n)
{
    size_t o = 0;
    const char* s = src;
    while (*s && o + 3 < n)
    {
        const char* start = s;
        int c = NextChar(s);
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c -= 32;
        else if (c == '?' && (unsigned char)*start >= 0x80) { while (start < s && o + 1 < n) dst[o++] = *start++; continue; }
        if (c < 0x80) dst[o++] = (char)c;
        else { dst[o++] = (char)(0xC0 | (c >> 6)); dst[o++] = (char)(0x80 | (c & 0x3F)); }
    }
    dst[o] = 0;
}

// ---- Simple shapes (immediately drawn, white texture tinted by the colour) ----
static void DrawFan(IDirect3DDevice9* dev, const std::vector<float>& pts, D3DCOLOR col)   // convex polygon, x/y pairs
{
    const int n = (int)pts.size() / 2;
    if (n < 3) return;
    float cx = 0.f, cy = 0.f;
    for (int i = 0; i < n; i++) { cx += pts[i * 2]; cy += pts[i * 2 + 1]; }
    cx /= n; cy /= n;
    std::vector<Vtx> v;
    v.reserve(n * 3);
    for (int i = 0; i < n; i++)
    {
        const int j = (i + 1) % n;
        v.push_back({ cx - 0.5f, cy - 0.5f, 0.f, 1.f, col, 0.5f, 0.5f });
        v.push_back({ pts[i * 2] - 0.5f, pts[i * 2 + 1] - 0.5f, 0.f, 1.f, col, 0.5f, 0.5f });
        v.push_back({ pts[j * 2] - 0.5f, pts[j * 2 + 1] - 0.5f, 0.f, 1.f, col, 0.5f, 0.5f });
    }
    dev->SetTexture(0, g_texWhite.t);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, n, v.data(), sizeof(Vtx));
}

static void ArcPts(std::vector<float>& p, float cx, float cy, float r, float a0, float a1, int segs)
{
    for (int i = 0; i <= segs; i++)
    {
        const float a = a0 + (a1 - a0) * i / segs;
        p.push_back(cx + cosf(a) * r);
        p.push_back(cy + sinf(a) * r);
    }
}

static const float kPi = 3.14159265f;

static void FillCircle(IDirect3DDevice9* dev, float cx, float cy, float r, D3DCOLOR col)
{
    std::vector<float> p;
    ArcPts(p, cx, cy, r, 0.f, 2.f * kPi, 36);
    p.resize(p.size() - 2);
    DrawFan(dev, p, col);
}

static void FillRoundRect(IDirect3DDevice9* dev, float x, float y, float w, float h, float r, D3DCOLOR col)
{
    r = (std::min)(r, (std::min)(w, h) * 0.5f);
    std::vector<float> p;
    ArcPts(p, x + w - r, y + r, r, -kPi * 0.5f, 0.f, 6);
    ArcPts(p, x + w - r, y + h - r, r, 0.f, kPi * 0.5f, 6);
    ArcPts(p, x + r, y + h - r, r, kPi * 0.5f, kPi, 6);
    ArcPts(p, x + r, y + r, r, kPi, kPi * 1.5f, 6);
    DrawFan(dev, p, col);
}

// A thick round-ended bar from (x1,y1) to (x2,y2)
static void FillCapsule(IDirect3DDevice9* dev, float x1, float y1, float x2, float y2, float r, D3DCOLOR col)
{
    const float a = atan2f(y2 - y1, x2 - x1);
    std::vector<float> p;
    ArcPts(p, x2, y2, r, a - kPi * 0.5f, a + kPi * 0.5f, 16);
    ArcPts(p, x1, y1, r, a + kPi * 0.5f, a + kPi * 1.5f, 16);
    DrawFan(dev, p, col);
}

static void DrawLine(IDirect3DDevice9* dev, float x1, float y1, float x2, float y2, float t, D3DCOLOR col)
{
    const float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) return;
    // lengthened by half the thickness at both ends, so two lines meet in a clean corner
    const float ux = dx / len * t * 0.5f, uy = dy / len * t * 0.5f;
    const float nx = -uy, ny = ux;
    std::vector<float> p = { x1 - ux + nx, y1 - uy + ny, x2 + ux + nx, y2 + uy + ny, x2 + ux - nx, y2 + uy - ny, x1 - ux - nx, y1 - uy - ny };
    DrawFan(dev, p, col);
}

static void DrawRing(IDirect3DDevice9* dev, float cx, float cy, float r, float t, D3DCOLOR col)
{
    const int segs = 36;
    std::vector<Vtx> v;
    for (int i = 0; i < segs; i++)
    {
        const float a0 = 2.f * kPi * i / segs, a1 = 2.f * kPi * (i + 1) / segs;
        const float o0x = cx + cosf(a0) * r, o0y = cy + sinf(a0) * r, o1x = cx + cosf(a1) * r, o1y = cy + sinf(a1) * r;
        const float i0x = cx + cosf(a0) * (r - t), i0y = cy + sinf(a0) * (r - t), i1x = cx + cosf(a1) * (r - t), i1y = cy + sinf(a1) * (r - t);
        const Vtx q[6] = { { o0x, o0y, 0, 1, col, .5f, .5f }, { o1x, o1y, 0, 1, col, .5f, .5f }, { i0x, i0y, 0, 1, col, .5f, .5f },
                           { i0x, i0y, 0, 1, col, .5f, .5f }, { o1x, o1y, 0, 1, col, .5f, .5f }, { i1x, i1y, 0, 1, col, .5f, .5f } };
        v.insert(v.end(), q, q + 6);
    }
    dev->SetTexture(0, g_texWhite.t);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, segs * 2, v.data(), sizeof(Vtx));
}

// ---- Colours of the menu (like the game's controller pictures: light lines on dark grey) ----
static const D3DCOLOR kLine   = D3DCOLOR_ARGB(255, 200, 200, 200);   // outlines
static const D3DCOLOR kBody   = D3DCOLOR_ARGB(255, 44, 44, 44);      // the controller's fill
static const D3DCOLOR kLabel  = D3DCOLOR_ARGB(255, 200, 200, 200);   // label text
static const D3DCOLOR kDim    = D3DCOLOR_ARGB(255, 125, 125, 125);
static const D3DCOLOR kLead   = D3DCOLOR_ARGB(255, 150, 150, 150);   // leader lines
static const D3DCOLOR kAccent = D3DCOLOR_ARGB(255, 128, 179, 252);   // chosen / changed (the blue of Manual Shift's gauge)
static const D3DCOLOR kOffCol = D3DCOLOR_ARGB(255, 235, 95, 85);

// ---- Button icons ----
static D3DCOLOR FaceColor(int style, int b)
{
    static const int xbox[4][3] = { { 120, 195, 60 }, { 225, 50, 45 }, { 55, 140, 235 }, { 240, 180, 30 } };
    static const int ps[4][3] = { { 120, 140, 215 }, { 220, 50, 70 }, { 225, 120, 190 }, { 90, 190, 120 } };
    const int* c = style ? ps[b] : xbox[b];
    return D3DCOLOR_ARGB(255, c[0], c[1], c[2]);
}

// PlayStation symbol of a face button in a circle of radius r
static void DrawPsSymbol(IDirect3DDevice9* dev, int b, float cx, float cy, float r, D3DCOLOR col)
{
    const float t = (std::max)(1.5f, r * 0.13f), k = r * 0.5f;
    switch (b)
    {
    case 0: DrawLine(dev, cx - k, cy - k, cx + k, cy + k, t, col); DrawLine(dev, cx - k, cy + k, cx + k, cy - k, t, col); break;
    case 1: DrawRing(dev, cx, cy, k * 1.1f, t, col); break;
    case 2:
        DrawLine(dev, cx - k, cy - k, cx + k, cy - k, t, col); DrawLine(dev, cx + k, cy - k, cx + k, cy + k, t, col);
        DrawLine(dev, cx + k, cy + k, cx - k, cy + k, t, col); DrawLine(dev, cx - k, cy + k, cx - k, cy - k, t, col);
        break;
    case 3:
    {
        const float ax = cx, ay = cy - k * 1.1f, bx = cx + k * 1.15f, by = cy + k * 0.8f, qx = cx - k * 1.15f, qy = by;
        DrawLine(dev, ax, ay, bx, by, t, col); DrawLine(dev, bx, by, qx, qy, t, col); DrawLine(dev, qx, qy, ax, ay, t, col);
        break;
    }
    }
}

// Cross of the D-pad (outline and fill), one arm lit
static void DrawDpad(IDirect3DDevice9* dev, int arm, float cx, float cy, float size, float o, D3DCOLOR line, D3DCOLOR fill, D3DCOLOR lit)
{
    const float w = size * 0.34f, h = size;
    for (int pass = 0; pass < 2; pass++)
    {
        const float g = pass ? 0.f : o;
        const D3DCOLOR c = pass ? fill : line;
        FillRoundRect(dev, cx - w * 0.5f - g, cy - h * 0.5f - g, w + 2 * g, h + 2 * g, w * 0.2f, c);
        FillRoundRect(dev, cx - h * 0.5f - g, cy - w * 0.5f - g, h + 2 * g, w + 2 * g, w * 0.2f, c);
    }
    const float a = (h - w) * 0.5f - o;
    switch (arm)
    {
    case 12: FillRoundRect(dev, cx - w * 0.5f, cy - h * 0.5f, w, a, w * 0.2f, lit); break;
    case 13: FillRoundRect(dev, cx - w * 0.5f, cy + w * 0.5f + o, w, a, w * 0.2f, lit); break;
    case 14: FillRoundRect(dev, cx - h * 0.5f, cy - w * 0.5f, a, w, w * 0.2f, lit); break;
    case 15: FillRoundRect(dev, cx + w * 0.5f + o, cy - w * 0.5f, a, w, w * 0.2f, lit); break;
    }
}

// The button as a small icon (for the bar at the bottom), centred at x/y with height h
static float IconWidth(int style, int b, float h)
{
    const PadIcon& ic = kIcon[style ? 1 : 0][b];
    if (g_texIcon[style ? 1 : 0][b].t) return h * ic.w / ic.h;
    if (b < 4 || b >= 12) return h;
    return (std::max)(h * 1.5f, TextWidth(kPadName[style][b], h * 0.7f) + h * 0.6f);
}

static void DrawBtnIcon(IDirect3DDevice9* dev, int style, int b, float x, float y, float h, bool lit)
{
    const Tex& tex = g_texIcon[style ? 1 : 0][b];
    if (tex.t)
    {
        const float w = IconWidth(style, b, h);
        DrawRect(dev, tex.t, floorf(x - w * 0.5f + 0.5f), floorf(y - h * 0.5f + 0.5f), w, h, 0.f, 0.f, 1.f, 1.f,
                 lit ? kAccent : D3DCOLOR_ARGB(255, 255, 255, 255));   // pressed: tinted blue
        return;
    }
    const D3DCOLOR fill = lit ? D3DCOLOR_ARGB(255, 60, 90, 140) : kBody;
    if (b < 4)
    {
        FillCircle(dev, x, y, h * 0.5f, kLine);
        FillCircle(dev, x, y, h * 0.5f - 2.f, fill);
        if (style) DrawPsSymbol(dev, b, x, y, h * 0.5f, FaceColor(style, b));
        else MenuText(dev, kPadName[0][b], x, y - h * 0.52f, h * 0.85f, FaceColor(style, b), 1);
        return;
    }
    if (b >= 12) { DrawDpad(dev, b, x, y, h, 2.f, kLine, kBody, lit ? kAccent : kLine); return; }
    const float w = IconWidth(style, b, h);
    FillRoundRect(dev, x - w * 0.5f, y - h * 0.5f, w, h, h * 0.3f, kLine);
    FillRoundRect(dev, x - w * 0.5f + 2.f, y - h * 0.5f + 2.f, w - 4.f, h - 4.f, h * 0.3f, fill);
    MenuText(dev, kPadName[style][b], x, y - h * 0.47f, h * 0.7f, kLabel, 1);
}

// ---- The controller picture. Positions in units of the picture's width, centre (0,0). ----
struct PadPos { float x, y; };
static PadPos PadButtonPos(int style, int b)
{
    const float* l = kPadLayout[style ? 1 : 0][b];
    return { (l[0] - kPadPicW * 0.5f) / kPadPicW, (l[1] - kPadPicH * 0.5f) / kPadPicW };
}
static float PadButtonR(int style, int b) { return kPadLayout[style ? 1 : 0][b][2] / kPadPicW; }

// The picture, and over it: pressed buttons lit, the chosen and the pointed-at one ringed in blue
static void DrawController(IDirect3DDevice9* dev, int style, float cx, float cy, float S, WORD phys, int selected, int hovered)
{
    const Tex& pic = g_texPad[style ? 1 : 0];
    const float h = S * kPadPicH / kPadPicW;
    if (pic.t) DrawRect(dev, pic.t, cx - S * 0.5f, cy - h * 0.5f, S, h, 0.f, 0.f, 1.f, 1.f, D3DCOLOR_ARGB(255, 255, 255, 255));
    // The chosen and the pointed-at button get their own outline in the blue of their line (their real shape:
    // trigger, bumper, each D-pad arm ...), drawn over the grey outline of the picture
    const float k = S / kPadPicW;
    const int st = style ? 1 : 0;
    for (int b = 0; b < kNumBtn; b++)
    {
        const PadPos p = PadButtonPos(style, b);
        const float x = cx + p.x * S, y = cy + p.y * S, r = PadButtonR(style, b) * S;
        if ((phys >> b) & 1) FillCircle(dev, x, y, r * 0.8f, Premul(150, 128, 179, 252));
        if (b != selected && b != hovered) continue;
        const PadHl& hl = kPadHl[st][b];
        if (g_texHl[st][b].t)
            DrawRect(dev, g_texHl[st][b].t, cx + (hl.x - kPadPicW * 0.5f) * k, cy + (hl.y - kPadPicH * 0.5f) * k, hl.w * k, hl.h * k,
                     0.f, 0.f, 1.f, 1.f, kAccent);
        else DrawRing(dev, x, y, r + 3.f * k, (std::max)(2.5f, 6.f * k), kAccent);
    }
}

static void DrawMenuContent(IDirect3DDevice9* dev, float W, float Hh)
{
    const UiInput realIn = ReadMouse(W, Hh);
    // While the tile window is open, everything below it neither lights up nor takes clicks
    const UiInput blocked = { -1e6f, -1e6f, false, false, 0 };
    const bool pickerOpen = g_picker;
    static bool s_dropOpen = false;              // the Xbox / PlayStation list is open
    static DWORD s_lastFrame = 0;
    if (GetTickCount() - s_lastFrame > 500) s_dropOpen = false;   // menu just opened
    s_lastFrame = GetTickCount();
    if (pickerOpen) s_dropOpen = false;
    const bool dropOpen = s_dropOpen;
    const UiInput in = (pickerOpen || dropOpen) ? blocked : realIn;
    const float s = Hh / 1080.f;
    const float pw = (std::min)(1440.f * s, W - 20.f), ph = 956.f * s;   // the last 26 px: the version in the corner
    const float px = floorf((W - pw) * 0.5f), py = floorf((Hh - ph) * 0.5f);
    const D3DCOLOR btnFill = D3DCOLOR_ARGB(255, 34, 34, 34), btnHover = D3DCOLOR_ARGB(255, 56, 56, 56);
    const D3DCOLOR activeFill = D3DCOLOR_ARGB(255, 30, 48, 78), activeHover = D3DCOLOR_ARGB(255, 44, 70, 112);
    const WORD phys = g_menuPhys;
    const int style = g_style;
    char up[96];

    FillRect(dev, 0, 0, W, Hh, Premul(130, 0, 0, 0));                 // dim the game a little
    FillRect(dev, px, py, pw, ph, Premul(242, 12, 12, 12));           // panel

    const float lx = px + 36.f * s, rx = px + pw - 36.f * s;
    float y = py + 24.f * s;
    MenuText(dev, "CONTROLLER REBIND", lx, y - 6.f * s, 44.f * s, kLabel);

    // Xbox / PlayStation: a drop-down list
    const float ddW = 300.f * s, ddH = 42.f * s, ddX = rx - ddW, ddY = y;
    static const char* const kStyle[2] = { "XBOX", "PLAYSTATION" };
    {
        const UiInput& din = pickerOpen ? blocked : realIn;
        const bool hover = Inside(din, ddX, ddY, ddW, ddH);
        MenuText(dev, Tr("CONTROLLER"), ddX - 16.f * s, ddY + 7.f * s, 26.f * s, kDim, 2);
        FillRect(dev, ddX, ddY, ddW, ddH, dropOpen ? activeFill : (hover ? btnHover : btnFill));
        if (dropOpen) FillRect(dev, ddX, ddY + ddH - 3.f * s, ddW, 3.f * s, kAccent);
        MenuText(dev, kStyle[style], ddX + 18.f * s, ddY + 4.f * s, 28.f * s, kLabel);
        // small triangle: down when closed, up when open
        const float ax = ddX + ddW - 26.f * s, ay = ddY + ddH * 0.5f, k = 7.f * s;
        std::vector<float> tri = dropOpen ? std::vector<float>{ ax - k, ay + k * 0.5f, ax + k, ay + k * 0.5f, ax, ay - k * 0.6f }
                                          : std::vector<float>{ ax - k, ay - k * 0.5f, ax + k, ay - k * 0.5f, ax, ay + k * 0.6f };
        DrawFan(dev, tri, kLabel);
        if (hover && din.click && !dropOpen) s_dropOpen = true;
    }
    y += 64.f * s;

    // Pages, in the middle like the game's page title
    static const char* const kTabName[3] = { "ON FOOT", "COMBAT", "IN VEHICLE" };
    const int tabs = TabCount();
    const float tw = 220.f * s, th = 44.f * s, tabsX = px + (pw - tabs * tw - (tabs - 1) * 8.f * s) * 0.5f;
    for (int t = 0; t < tabs; t++)
    {
        const float tx = tabsX + t * (tw + 8.f * s);
        const bool hover = Inside(in, tx, y, tw, th);
        if (hover && in.click && g_tab != t) { g_tab = t; if (g_row >= RowCount(t)) g_row = 0; }
        const bool active = t == g_tab;
        if (active || hover) FillRect(dev, tx, y, tw, th, active ? D3DCOLOR_ARGB(255, 30, 30, 30) : D3DCOLOR_ARGB(255, 24, 24, 24));
        if (active) FillRect(dev, tx + tw * 0.2f, y + th - 3.f * s, tw * 0.6f, 3.f * s, kAccent);
        MenuText(dev, Tr(kTabName[t]), tx + tw * 0.5f, y + 2.f * s, 30.f * s, active ? kLabel : kDim, 1);
    }
    y += th;

    const int tab = g_tab;
    const float barY = py + ph - 146.f * s;   // end of the controller area (above the help line)
    {
        const int pr = TabProfile(tab);
        // Controller in the middle, the labels at both sides with lines to their buttons, as in the game's pictures
        const float S = 660.f * s, ccx = px + pw * 0.5f, ccy = y + (barY - y) * 0.5f;
        // One row per button (the sticks' movement is not changed by the mod, so it has no row)
        struct Lbl { int b; float py; };
        Lbl left[10], right[10];
        int nl = 0, nr = 0;
        for (int b = 0; b < kNumBtn; b++)
        {
            const bool isLeft = b == 4 || b == 6 || b == 8 || b == 10 || b >= 12;
            (isLeft ? left[nl++] : right[nr++]) = { b, PadButtonPos(style, b).y };
        }
        auto sortY = [](Lbl* a, int n)
        {
            for (int i = 1; i < n; i++)
                for (int j = i; j > 0 && a[j].py < a[j - 1].py; j--) std::swap(a[j], a[j - 1]);
        };
        sortY(left, nl); sortY(right, nr);
        const float rowH = 46.f * s, textSize = 26.f * s;   // 8 rows per side over the height of the buttons
        const float top = ccy - S * 0.31f;   // the rows cover the height of the buttons on the picture
        const float textL = ccx - S * 0.52f, textR = ccx + S * 0.52f;   // inner ends of the label text
        const float colW = 430.f * s;

        // What is pointed at: a button on the controller or a label
        int hovered = -1;
        for (int b = 0; b < kNumBtn; b++)
        {
            const PadPos p = PadButtonPos(style, b);
            const float r = PadButtonR(style, b) * S * 1.15f;
            const float dx = in.x - (ccx + p.x * S), dy = in.y - (ccy + p.y * S);
            if (dx * dx + dy * dy <= r * r) hovered = b;
        }
        for (int side = 0; side < 2; side++)
        {
            const Lbl* list = side ? right : left;
            const int n = side ? nr : nl;
            for (int k = 0; k < n; k++)
            {
                const float ry = top + k * rowH;
                const float x0 = side ? textR - 10.f * s : textL - colW + 10.f * s;
                if (list[k].b >= 0 && Inside(in, x0, ry, colW, rowH)) hovered = list[k].b;
            }
        }
        if (hovered >= 0 && (in.click || in.rclick)) { g_row = hovered; g_picker = true; }

        // Only the button under the mouse lights up; while its tile window is open, the button being changed
        const int lit = pickerOpen ? (int)g_row : hovered;
        DrawController(dev, style, ccx, ccy, S, phys, lit, -1);
        // Leader lines: from the label straight out, then up or down into the button
        for (int pass = 0; pass < 2; pass++)   // the highlighted ones last, on top
            for (int side = 0; side < 2; side++)
            {
                const Lbl* list = side ? right : left;
                const int n = side ? nr : nl;
                for (int k = 0; k < n; k++)
                {
                    const int b = list[k].b;
                    const bool hl = b >= 0 && b == lit;
                    if (hl != (pass == 1)) continue;
                    const int pb = b >= 0 ? b : (b == -1 ? 8 : 9);
                    const PadPos p = PadButtonPos(style, pb);
                    const float ly = top + k * rowH + rowH * 0.5f;
                    const float sx = side ? textR - 4.f * s : textL + 4.f * s;
                    const float bx = ccx + p.x * S, by = ccy + p.y * S, r = PadButtonR(style, pb) * S;
                    const D3DCOLOR c = hl ? kAccent : kLead;
                    const float t = (hl ? 2.5f : 1.6f) * (std::max)(1.f, s);
                    // As in the game's pictures: straight out, then up or down. The line ends at the button's edge,
                    // only a stick's click line goes into its middle (the stick itself: to its edge).
                    const bool toMiddle = b == 8 || b == 9;
                    const float dir = side ? 1.f : -1.f;   // the side the label is on
                    if (!toMiddle && fabsf(ly - by) < r * 0.9f)
                    {
                        const float dy = ly - by;
                        DrawLine(dev, sx, ly, bx + dir * sqrtf(r * r - dy * dy), ly, t, c);
                    }
                    else
                    {
                        DrawLine(dev, sx, ly, bx, ly, t, c);
                        DrawLine(dev, bx, ly, bx, toMiddle ? by : (ly < by ? by - r : by + r), t, c);
                    }
                }
            }
        // Labels
        for (int side = 0; side < 2; side++)
        {
            const Lbl* list = side ? right : left;
            const int n = side ? nr : nl;
            for (int k = 0; k < n; k++)
            {
                const int b = list[k].b;
                const float ry = top + k * rowH;
                const float tx = side ? textR + 4.f * s : textL - 4.f * s;
                const int align = side ? 0 : 2;
                if (b < 0)
                {
                    Upper(up, kStickInfo[ActionPage(tab)][b == -1 ? 0 : 1], sizeof(up));
                    MenuText(dev, up, tx, ry + (rowH - textSize) * 0.5f - 2.f * s, textSize, kDim, align);
                    continue;
                }
                const int t = g_map[pr][b];
                const int key = g_keyBind[pr][b];
                const bool changed = key != 0 || t != b, sel = b == lit;   // lit: under the mouse (or being changed)
                if (key) { char kb[64]; snprintf(kb, sizeof(kb), Tr("Key: %s"), KeyName(key)); Upper(up, kb, sizeof(up)); }
                else Upper(up, ActionText(tab, t), sizeof(up));
                const float w = TextWidth(up, textSize);
                if (sel)
                {
                    const float bx0 = side ? tx - 8.f * s : tx - w - 8.f * s;
                    FillRect(dev, bx0, ry + 3.f * s, w + 16.f * s, rowH - 6.f * s, D3DCOLOR_ARGB(255, 30, 48, 78));
                }
                const D3DCOLOR c = key ? kAccent : t < 0 ? kOffCol : (changed ? kAccent : (sel ? D3DCOLOR_ARGB(255, 255, 255, 255) : kLabel));
                MenuText(dev, up, tx, ry + (rowH - textSize) * 0.5f - 2.f * s, textSize, c, align);
            }
        }
    }

    // ---- Bottom: help and the two buttons ----
    const float fy = py + ph - 126.f * s;
    MenuText(dev, Tr(tab == kTabCombat ? "Combat uses the on-foot layout: changes here change on foot as well."
                                       : "Click a button on the controller or its label to choose what it does."),
             lx, fy - 4.f * s, 24.f * s, kDim);
    {
        char ver[16];
        snprintf(ver, sizeof(ver), "v%s", kVersion);
        MenuText(dev, ver, px + pw - 12.f * s, py + ph - 28.f * s, 20.f * s, kDim, 2);   // version, in the bottom right corner
    }
    const float by = fy + 38.f * s, bh = 46.f * s;
    auto btnW = [&](const char* label) { return (std::max)(250.f * s, TextWidth(label, 28.f * s) + 40.f * s); };
    const float bwAll = btnW(Tr("RESET ALL")), bwPage = btnW(Tr("RESET PAGE")), bw = btnW(Tr("SAVE AND CLOSE"));
    // RESET ALL: every page back to the standard layout (in a vehicle and on foot, keyboard keys too); then RESET PAGE
    if (UiButton(dev, in, Tr("RESET ALL"), lx, by, bwAll, bh, s, btnFill, btnHover, kLabel, 28.f * s))
    {
        ResetRow(kTabFoot, 0, true);
        ResetRow(kTabVehicle, 0, true);
        Log("All pages reset to the standard layout");
    }
    if (UiButton(dev, in, Tr("RESET PAGE"), lx + bwAll + 10.f * s, by, bwPage, bh, s, btnFill, btnHover, kLabel, 28.f * s)) ResetRow(tab, 0, true);
    {
        // the menu key from the INI, by its name (Page Up / Page Down in the menu's language)
        const char* key = cfg.keyMenu == VK_PRIOR ? Tr("Page Up") : cfg.keyMenu == VK_NEXT ? Tr("Page Down") : KeyName(cfg.keyMenu);
        char hint[128];
        snprintf(hint, sizeof(hint), Tr("%s / ESC: CLOSE WITHOUT SAVING"), key);
        Upper(up, hint, sizeof(up));
        MenuText(dev, up, rx - bw - 16.f * s, by + 9.f * s, 22.f * s, kDim, 2);
    }
    if (UiButton(dev, in, Tr("SAVE AND CLOSE"), rx - bw, by, bw, bh, s, activeFill, activeHover, kLabel, 28.f * s))
        g_closeRequest = true;

    // ---- KEYBOARD KEY tile: the window that waits for a key (the key itself is taken in the game thread) ----
    if (pickerOpen && g_picker && g_capture)
    {
        const UiInput& pin = realIn;
        const float qw = 760.f * s, qh = 300.f * s;
        const float qx = floorf(px + (pw - qw) * 0.5f), qy = floorf(py + (ph - qh) * 0.5f + 30.f * s);
        bool done = false;
        FillRect(dev, px, py, pw, ph, Premul(170, 0, 0, 0));
        FillRect(dev, qx - 2.f, qy - 2.f, qw + 4.f, qh + 4.f, D3DCOLOR_ARGB(255, 90, 90, 90));
        FillRect(dev, qx, qy, qw, qh, D3DCOLOR_ARGB(255, 18, 18, 18));
        const int b = g_row;
        const float ih = 44.f * s, iw = IconWidth(style, b, ih);
        DrawBtnIcon(dev, style, b, qx + qw * 0.5f, qy + 54.f * s, ih, ((phys >> b) & 1) != 0);
        MenuText(dev, Tr("PRESS A KEY ON THE KEYBOARD"), qx + qw * 0.5f, qy + 92.f * s, 38.f * s, kLabel, 1);
        const int curKey = g_keyBind[TabProfile(tab)][b];
        char now[64];
        snprintf(now, sizeof(now), Tr("NOW: %s"), curKey ? KeyName(curKey) : Tr("NO KEY"));
        Upper(up, now, sizeof(up));
        MenuText(dev, up, qx + qw * 0.5f, qy + 146.f * s, 26.f * s, curKey ? kAccent : kDim, 1);
        (void)iw;
        const float bw2 = 220.f * s, bh2 = 46.f * s, by2 = qy + qh - bh2 - 26.f * s;
        MenuText(dev, Tr("ESC: BACK"), qx + 28.f * s, by2 + 10.f * s, 22.f * s, kDim);
        if (UiButton(dev, pin, Tr("BACK"), qx + qw - 28.f * s - 2 * bw2 - 10.f * s, by2, bw2, bh2, s, btnFill, btnHover, kLabel, 26.f * s))
            g_capture = false;   // back to the tiles
        if (UiButton(dev, pin, Tr("CANCEL"), qx + qw - 28.f * s - bw2, by2, bw2, bh2, s, btnFill, btnHover, kLabel, 26.f * s)) done = true;
        if ((pin.click && !Inside(pin, qx, qy, qw, qh)) || pin.rclick) done = true;
        if (done) { g_picker = false; g_capture = false; }
    }
    // ---- The window with all actions as tiles ----
    else if (pickerOpen && g_picker)
    {
        const UiInput& pin = realIn;
        const bool ms = false;
        const int b = g_row;
        int opts[18], n = 0;
        for (int i = 0; i < kNumBtn; i++) if (!ms || (i != kBtnLT && i != kBtnRT)) opts[n++] = i;
        const int kKeyTile = -3;   // KEYBOARD KEY: the button presses a keyboard key
        opts[n++] = kOff;
        opts[n++] = kKeyTile;
        const int cols = 3, rows = (n + cols - 1) / cols;
        const float pad = 28.f * s, gap = 10.f * s, tileH = 60.f * s, headH = 66.f * s, footH = 84.f * s;
        const float qw = (std::min)(1260.f * s, pw - 60.f * s);
        const float tileW = (qw - 2 * pad - (cols - 1) * gap) / cols;
        const float qh = headH + rows * (tileH + gap) - gap + footH;
        const float qx = floorf(px + (pw - qw) * 0.5f), qy = floorf(py + (ph - qh) * 0.5f + 30.f * s);
        const int curKey = g_keyBind[TabProfile(tab)][b];
        const int cur = curKey ? kKeyTile : g_map[TabProfile(tab)][b];
        const int def = ms ? (b == 0 ? 0 : 1) : b;
        bool done = false;

        FillRect(dev, px, py, pw, ph, Premul(170, 0, 0, 0));
        FillRect(dev, qx - 2.f, qy - 2.f, qw + 4.f, qh + 4.f, D3DCOLOR_ARGB(255, 90, 90, 90));
        FillRect(dev, qx, qy, qw, qh, D3DCOLOR_ARGB(255, 18, 18, 18));

        // Head: only the page (Manual Shift: which direction)
        {
            const float mid = qy + headH * 0.5f, ts = 30.f * s;
            static const char* const kPage[3] = { "ON FOOT", "COMBAT", "IN VEHICLE" };
            MenuText(dev, ms ? (b == 0 ? "MANUAL SHIFT - SHIFT UP" : "MANUAL SHIFT - SHIFT DOWN") : Tr(kPage[tab]), qx + qw - pad, mid - ts * 0.62f, ts, kLabel, 2);
        }
        // Tiles
        for (int k = 0; k < n; k++)
        {
            const int v = opts[k];
            const float x = qx + pad + (k % cols) * (tileW + gap), y2 = qy + headH + (k / cols) * (tileH + gap);
            const bool hover = Inside(pin, x, y2, tileW, tileH), isCur = v == cur;
            // Blue only under the mouse, like everything else in the menu; the current one has white text
            FillRect(dev, x, y2, tileW, tileH, hover ? D3DCOLOR_ARGB(255, 30, 48, 78) : D3DCOLOR_ARGB(255, 32, 32, 32));
            if (hover)
            {
                const D3DCOLOR bc = kAccent;
                const float bt = 2.f * (std::max)(1.f, s);
                FillRect(dev, x, y2, tileW, bt, bc); FillRect(dev, x, y2 + tileH - bt, tileW, bt, bc);
                FillRect(dev, x, y2, bt, tileH, bc); FillRect(dev, x + tileW - bt, y2, bt, tileH, bc);
            }
            const float mid = y2 + tileH * 0.5f, ts = 24.f * s;
            float tx = x + 16.f * s;
            if (v >= 0)
            {
                const float iw = IconWidth(style, v, 34.f * s);
                DrawBtnIcon(dev, style, v, tx + iw * 0.5f, mid, 34.f * s, false);
                tx += iw + 14.f * s;
                Upper(up, ms ? kPadName[style][v] : ActionText(tab, v), sizeof(up));
                MenuText(dev, up, tx, mid - ts * 0.62f, ts, isCur || hover ? D3DCOLOR_ARGB(255, 255, 255, 255) : kDim);
            }
            else if (v == kKeyTile)
            {
                char kb[48];
                if (curKey) snprintf(kb, sizeof(kb), Tr("Keyboard key: %s"), KeyName(curKey)); else snprintf(kb, sizeof(kb), "%s", Tr("Keyboard key ..."));
                Upper(up, kb, sizeof(up));
                MenuText(dev, up, tx, mid - ts * 0.62f, ts, kAccent);
            }
            else MenuText(dev, Tr("OFF - DOES NOTHING"), tx, mid - ts * 0.62f, ts, kOffCol);
            if (v == def) FillCircle(dev, x + tileW - 12.f * s, y2 + 12.f * s, 4.f * s, D3DCOLOR_ARGB(255, 150, 150, 150));   // the standard one
            if (hover && pin.click)
            {
                if (v == kKeyTile) g_capture = true;   // the window now waits for a key
                else { SetRow(tab, b, v); done = true; }
            }
        }
        // Foot
        {
            const float fy2 = qy + qh - footH + 18.f * s, bh2 = 46.f * s;
            FillCircle(dev, qx + pad + 6.f * s, fy2 + bh2 * 0.5f, 4.f * s, D3DCOLOR_ARGB(255, 150, 150, 150));
            MenuText(dev, Tr("= STANDARD"), qx + pad + 18.f * s, fy2 + 10.f * s, 22.f * s, kDim);
            const float bw2 = 250.f * s;
            if (UiButton(dev, pin, Tr("CANCEL"), qx + qw - pad - bw2, fy2, bw2, bh2, s, btnFill, btnHover, kLabel, 26.f * s)) done = true;
            if (UiButton(dev, pin, Tr("SET TO STANDARD"), qx + qw - pad - 2 * bw2 - 10.f * s, fy2, bw2, bh2, s, btnFill, btnHover, kLabel, 26.f * s))
            {
                SetRow(tab, b, def);
                done = true;
            }
        }
        // A click outside the window or a right click closes it without a change
        if ((pin.click && !Inside(pin, qx, qy, qw, qh)) || pin.rclick) done = true;
        if (done) { g_picker = false; g_capture = false; }
    }

    // ---- The Xbox / PlayStation list, over everything ----
    if (dropOpen)
    {
        const float itemH = 44.f * s, ly = ddY + ddH + 2.f * s;
        FillRect(dev, ddX - 2.f, ly - 2.f, ddW + 4.f, 2 * itemH + 4.f, D3DCOLOR_ARGB(255, 90, 90, 90));
        bool done = false;
        for (int k = 0; k < 2; k++)
        {
            const float iy = ly + k * itemH;
            const bool hover = Inside(realIn, ddX, iy, ddW, itemH);
            FillRect(dev, ddX, iy, ddW, itemH, k == style ? D3DCOLOR_ARGB(255, 30, 48, 78) : (hover ? D3DCOLOR_ARGB(255, 50, 50, 50) : D3DCOLOR_ARGB(255, 26, 26, 26)));
            if (k == style) FillRect(dev, ddX, iy, 4.f * s, itemH, kAccent);
            MenuText(dev, kStyle[k], ddX + 18.f * s, iy + 5.f * s, 28.f * s, k == style ? D3DCOLOR_ARGB(255, 255, 255, 255) : kLabel);
            if (hover && realIn.click)
            {
                if (g_style != k) { g_style = k; g_styleChanged = true; }
                done = true;
            }
        }
        // a click anywhere else (also on the box itself) or a right click closes it
        if ((realIn.click && !Inside(realIn, ddX, ly, ddW, 2 * itemH)) || realIn.rclick) done = true;
        if (done) s_dropOpen = false;
    }

    DrawPointer(dev, realIn.x, realIn.y, s);
}

static void DrawMenu(IDirect3DDevice9* dev)
{
    if (!g_texTried) LoadTextures(dev);
    if (!g_texWhite.t || !g_texFont.t) return;

    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC desc; bb->GetDesc(&desc);

    IDirect3DStateBlock9* sb = nullptr;
    if (IsVulkanGame()) dev->CreateStateBlock(D3DSBT_ALL, &sb);
    static const D3DRENDERSTATETYPE kRs[] = {
        D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_BLENDOP,
        D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE, D3DRS_LIGHTING, D3DRS_FOGENABLE,
        D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_COLORWRITEENABLE, D3DRS_FILLMODE };
    static const D3DTEXTURESTAGESTATETYPE kTss0[] = {
        D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2,
        D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS };
    static const D3DSAMPLERSTATETYPE kSamp[] = {
        D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV,
        D3DSAMP_SRGBTEXTURE, D3DSAMP_MAXMIPLEVEL, D3DSAMP_MIPMAPLODBIAS };
    DWORD rsOld[16], tssOld[8], sampOld[8], tss1Color = 0, tss1Alpha = 0, oldFvf = 0;
    for (int i = 0; i < 16; i++) dev->GetRenderState(kRs[i], &rsOld[i]);
    for (int i = 0; i < 8; i++) dev->GetTextureStageState(0, kTss0[i], &tssOld[i]);
    for (int i = 0; i < 8; i++) dev->GetSamplerState(0, kSamp[i], &sampOld[i]);
    dev->GetTextureStageState(1, D3DTSS_COLOROP, &tss1Color);
    dev->GetTextureStageState(1, D3DTSS_ALPHAOP, &tss1Alpha);
    dev->GetFVF(&oldFvf);
    D3DVIEWPORT9 oldVp;
    dev->GetViewport(&oldVp);
    IDirect3DVertexShader9* oldVs = nullptr;
    IDirect3DPixelShader9* oldPs = nullptr;
    IDirect3DVertexDeclaration9* oldDecl = nullptr;
    IDirect3DBaseTexture9* oldTex = nullptr;
    IDirect3DVertexBuffer9* oldStream = nullptr;
    UINT oldStreamOffset = 0, oldStreamStride = 0;
    dev->GetVertexShader(&oldVs);
    dev->GetPixelShader(&oldPs);
    dev->GetVertexDeclaration(&oldDecl);
    dev->GetTexture(0, &oldTex);
    dev->GetStreamSource(0, &oldStream, &oldStreamOffset, &oldStreamStride);
    IDirect3DSurface9* oldRt = nullptr;
    dev->GetRenderTarget(0, &oldRt);

    D3DVIEWPORT9 vp = { 0, 0, desc.Width, desc.Height, 0.f, 1.f };
    dev->SetRenderTarget(0, bb);
    dev->SetViewport(&vp);
    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(nullptr);
    dev->SetFVF(kFVF);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);
    dev->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, 0);

    DrawMenuContent(dev, (float)desc.Width, (float)desc.Height);

    if (oldRt) { dev->SetRenderTarget(0, oldRt); oldRt->Release(); }
    if (sb) { sb->Apply(); sb->Release(); }
    else
    {
        dev->SetViewport(&oldVp);
        for (int i = 0; i < 16; i++) dev->SetRenderState(kRs[i], rsOld[i]);
        for (int i = 0; i < 8; i++) dev->SetTextureStageState(0, kTss0[i], tssOld[i]);
        for (int i = 0; i < 8; i++) dev->SetSamplerState(0, kSamp[i], sampOld[i]);
        dev->SetTextureStageState(1, D3DTSS_COLOROP, tss1Color);
        dev->SetTextureStageState(1, D3DTSS_ALPHAOP, tss1Alpha);
        dev->SetVertexShader(oldVs);
        dev->SetPixelShader(oldPs);
        if (oldDecl) dev->SetVertexDeclaration(oldDecl); else dev->SetFVF(oldFvf);
        dev->SetTexture(0, oldTex);
        dev->SetStreamSource(0, oldStream, oldStreamOffset, oldStreamStride);
    }
    if (oldVs) oldVs->Release();
    if (oldPs) oldPs->Release();
    if (oldDecl) oldDecl->Release();
    if (oldTex) oldTex->Release();
    if (oldStream) oldStream->Release();
    bb->Release();
}

// ---------------------------------------------------------------------
//  Hooks (the same drawing ways as Manual Shift's gauge)
// ---------------------------------------------------------------------
static bool MenuFresh() { return g_menuOpen && (GetTickCount() - g_menuTick) < 300; }

typedef HRESULT (__stdcall* EndSceneFn)(IDirect3DDevice9*);
typedef HRESULT (__stdcall* PresentFn)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT (__stdcall* ScPresentFn)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
typedef HRESULT (__stdcall* PresentExFn)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
static EndSceneFn g_origEndScene = nullptr;
static PresentFn g_origPresent = nullptr;
static ScPresentFn g_origScPresent = nullptr;
static PresentExFn g_origPresentEx = nullptr;
static bool g_exHooked = false, g_d3dHooked = false, g_scHooked = false;
static volatile DWORD g_lastPresentTick = 0;
static volatile bool g_inOwnScene = false;
static volatile bool g_inDevPresent = false;
static volatile bool g_drawnThisFrame = false;

static void DrawOwnScene(IDirect3DDevice9* dev)
{
    if (g_drawnThisFrame || !MenuFresh()) return;
    if (SUCCEEDED(dev->BeginScene()))
    {
        g_inOwnScene = true;
        DrawMenu(dev);
        dev->EndScene();
        g_inOwnScene = false;
        g_drawnThisFrame = true;
    }
}

static HRESULT __stdcall PresentHook(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND w, const RGNDATA* r)
{
    g_lastPresentTick = GetTickCount();
    DrawOwnScene(dev);
    g_inDevPresent = true;
    HRESULT hr = g_origPresent(dev, a, b, w, r);
    g_inDevPresent = false;
    g_drawnThisFrame = false;
    return hr;
}

static HRESULT __stdcall PresentExHook(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND w, const RGNDATA* r, DWORD flags)
{
    g_lastPresentTick = GetTickCount();
    DrawOwnScene(dev);
    g_inDevPresent = true;
    HRESULT hr = g_origPresentEx(dev, a, b, w, r, flags);
    g_inDevPresent = false;
    g_drawnThisFrame = false;
    return hr;
}

static HRESULT __stdcall ScPresentHook(IDirect3DSwapChain9* sc, const RECT* a, const RECT* b, HWND w, const RGNDATA* r, DWORD flags)
{
    g_lastPresentTick = GetTickCount();
    if (!g_inDevPresent)
    {
        IDirect3DDevice9* dev = nullptr;
        if (SUCCEEDED(sc->GetDevice(&dev)) && dev) { DrawOwnScene(dev); dev->Release(); }
    }
    HRESULT hr = g_origScPresent(sc, a, b, w, r, flags);
    if (!g_inDevPresent) g_drawnThisFrame = false;
    return hr;
}

static HRESULT __stdcall EndSceneHook(IDirect3DDevice9* dev)
{
    if (!g_inOwnScene)
    {
        IDirect3DSurface9* rt = nullptr; IDirect3DSurface9* bb = nullptr;
        bool toBackBuffer = false;
        if (SUCCEEDED(dev->GetRenderTarget(0, &rt)) && rt &&
            SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
            toBackBuffer = (rt == bb);
        if (rt) rt->Release();
        if (bb) bb->Release();
        if (toBackBuffer)
        {
            bool presentAlive = g_lastPresentTick != 0 && GetTickCount() - g_lastPresentTick < 200;
            bool allowed = IsVulkanGame() ? (!presentAlive || !g_drawnThisFrame) : !presentAlive;
            if (MenuFresh() && allowed)
            {
                g_drawnThisFrame = true;
                DrawMenu(dev);
            }
        }
    }
    return g_origEndScene(dev);
}

static void PatchVtable(void** vt, int idx, void* fn, void** orig)
{
    DWORD old;
    if (!VirtualProtect(&vt[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
    *orig = vt[idx];
    vt[idx] = fn;
    VirtualProtect(&vt[idx], sizeof(void*), old, &old);
}

static void TryHookD3D()
{
    if (!g_ppDevice || !*g_ppDevice) return;
    IDirect3DDevice9* dev = *g_ppDevice;
    if (!g_d3dHooked)
    {
        void** vt = *(void***)dev;
        PatchVtable(vt, 42, (void*)&EndSceneHook, (void**)&g_origEndScene);
        PatchVtable(vt, 17, (void*)&PresentHook, (void**)&g_origPresent);
        g_d3dHooked = g_origEndScene && g_origPresent;
        Log("Graphics hook %s", g_d3dHooked ? "active" : "failed");
    }
    if (!g_exHooked)
    {
        g_exHooked = true;
        static const GUID iidEx = { 0xb18b10ce, 0x2649, 0x405a, { 0x87, 0x0f, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a } };
        void* ex = nullptr;
        if (SUCCEEDED(dev->QueryInterface(iidEx, &ex)) && ex)
        {
            void** vt = *(void***)ex;
            PatchVtable(vt, 121, (void*)&PresentExHook, (void**)&g_origPresentEx);
            ((IUnknown*)ex)->Release();
            Log("PresentEx hook %s (D3D9Ex)", g_origPresentEx ? "active" : "failed");
        }
    }
    if (!g_scHooked)
    {
        IDirect3DSwapChain9* sc = nullptr;
        if (SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc)
        {
            void** vt = *(void***)sc;
            PatchVtable(vt, 3, (void*)&ScPresentHook, (void**)&g_origScPresent);
            g_scHooked = g_origScPresent != nullptr;
            sc->Release();
            Log("Swapchain hook %s", g_scHooked ? "active" : "failed");
        }
    }
}

typedef void (__cdecl* ProcessFn)();
static ProcessFn g_origProcess = nullptr;

static void __cdecl GameProcessHook()
{
    TryHookD3D();
    OnGameFrame();
    // Once: does the game read the controller through us?
    static DWORD firstCheck = 0;
    static bool reported = false;
    if (!reported && g_next)
    {
        if (!firstCheck) firstCheck = GetTickCount();
        else if (GetTickCount() - firstCheck > 5000)
        {
            reported = true;
            Log("Controller: the game read it %ld times in the first 5 s", InterlockedExchange(&g_gameReads, 0));
        }
    }
    g_origProcess();
}

// ---------------------------------------------------------------------
//  Start
// ---------------------------------------------------------------------
static bool Init()
{
    uint8_t* p;
    auto nat = FindAll("56 8B 35 ? ? ? ? 85 F6 75 06");
    if (nat.size() != 2) { Log("Pattern 'natives' not found (%u matches)", (unsigned)nat.size()); return false; }
    g_getNativeAddress = (decltype(g_getNativeAddress))nat[1];

    p = FindFirst({ "0F B6 0D ? ? ? ? 0F B6 C0 0B C1" });
    if (!p) { Log("Pattern 'pause' not found"); return false; }
    g_userPause = *(bool**)(p + 3);
    g_scriptPause = *(bool**)(p + 15);

    p = FindFirst({ "80 3D ? ? ? ? ? 74 4B E8 ? ? ? ? 84 C0" });
    if (p) g_menuActive = *(bool**)(p + 2);

    p = FindFirst({ "83 3D ? ? ? ? ? 74 17 8B 4D 14", "83 3D ? ? ? ? ? 74 15 8B 44 24 1C", "83 3D ? ? ? ? ? 74 EF" });
    if (p) g_ppDevice = *(IDirect3DDevice9***)(p + 2);
    else Log("Pattern 'D3D device' not found - no menu");

    FindPadSlot();
    FindManualShift();

    p = FindFirst({ "E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? B9 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? B9" });
    if (!p) { Log("Pattern 'game frame' not found"); return false; }
    g_origProcess = (ProcessFn)CallDest(p);
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int32_t*)(p + 1) = (int32_t)((uint8_t*)&GameProcessHook - (p + 5));
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);

    Log("Initialised");
    return true;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        char path[MAX_PATH];
        GetModuleFileNameA(inst, path, MAX_PATH);
        g_dir = path;
        g_dir = g_dir.substr(0, g_dir.find_last_of("\\/") + 1);
        g_log = fopen((g_dir + "ControllerRebind.log").c_str(), "w");
        Log("Controller Rebind %s starting", kVersion);
        LoadConfig();
        g_style = cfg.style;
        Init();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (g_log) fclose(g_log);
    }
    return TRUE;
}
