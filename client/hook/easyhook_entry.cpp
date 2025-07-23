// 将文件保存为 UTF-8 编码以修复编码问题
#include "pch.h" // 预编译头必须放在最前面
#include <Windows.h>
#include <easyhook.h>
#include <iostream> // 添加iostream头文件
#include "../hook/hook_cuda.h"
#include "../launcher/launcher_client.h"

// 全局LauncherClient对象
std::unique_ptr<LauncherClient> g_launcher_client;

// DLL入口点
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            // 初始化LauncherClient并连接到本地服务
    g_launcher_client = std::make_unique<LauncherClient>("127.0.0.1:12345");
    if (g_launcher_client) {
        if (g_launcher_client->connect()) {
            std::cout << "Successfully connected to launcher" << std::endl;
        } else {
            std::cerr << "Failed to connect to launcher" << std::endl;
        }
    } else {
        std::cerr << "Failed to create launcher client" << std::endl;
    }
            break;
        case DLL_PROCESS_DETACH:
            // 清理资源
            g_launcher_client.reset();
            break;
    }
    return TRUE;
}

// 安装Hook
extern "C" __declspec(dllexport) void InstallHook() {
    HMODULE hCuda = GetModuleHandleW(L"nvcuda.dll");
    if (!hCuda) {
        MessageBoxA(NULL, "Failed to find nvcuda.dll", "Error", MB_ICONERROR);
        return;
    }

    // 函数指针列表和对应的hook函数
    struct HookInfo {
        void** original;
        void* hook_function;
        const char* function_name;
    } hooks[] = {
        { (void**)&pOriginal_cuMemAlloc, Hooked_cuMemAlloc, "cuMemAlloc_v2" },
        { (void**)&pOriginal_cuMemFree, Hooked_cuMemFree, "cuMemFree_v2" },
        { (void**)&pOriginal_cuMemcpyHtoD, Hooked_cuMemcpyHtoD, "cuMemcpyHtoD_v2" },
        { (void**)&pOriginal_cuMemcpyDtoH, Hooked_cuMemcpyDtoH, "cuMemcpyDtoH_v2" },
        { (void**)&pOriginal_cuLaunchKernel, Hooked_cuLaunchKernel, "cuLaunchKernel" }
    };

    // 统一安装所有hook
    for (auto& hook : hooks) {
        *hook.original = GetProcAddress(hCuda, hook.function_name);
        if (!*hook.original) {
            char error_msg[256];
            sprintf_s(error_msg, "Failed to find %s", hook.function_name);
            MessageBoxA(NULL, error_msg, "Error", MB_ICONERROR);
            continue;
        }

        HOOK_TRACE_INFO hHook = { NULL };
        NTSTATUS result = LhInstallHook(
            *hook.original,
            hook.hook_function,
            NULL,
            &hHook
        );

        if (FAILED(result)) {
            char error_msg[256];
            sprintf_s(error_msg, "Failed to install hook for %s: 0x%X", 
                     hook.function_name, result);
            MessageBoxA(NULL, error_msg, "Error", MB_ICONERROR);
        }
    }
}

// 卸载Hook
extern "C" __declspec(dllexport) void UninstallHook() {
    LhUninstallAllHooks();
    LhWaitForPendingRemovals();
}
