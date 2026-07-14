/*
 * TaskMgrInjector.cpp
 *
 * Windows 7/10 任务管理器钩子注入器
 * 使用 Microsoft Detours 库
 *
 * 本程序将 HideProcessHook.dll 注入到 taskmgr.exe 中
 * 以在任务管理器中隐藏目标进程 (HideMe.exe)
 *
 * 兼容性说明:
 *   - Windows 7: 完全支持（无保护机制）
 *   - Windows 10 (早期版本): 可能工作
 *   - Windows 10/11 (近期版本): 由于PPL保护机制会失败
 */

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string>

 // ============================================================
 // 辅助函数
 // ============================================================

 // 打印错误信息
void PrintError(const char* operation) {
    DWORD err = GetLastError();
    char buffer[512];
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buffer, sizeof(buffer), NULL
    );
    printf("[-] %s 失败。错误代码 %lu: %s\n", operation, err, buffer);
}

// 启用调试权限以访问系统进程
BOOL EnableDebugPrivilege() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    LUID luid;

    // 打开当前进程的访问令牌
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        PrintError("OpenProcessToken");
        return FALSE;
    }

    // 查找调试权限的LUID值
    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
        PrintError("LookupPrivilegeValue");
        CloseHandle(hToken);
        return FALSE;
    }

    // 设置权限结构
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // 调整令牌权限
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL)) {
        PrintError("AdjustTokenPrivileges");
        CloseHandle(hToken);
        return FALSE;
    }

    DWORD lastError = GetLastError();
    CloseHandle(hToken);

    // 检查权限是否完全分配
    if (lastError == ERROR_NOT_ALL_ASSIGNED) {
        printf("[-] 调试权限未完全分配。请以管理员身份运行！\n");
        return FALSE;
    }

    printf("[+] 调试权限启用成功。\n");
    return TRUE;
}

// 通过进程名称查找进程ID
DWORD FindProcessByName(const wchar_t* processName) {
    // 创建进程快照
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(pe32);

    // 遍历所有进程
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            // 比较进程名称（不区分大小写）
            if (_wcsicmp(pe32.szExeFile, processName) == 0) {
                CloseHandle(hSnapshot);
                return pe32.th32ProcessID;
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);
    return 0;
}

// 向目标进程注入DLL
BOOL InjectDLL(DWORD pid, const char* dllPath) {
    printf("[*] 打开进程 PID: %lu...\n", pid);

    // 打开目标进程，获取必要的访问权限
    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid
    );

    if (!hProcess) {
        PrintError("OpenProcess");
        printf("    提示：请确保以管理员身份运行！\n");
        return FALSE;
    }
    printf("[+] 进程打开成功。\n");

    // 在目标进程中分配内存
    size_t pathLen = strlen(dllPath) + 1;
    LPVOID pRemotePath = VirtualAllocEx(hProcess, NULL, pathLen, MEM_COMMIT, PAGE_READWRITE);
    if (!pRemotePath) {
        PrintError("VirtualAllocEx");
        CloseHandle(hProcess);
        return FALSE;
    }
    printf("[+] 远程内存分配在: 0x%p\n", pRemotePath);

    // 将DLL路径写入目标进程
    if (!WriteProcessMemory(hProcess, pRemotePath, dllPath, pathLen, NULL)) {
        PrintError("WriteProcessMemory");
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }
    printf("[+] DLL路径已写入目标进程。\n");

    // 获取LoadLibraryA函数地址（在kernel32.dll中）
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    FARPROC pLoadLibraryA = GetProcAddress(hKernel32, "LoadLibraryA");
    if (!pLoadLibraryA) {
        PrintError("GetProcAddress(LoadLibraryA)");
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }

    // 创建远程线程来加载DLL
    printf("[*] 创建远程线程...\n");
    HANDLE hThread = CreateRemoteThread(
        hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)pLoadLibraryA,
        pRemotePath, 0, NULL
    );

    if (!hThread) {
        PrintError("CreateRemoteThread");
        printf("    在Windows 10/11上可能因进程保护机制而失败。\n");
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }
    printf("[+] 远程线程创建成功。\n");

    // 等待线程完成
    printf("[*] 等待DLL加载...\n");
    WaitForSingleObject(hThread, 5000);

    // 检查线程退出码
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    // 清理资源
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    // 检查DLL是否加载成功
    if (exitCode == 0) {
        printf("[-] LoadLibrary返回NULL。DLL加载失败。\n");
        printf("    可能的原因：\n");
        printf("    1. DLL文件未找到\n");
        printf("    2. DLL架构不匹配 (x86 vs x64)\n");
        printf("    3. DLL缺少依赖项\n");
        return FALSE;
    }

    printf("[+] DLL加载成功！句柄: 0x%lX\n", exitCode);
    return TRUE;
}

// 启动任务管理器
BOOL LaunchTaskManager() {
    printf("[*] 启动任务管理器...\n");

    // 构建任务管理器完整路径（Windows\System32\taskmgr.exe）
    wchar_t systemDir[MAX_PATH];
    GetSystemDirectoryW(systemDir, MAX_PATH);

    wchar_t taskmgrPath[MAX_PATH];
    swprintf_s(taskmgrPath, L"%s\\taskmgr.exe", systemDir);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    // 创建任务管理器进程
    if (CreateProcessW(taskmgrPath, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("[+] 任务管理器已启动。PID: %lu\n", pi.dwProcessId);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Sleep(1000); // 等待进程初始化
        return TRUE;
    }

    PrintError("CreateProcess(taskmgr.exe)");
    return FALSE;
}

// ============================================================
// 主函数
// ============================================================
int main() {
    printf("==============================================\n");
    printf("  任务管理器钩子注入器\n");
    printf("  使用 Microsoft Detours 库\n");
    printf("==============================================\n");
    printf("  目标：在任务管理器中隐藏 HideMe.exe\n");
    printf("  兼容性：Windows 7 / Windows 10 (早期版本)\n");
    printf("==============================================\n\n");

    // 步骤1: 启用调试权限
    printf("[步骤1] 启用调试权限...\n");
    if (!EnableDebugPrivilege()) {
        printf("\n[!] 请以管理员身份运行此程序！\n");
        printf("    右键 -> 以管理员身份运行\n");
        system("pause");
        return 1;
    }

    // 步骤2: 检查DLL文件是否存在
    printf("\n[步骤2] 检查DLL文件...\n");
    char dllPath[MAX_PATH];
    GetFullPathNameA("HideProcessHook.dll", MAX_PATH, dllPath, NULL);
    printf("[*] DLL路径: %s\n", dllPath);

    if (GetFileAttributesA(dllPath) == INVALID_FILE_ATTRIBUTES) {
        printf("[-] 错误：未找到 HideProcessHook.dll！\n");
        printf("    请确保DLL文件在同一目录下。\n");
        system("pause");
        return 1;
    }
    printf("[+] DLL文件存在。\n");

    // 步骤3: 检查目标进程 HideMe.exe 是否正在运行
    printf("\n[步骤3] 检查目标进程...\n");
    DWORD hideMePid = FindProcessByName(L"HideMe.exe");
    if (hideMePid) {
        printf("[+] HideMe.exe 正在运行。PID: %lu\n", hideMePid);
    }
    else {
        printf("[!] HideMe.exe 未运行。\n");
        printf("    为了测试，请先启动 HideMe.exe。\n");
    }

    // 步骤4: 查找或启动任务管理器
    printf("\n[步骤4] 查找任务管理器...\n");
    DWORD taskmgrPid = FindProcessByName(L"Taskmgr.exe");

    if (!taskmgrPid) {
        printf("[*] 任务管理器未运行。\n");
        printf("[*] 是否要启动它？(Y/N): ");
        char choice;
        scanf_s(" %c", &choice, 1);

        if (choice == 'Y' || choice == 'y') {
            if (!LaunchTaskManager()) {
                system("pause");
                return 1;
            }
            taskmgrPid = FindProcessByName(L"Taskmgr.exe");
        }
    }

    if (!taskmgrPid) {
        printf("[-] 未找到任务管理器。请手动启动。\n");
        system("pause");
        return 1;
    }
    printf("[+] 找到任务管理器。PID: %lu\n", taskmgrPid);

    // 步骤5: 注入DLL
    printf("\n[步骤5] 向任务管理器注入DLL...\n");
    if (InjectDLL(taskmgrPid, dllPath)) {
        printf("\n");
        printf("==============================================\n");
        printf("  成功！钩子已安装。\n");
        printf("==============================================\n");
        printf("\n");
        printf("现在检查任务管理器：\n");
        printf("  - 转到'进程'选项卡\n");
        printf("  - 查找'HideMe.exe'\n");
        printf("  - 它应该从列表中隐藏！\n");
        printf("\n");
        printf("注意：您可能需要刷新进程列表\n");
        printf("      按F5或切换选项卡。\n");
    }
    else {
        printf("\n");
        printf("==============================================\n");
        printf("  注入失败\n");
        printf("==============================================\n");
        printf("\n");
        printf("故障排除：\n");
        printf("  1. 以管理员身份运行\n");
        printf("  2. 在Windows 10/11上，任务管理器可能受保护\n");
        printf("  3. 请在Windows7 x64进行测试\n");
        printf("  4. 检查DLL架构是否匹配 (x86/x64)\n");
    }

    printf("\n");
    system("pause");
    return 0;
}