# Windows安全：inline hook

本文配套代码：在 Windows 上通过 **inline hook（Detours）** 隐藏任务管理器中的目标进程。

## 工程结构

```text
Windows安全：inline hook/
├── HideProcessHook.slnx          # Visual Studio 解决方案
├── HideMe/                       # 目标进程（被隐藏）
├── HideProcessHook/              # 注入到 taskmgr 的 hook DLL
└── TaskMgrHook/                  # 注入器（将 DLL 注入 taskmgr.exe）
```

| 项目 | 作用 |
|------|------|
| `HideMe` | 模拟需要被隐藏的目标进程 |
| `HideProcessHook` | 对任务管理器相关 API 做 inline hook |
| `TaskMgrHook` | 将 `HideProcessHook.dll` 注入到 `taskmgr.exe` |

## 使用提示

1. 使用 Visual Studio 打开 `HideProcessHook.slnx`
2. 按 Debug/Release 与 x64 配置编译各子项目
3. 先启动 `HideMe`，再以管理员权限运行注入器
4. 打开任务管理器观察目标进程是否被隐藏

## 兼容性（简要）

- **Windows 7**：通常可完整复现
- **Windows 10 早期版本**：可能可用
- **Windows 10/11 近期版本**：任务管理器受 PPL 等保护，注入可能失败

## 免责声明

仅供安全研究与学习，请勿用于未授权场景。
