// LayAndRelax.cpp
// GTA5 ScriptHookV ASI 躺卧模组 (C++ 原生, 无 SHVDN 依赖)
// 功能: 按键切换躺下/起身; 跳跃/冲刺/上车自动起身; 脚部IK贴地; ini 可配按键
// 编译目标: x64 Release, 链接 scripthookv.lib, 输出 DLL 后改名 .asi

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstring>

// ---------- ScriptHookV 导出函数指针 (与你之前 SitDown 源码同款解析方式) ----------
typedef void (__cdecl *ScriptMainFn)();
typedef bool (__cdecl *ScriptRegisterFn)(ScriptMainFn, const char*);
typedef void (__cdecl *ScriptUnregisterFn)();
typedef void (__cdecl *ScriptWaitFn)(int);

typedef void (__cdecl *NativeInitFn)(uint64_t);
typedef void (__cdecl *NativePush64Fn)(uint64_t);
typedef uint64_t* (__cdecl *NativeCallFn)();

typedef void (__stdcall *KeyboardHandlerFn)(DWORD, WORD, BOOL, BOOL, BOOL);
typedef bool (__cdecl *KeyboardRegisterFn)(KeyboardHandlerFn);
typedef void (__cdecl *KeyboardUnregisterFn)();

static HMODULE g_module = nullptr;
static ScriptRegisterFn   g_scriptRegister   = nullptr;
static ScriptUnregisterFn g_scriptUnregister = nullptr;
static ScriptWaitFn       g_scriptWait       = nullptr;
static NativeInitFn       g_nativeInit       = nullptr;
static NativePush64Fn     g_nativePush64     = nullptr;
static NativeCallFn       g_nativeCall       = nullptr;
static KeyboardRegisterFn g_keyboardRegister = nullptr;
static KeyboardUnregisterFn g_keyboardUnregister = nullptr;

// ---------- Native 哈希常量 ----------
static constexpr uint64_t H_PLAYER_PED_ID              = 0x6A34F865A3F02D4AULL;
static constexpr uint64_t H_DOES_ENTITY_EXIST          = 0x7BB556A268390B5AULL;
static constexpr uint64_t H_IS_PED_IN_VEHICLE         = 0x84B0F3A5A3339156ULL;
static constexpr uint64_t H_REQUEST_ANIM_DICT          = 0x23F82128FDDE703AULL;
static constexpr uint64_t H_HAS_ANIM_DICT_LOADED       = 0x78AB0FE603B1809BULL;
static constexpr uint64_t H_REMOVE_ANIM_DICT          = 0x904A20F679C0B28AULL;
static constexpr uint64_t H_TASK_PLAY_ANIM             = 0xEA42CE82FB28E60AULL;
static constexpr uint64_t H_CLEAR_PED_TASKS           = 0xEEC9A4F160C2B283ULL;
static constexpr uint64_t H_IS_CONTROL_JUST_PRESSED    = 0x5317B2B32D83152DULL;
static constexpr uint64_t H_IS_CONTROL_PRESSED        = 0x64D3B7162D7B4698ULL;

// IK 相关 (贴地修正)
static constexpr uint64_t H_SET_PED_CAN_LEG_IK        = 0x73518ECE2485412BULL;
static constexpr uint64_t H_GET_PED_BONE_COORDS      = 0x17C07FC640E86B4EULL;
static constexpr uint64_t H_GET_GROUND_Z              = 0xC906A7DAB05C8D2BULL;
static constexpr uint64_t H_SET_IK_TARGET              = 0xC32779C16FCEECD9ULL;

// 骨骼 / IK 部件
static constexpr int BONE_LEFT_FOOT   = 0x3779;
static constexpr int BONE_RIGHT_FOOT  = 0xCC4D;
static constexpr int IK_LEFT_LEG      = 5;
static constexpr int IK_RIGHT_LEG     = 6;

// ---------- 状态变量 ----------
static bool  g_laying = false;
static volatile LONG g_keyboardLayPressed = 0;
static int   g_layVirtualKey = 74;   // 默认 J 键
static int   g_lbControl = 37;       // 手柄 LB
static int   g_rbControl = 44;      // 手柄 RB
static bool  g_comboWasDown = false;
static wchar_t g_iniPath[MAX_PATH] = { 0 };

// ---------- Native 封装 ----------
static inline void Push(uint64_t v) { g_nativePush64(v); }

static uint64_t Call0(uint64_t hash) {
    g_nativeInit(hash);
    uint64_t* r = g_nativeCall();
    return r ? *r : 0;
}
static uint64_t Call1(uint64_t hash, uint64_t a) {
    g_nativeInit(hash); Push(a);
    uint64_t* r = g_nativeCall();
    return r ? *r : 0;
}
static uint64_t Call2(uint64_t hash, uint64_t a, uint64_t b) {
    g_nativeInit(hash); Push(a); Push(b);
    uint64_t* r = g_nativeCall();
    return r ? *r : 0;
}
static uint64_t Call3(uint64_t hash, uint64_t a, uint64_t b, uint64_t c) {
    g_nativeInit(hash); Push(a); Push(b); Push(c);
    uint64_t* r = g_nativeCall();
    return r ? *r : 0;
}
static uint64_t Call10(uint64_t hash, const uint64_t* args) {
    g_nativeInit(hash);
    for (int i = 0; i < 10; ++i) Push(args[i]);
    uint64_t* r = g_nativeCall();
    return r ? *r : 0;
}

static inline uint64_t FloatBits(float f) {
    uint32_t u; std::memcpy(&u, &f, sizeof(float));
    return static_cast<uint64_t>(u);
}
static inline bool NativeBool(uint64_t v) { return v != 0; }

// ---------- 键盘回调 ----------
static void OnKeyboard(DWORD vk, WORD /*scan*/, BOOL ctrl, BOOL shift, BOOL /*alt*/) {
    if (static_cast<int>(vk) == g_layVirtualKey) {
        InterlockedExchange(&g_keyboardLayPressed, 1);
    }
}

// ---------- 解析 ScriptHookV ----------
static bool ResolveScriptHook() {
    HMODULE shv = GetModuleHandleW(L"ScriptHookV.dll");
    if (!shv) return false;

    g_scriptRegister   = reinterpret_cast<ScriptRegisterFn>(  GetProcAddress(shv, "?RegisterScript@@YAHPEAXPEBD@Z"));
    g_scriptUnregister = reinterpret_cast<ScriptUnregisterFn>(GetProcAddress(shv, "?UnregisterScript@@YAXXZ"));
    g_scriptWait       = reinterpret_cast<ScriptWaitFn>(      GetProcAddress(shv, "?ScriptWait@@YAXH@Z"));
    g_nativeInit       = reinterpret_cast<NativeInitFn>(       GetProcAddress(shv, "?nativeInit@@YAX_K@Z"));
    g_nativePush64     = reinterpret_cast<NativePush64Fn>(     GetProcAddress(shv, "?nativePush64@@YAX_K@Z"));
    g_nativeCall       = reinterpret_cast<NativeCallFn>(       GetProcAddress(shv, "?nativeCall@@YAPEAX_K@Z"));
    g_keyboardRegister = reinterpret_cast<KeyboardRegisterFn>( GetProcAddress(shv, "?RegisterKeyboardHandler@@YA_NP6AXK_W000@Z"));
    g_keyboardUnregister = reinterpret_cast<KeyboardUnregisterFn>(GetProcAddress(shv, "?UnregisterKeyboardHandler@@YAXXZ"));

    return g_scriptRegister && g_scriptUnregister && g_scriptWait &&
           g_nativeInit && g_nativePush64 && g_nativeCall;
}

// ---------- IK 工具 ----------
static bool GetFootWorldPosition(int ped, int bone, float& x, float& y, float& z) {
    const uint64_t args[5] = {
        static_cast<uint32_t>(ped), static_cast<uint32_t>(bone),
        FloatBits(0.f), FloatBits(0.f), FloatBits(0.f)
    };
    g_nativeInit(H_GET_PED_BONE_COORDS);
    for (uint64_t v : args) Push(v);
    uint64_t* r = g_nativeCall();
    if (!r) return false;
    std::memcpy(&x, &r[0], sizeof(float));
    std::memcpy(&y, &r[1], sizeof(float));
    std::memcpy(&z, &r[2], sizeof(float));
    return true;
}

static bool GetGroundZ(float x, float y, float probeZ, float& groundZ) {
    const uint64_t args[5] = {
        FloatBits(x), FloatBits(y), FloatBits(probeZ),
        reinterpret_cast<uint64_t>(&groundZ), 0
    };
    g_nativeInit(H_GET_GROUND_Z);
    for (uint64_t v : args) Push(v);
    uint64_t* r = g_nativeCall();
    return r && (*r != 0);
}

static void ApplyFootIK(int ped) {
    float x = 0.f, y = 0.f, z = 0.f, groundZ = 0.f;
    const int bones[2] = { BONE_LEFT_FOOT, BONE_RIGHT_FOOT };
    const int parts[2]  = { IK_LEFT_LEG, IK_RIGHT_LEG };
    for (int i = 0; i < 2; ++i) {
        if (!GetFootWorldPosition(ped, bones[i], x, y, z)) continue;
        if (!GetGroundZ(x, y, z + 1.0f, groundZ)) continue;
        const uint64_t args[10] = {
            static_cast<uint32_t>(ped), static_cast<uint32_t>(parts[i]), 0,
            static_cast<uint32_t>(-1),
            FloatBits(x), FloatBits(y), FloatBits(groundZ + 0.02f),
            0, static_cast<uint32_t>(-1), static_cast<uint32_t>(-1)
        };
        Call10(H_SET_IK_TARGET, args);
    }
}

// ---------- 动作函数 ----------
static void LayDown(int ped) {
    // 开启腿部 IK
    Call2(H_SET_PED_CAN_LEG_IK, static_cast<uint32_t>(ped), 1);

    static const char* dict = "missfbi4prepp1";
    static const char* anim  = "lay_ground";

    Call1(H_REQUEST_ANIM_DICT, reinterpret_cast<uint64_t>(dict));
    for (int i = 0; i < 100; ++i) {
        if (NativeBool(Call1(H_HAS_ANIM_DICT_LOADED, reinterpret_cast<uint64_t>(dict)))) break;
        g_scriptWait(10);
    }
    if (!NativeBool(Call1(H_HAS_ANIM_DICT_LOADED, reinterpret_cast<uint64_t>(dict)))) return;

    g_nativeInit(H_TASK_PLAY_ANIM);
    Push(static_cast<uint32_t>(ped));
    Push(reinterpret_cast<uint64_t>(dict));
    Push(reinterpret_cast<uint64_t>(anim));
    Push(FloatBits(8.0f));
    Push(FloatBits(-8.0f));
    Push(static_cast<uint32_t>(-1));
    Push(1); // loop
    Push(FloatBits(1.0f));
    Push(0); Push(0); Push(0);
    g_nativeCall();

    g_laying = true;
}

static void StandUp(int ped) {
    Call2(H_SET_PED_CAN_LEG_IK, static_cast<uint32_t>(ped), 0);
    Call1(H_CLEAR_PED_TASKS, static_cast<uint32_t>(ped));
    g_laying = false;
}

// ---------- ini 读取 ----------
static void MakeIniPath() {
    wchar_t dllPath[MAX_PATH] = { 0 };
    GetModuleFileNameW(g_module, dllPath, MAX_PATH);
    wchar_t* slash = wcsrchr(dllPath, L'\\');
    if (slash) {
        wcscpy_s(slash + 1, MAX_PATH - (slash - dllPath), L"LayAndRelax.ini");
        std::memcpy(g_iniPath, dllPath, sizeof(dllPath));
    }
}

static void ReadConfig() {
    g_layVirtualKey = GetPrivateProfileIntA("Keys", "LayVirtualKey", 74, reinterpret_cast<LPCSTR>(g_iniPath));
    g_lbControl     = GetPrivateProfileIntA("Controller", "LeftShoulderControl", 37, reinterpret_cast<LPCSTR>(g_iniPath));
    g_rbControl     = GetPrivateProfileIntA("Controller", "RightShoulderControl", 44, reinterpret_cast<LPCSTR>(g_iniPath));
    if (g_layVirtualKey <= 0) g_layVirtualKey = 74;
    if (g_lbControl <= 0) g_lbControl = 37;
    if (g_rbControl <= 0) g_rbControl = 44;
}

// ---------- 脚本主线程 ----------
static void ScriptMain() {
    ReadConfig();

    while (true) {
        g_scriptWait(0);

        int ped = static_cast<int>(Call0(H_PLAYER_PED_ID));
        if (!NativeBool(Call1(H_DOES_ENTITY_EXIST, static_cast<uint32_t>(ped)))) continue;

        bool inVehicle = NativeBool(Call1(H_IS_PED_IN_VEHICLE, static_cast<uint32_t>(ped)));

        // 键盘触发
        LONG kbPressed = InterlockedExchange(&g_keyboardLayPressed, 0);
        bool keyboardToggle = (kbPressed == 1);

        // 手柄 LB+RB 组合键刚按下
        bool comboDown =
            NativeBool(Call2(H_IS_CONTROL_PRESSED, 0, static_cast<uint32_t>(g_lbControl))) &&
            NativeBool(Call2(H_IS_CONTROL_PRESSED, 0, static_cast<uint32_t>(g_rbControl)));
        bool comboToggle = comboDown && !g_comboWasDown;
        g_comboWasDown = comboDown;

        bool toggle = keyboardToggle || comboToggle;

        // 跳跃(22) / 冲刺(21) 自动起身
        bool jumpPressed = NativeBool(Call2(H_IS_CONTROL_JUST_PRESSED, 0, 22));
        bool sprintHeld  = NativeBool(Call2(H_IS_CONTROL_PRESSED, 0, 21));

        if (g_laying) {
            if (toggle || jumpPressed || sprintHeld || inVehicle) {
                StandUp(ped);
            } else {
                ApplyFootIK(ped);
            }
        } else {
            if (toggle && !inVehicle) {
                LayDown(ped);
            }
        }
    }
}

// ---------- DLL 入口 ----------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_module = hModule;
        DisableThreadLibraryCalls(hModule);
        MakeIniPath();
        ReadConfig();
        if (ResolveScriptHook()) {
            if (g_keyboardRegister) g_keyboardRegister(OnKeyboard);
            g_scriptRegister(ScriptMain, "LayAndRelax");
        }
        break;
    case DLL_PROCESS_DETACH:
        if (g_keyboardUnregister) g_keyboardUnregister();
        if (g_scriptUnregister) g_scriptUnregister();
        break;
    }
    return TRUE;
}
