# BounceBall

**弹球音序器** —— 一个把物理碰撞变成音乐的 MIDI 生成器 / 合成器插件。

小球在圆形腔体里弹跳，撞到器壁或彼此相撞时触发音符；和弦进行由内置引擎按小节推进，
所有碰撞事件既可以用插件内置音源直接发声，也可以走 MIDI 端口输出到 DAW 里驱动你自己的乐器。

基于 **JUCE 8.0.4**，支持 **VST3 / Standalone**。

---

## 下载

不想自己编译？到 [**Releases**](../../releases/latest) 页直接下载预编译版本：

| 文件 | 说明 |
|---|---|
| `BounceBall-v0.2.0-Windows-Standalone.zip` | 独立程序，解压即用，无需 DAW |
| `BounceBall-v0.2.0-Windows-VST3.zip` | VST3 插件 |

**Standalone**：解压后双击 `BounceBall.exe`。

**VST3**：解压得到 `BounceBall.vst3` 文件夹，整个放到
`C:\Program Files\Common Files\VST3\`（需管理员权限），再在 DAW 里重新扫描。

> 未做代码签名，首次运行 Windows SmartScreen 可能提示 → 「更多信息」→「仍要运行」。

---

## 特性

**物理与音乐**
- 归一化物理引擎，球在圆形腔体内自由弹跳，撞壁 / 球撞球都触发音符
- 内置和弦引擎：**10 种调式 × 18 条和弦进行**（含三和弦与氛围七和弦）
- 音区分层 + voice leading，让进行听感平滑
- 可选琶音器（按音乐时值走步，1/2 ~ 1/16）
- 可跟随 DAW 传输与 BPM，也可手动独立运行

**音色（12 种，按音区从低到高排列，设计目标是互补而非互相掩蔽）**
- 低频：Sub · Sine · Pad · Organ · Strings · Choir
- 中频：Rhodes · Pluck · Marimba · Kalimba
- 高频：Bell · Glass
- 多泛音振荡器 + 独立 ADSR，参数全部查表（无硬编码分支）
- 一键 **Ensemble**：按和弦档位自动为每颗球分派互补音色

**每球参数**
- 音高 / 八度 / 音色 / 通道 / 音量 / 概率（Probability，默认 100%）
- 是否跟随和弦（Follow）/ 琶音（Arp）/ 静音（Mute）/ 独奏（Solo）

**音频处理**
- 乒乓延时（跟随 BPM）+ Schroeder 混响，总线软限幅（knee 0.72 + tanh），不会硬削
- 静默快速通道：无 voice / 无待处理触发 / 效果尾巴散尽时整条效果链旁路，待机 CPU 接近 0
- 物理与琶音全部由音频线程按采样时钟推进 —— **关掉插件窗口后依然发声**

**界面**
- 高性能画布动画（60fps），碰撞处水面涟漪扩散 + 腔体外缘形变
- 浅色极简风格，统一 6px 圆角，无渐变
- 高 DPI 下字号做整数物理像素对齐，不发虚

---

## 编译

### 依赖
- **CMake** ≥ 3.22
- **Visual Studio 2022**（含 MSVC 与 Windows SDK）
- 首次 configure 需要联网下载 JUCE（约 100MB）

### 步骤（Windows）

```bat
:: 1. 在 "x64 Native Tools Command Prompt for VS 2022" 中打开本目录
:: 2. 一键构建（自动 configure + 编译 Release）
build.bat
```

或者手动来：

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

只编 Standalone（不装 VST3 到系统目录）：

```bat
cmake --build build --config Release --target BounceBall_Standalone --parallel
```

### 用本地 JUCE 源码（跳过下载）

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DBOUNCEBALL_JUCE_DIR=D:/path/to/JUCE
```

### 产物位置

- Standalone：`build/BounceBall_artefacts/Release/Standalone/BounceBall.exe`
- VST3：`build/BounceBall_artefacts/Release/VST3/BounceBall.vst3`

`COPY_PLUGIN_AFTER_BUILD` 已开启，编译 VST3 后会自动覆盖安装到
`C:\Program Files\Common Files\VST3\BounceBall.vst3`。

---

## 输出 MIDI 到 DAW

插件通过 MIDI 端口输出音符。**Ableton Live 不支持插件的 MIDI Out**，需要虚拟 MIDI 端口：

1. 安装 [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html)，新建一个虚拟端口
2. 在插件顶栏的 **MIDI Out** 下拉框里选中该端口
3. 在 DAW 里新建 MIDI 轨道，输入设为同一个虚拟端口
4. **取消勾选 `Preview`** —— Preview 与 MIDI 输出互斥：
   - `Preview` 勾选 = 用插件内置音源本地试听，不往外发 MIDI
   - `Preview` 取消 = 只往 MIDI 端口输出，交给 DAW 里的乐器

> 其他 DAW（Reaper / Cubase / Studio One 等）原生支持插件 MIDI Out 时，可直接使用，无需虚拟端口。

---

## 已知限制

- 球速为全局固定 0.5x 系数（不开放调节，默认手感已调校）
- 音色索引与早期开发版本不兼容：旧工程保存的音色序号可能映射到不同音色
- 仅在 Windows + MSVC 下验证过；macOS / Linux 理论上可编译但未测试

---

## 项目结构

```
Source/
  Ball.h             球数据结构（归一化坐标、音高、概率、静音/独奏状态）
  ChordEngine.h      和弦引擎：10 调式 × 18 进行，小节推进 + 音区分层
  TriggerQueue.h     UI → 音频线程的无锁 FIFO
  World.h            物理引擎 + 传输状态 + 涟漪事件
  Voices.h           12 种音色的合成器（多泛音 + ADSR + 同音复用）
  AmbienceFX.h       乒乓延时 + Schroeder 混响 + 总线软限幅
  PluginProcessor.*  插件主体：内置音源、MIDI 输出、状态序列化
  PluginEditor.*     界面：画布 + 控制区 + LookAndFeel
```

---

## 许可

尚未指定开源许可证。在添加 LICENSE 之前，默认保留所有权利。
