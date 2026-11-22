# LayAndRelax ASI 编译说明

## 工程文件
- `LayAndRelax.cpp`  — C++ 原生源码（无 SHVDN 依赖）
- `LayAndRelax.ini`  — 运行时配置（按键 / 手柄）

## 功能
- 按键（默认 J）或手柄 LB+RB 切换躺下 / 起身
- 跳跃、按住冲刺、进入载具自动起身
- 腿部 IK 每帧贴地修正，消除悬浮
- 载具内禁止躺下

## 编译步骤（PC / VS2022）
1. 新建「Windows 动态链接库 (DLL)」空项目，平台 **x64**，配置 **Release**
2. 引入 ScriptHookV SDK：`scripthookv.h`、`scripthookv.lib`（链接器 → 输入 → 附加依赖项）
3. 把 `LayAndRelax.cpp` 加入项目
4. 预处理器定义：`_CRT_SECURE_NO_WARNINGS;WIN32_LEAN_AND_MEAN;NOMINMAX`
5. 编译输出 `LayAndRelax.dll`，重命名为 `LayAndRelax.asi`
6. 把 `LayAndRelax.asi` + `LayAndRelax.ini` 放到 GTA5 根目录（与 `ScriptHookV.dll`、`dinput8.dll` 同级）

## 动画
- 字典：`missfbi4prepp1`
- 片段：`lay_ground`（仰躺循环）

## GitHub Actions / GitLab CI 在线编译
仓库需放入：
- `LayAndRelax.cpp`
- `scripthookv.h`、`scripthookv.lib`（SHV SDK）
- workflow 调用 MSVC（cl.exe）编译 x64 DLL，产物重命名为 `.asi`

## 注意
- 仅单机故事模式；线上禁用
- 躺下动画自带小幅背部悬浮，脚部已由 IK 拉到离地 0.02 单位贴地

> （注：部分内容由豆包 AI 生成）