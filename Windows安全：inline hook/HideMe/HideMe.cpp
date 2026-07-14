#include <windows.h>
#include <iostream>

int main() {
    // 设置控制台标题，方便在任务栏找到
    SetConsoleTitleA("HideMe.exe");

    std::cout << "===========================================" << std::endl;
    std::cout << " [Target] 我是 HideMe.exe" << std::endl;
    std::cout << " [PID]    " << GetCurrentProcessId() << std::endl;
    std::cout << "===========================================" << std::endl;
    std::cout << "1. 请保持我运行。" << std::endl;
    std::cout << "2. 打开任务管理器，查找HideMe.exe进程。" << std::endl;
    std::cout << "3. 加载dll进行Hook，我就会从列表里消失！" << std::endl;

    while (true) {
        Sleep(1000);
    }
    return 0;
}