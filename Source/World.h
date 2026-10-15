#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>
#include "Ball.h"
#include "ChordEngine.h"
#include "TriggerQueue.h"
#include "Voices.h"

// 物理世界：圆容器 + 弹球
//  ⚠️ 线程模型（2026-10-15 起）：
//    · 音频线程：audioThreadUpdate（内部串起 advancePhysicsLocked / advanceArpLocked /
//      refreshArpActiveLocked，只读写 balls 的元素）
//    · 消息线程：增删球、改球参数、读球状态画界面
//    · 用 uiLock 串行化：音频线程 tryEnter（抢不到就跳过这一块，绝不阻塞音频），
//      消息线程 enter（可以等）
//    · balls 是 std::vector：push_back / erase 会重分配 → 不保护的并发读是 use-after-free
class World
{
public:
    ChordEngine engine;
    std::vector<Ball> balls;
    std::vector<int> selection;   // 选中的球索引（多选）
    int selected = -1;            // 主选中（属性面板显示用；= selection 的最后一个）

    // 保护 balls 容器的结构 + 元素读写（音频线程 tryEnter，消息线程 enter）
    juce::SpinLock uiLock;

    bool preview = true;          // 内置音源开关
    int  timbre  = 6;             // 全局音色记录（默认 Pluck）。新球音色现由智能推荐给出，
                                  // 此字段仅随工程保存/恢复，保留以便后续"统一设置"用

    // 传输控制
    bool  followDaw = false;       // true=跟随 DAW 速度/播放；false=用自己的 BPM（默认）
    bool  manualPlaying = true;    // 手动 Run/Stop：默认直接开跑，方便监听
    double manualBpm = 60.0;       // 默认 BPM 60

    static constexpr int maxBalls = 32;

    // 触发回调（音频线程）：
    //   index            → 这个球在 balls 里的下标（音频线程用它查 Mute/Solo 闸门，
    //                      避免一条 onTrigger 里还要再扫一遍 balls 找自己）
    //   overrideNote < 0 → 用球自己的音高（碰撞触发）
    //   overrideNote ≥ 0 → 用这个音高（琶音器接管）
    //   velScale         → 力度缩放（Chord 模式一次多颗音，按 1/√n 压一档，免得爆）
    std::function<void (const Ball&, int index, int overrideNote, float velScale)> onTrigger;

    //=== 容器水纹动效：碰撞时记录碰撞点，画布据此让容器外缘产生水波形变并回弹 ===
    //   colour = 撞球颜色（容器内部的水波圈用它，画得更浅）
    //  ★ 水纹分两类（靠 fromEdge 区分画法）：
    //    · 撞壁（fromEdge=true） —— 撞点必在容器圆周上，位置由 angle 决定；
    //                               还要驱动**容器外缘**的形变（外缘只会被壁撞扰动）
    //    · 球撞球（fromEdge=false）—— 撞点在容器**内部任意处**，只画内部水波圈，
    //                               不参与外缘形变（否则外缘会莫名其妙地抖）
    //   px/py = 碰撞点的归一化坐标（圆心 0,0，容器半径 1）—— 内部水波圈一律用它定位
    //  ⚠️ 音频线程（addRipple）写、UI 线程（paint）读写 → 与 balls 共用 uiLock。
    //     调用 addRipple 时必须已持有 uiLock（advancePhysicsLocked 里正是如此）。
    struct Ripple
    {
        float angle;      // 撞点极角（撞壁用；球撞球时也填，便于调试/兼容）
        float px, py;     // 撞点归一化坐标（水波圈的真正中心）
        float strength;
        double time;
        juce::Colour colour;
        bool fromEdge;    // true = 撞壁（参与外缘形变）
    };
    std::vector<Ripple> ripples;

    // 供 UI 线程安全取快照（锁内拷贝 + 顺手清掉过期项）。返回这一帧要画的水纹。
    std::vector<Ripple> snapshotRipples (double nowSec, double maxAge, int maxKeep = 24)
    {
        std::vector<Ripple> out;
        const juce::SpinLock::ScopedTryLockType sl (uiLock);
        if (! sl.isLocked()) return out;

        // 清过期（原来在 paint 里做，无锁 → 与音频线程 push_back 打架）
        for (int i = (int) ripples.size() - 1; i >= 0; --i)
            if (nowSec - ripples[(size_t) i].time > maxAge)
                ripples.erase (ripples.begin() + i);
        if ((int) ripples.size() > maxKeep)
            ripples.erase (ripples.begin(), ripples.begin() + (ripples.size() - (size_t) maxKeep));

        out = ripples;
        return out;
    }

    // 是否还有水纹在动（供 UI 决定要不要继续 60fps 重绘）。
    //  ⚠️ 用 try-lock：抢不到就保守返回 true（宁可多画一帧，也不要漏掉动画尾巴）
    bool hasRipples() const
    {
        const juce::SpinLock::ScopedTryLockType sl (uiLock);
        if (! sl.isLocked()) return true;
        return ! ripples.empty();
    }

    //=== 水纹写入（调用方须已持 uiLock）=====================================
    //  · addRippleAtAngle —— 撞壁：撞点必在圆周上，顺带驱动外缘形变
    //  · addRippleAtPoint —— 球撞球：撞点在容器内部任意处，只画内部水波圈
    void addRippleAtAngle (float angle, float strength, juce::Colour colour)
    {
        // 圆周上的撞点（归一化坐标，半径取 1 稍内侧，与外缘形变同一基准）
        ripples.push_back ({ angle,
                             std::cos (angle), std::sin (angle),
                             strength,
                             juce::Time::getMillisecondCounterHiRes() / 1000.0,
                             colour,
                             true });
        trimRipples();
    }

    void addRippleAtPoint (float x, float y, float strength, juce::Colour colour)
    {
        const float ang = std::atan2 (y, x);
        ripples.push_back ({ ang, x, y, strength,
                             juce::Time::getMillisecondCounterHiRes() / 1000.0,
                             colour,
                             false });
        trimRipples();
    }

    void addRipple (float angle, float strength, juce::Colour colour)
    {
        addRippleAtAngle (angle, strength, colour);
    }

    // 修剪水纹数量（保持在 24 条以内）。public 是刻意的：
    //  下面 addRippleAtAngle/Point 会调用它，而 World 里 public/private 是交错分段的，
    //  保持 public 可以避免误把中间那一大段 public API 变成 private（踩过这个坑）。
    void trimRipples()
    {
        if (ripples.size() > 24)
            ripples.erase (ripples.begin());
    }

    // 内部小节推进（不依赖 DAW 播放头，手动模式也能换和弦）
    double beatsAccum = 0.0;          // 累计拍数（4 拍 = 1 小节）
    bool   internalBarAdvance = true; // true=World 自己推进小节；跟随 DAW 时置 false

    // 全局速度系数：固定 0.5（用户确认 0.5x 手感合适，不开放调节）
    static constexpr float globalSpeed = 0.5f;

    // 是否有球开着 ARP（音频线程每块刷新一次，见 audioThreadUpdate）
    bool arpActive = false;

    // 是否有任意球处于 Solo（Solo 是高优先级、全局生效的）
    bool anySolo() const
    {
        for (const auto& b : balls)
            if (b.solo)
                return true;
        return false;
    }

    //==== 音频线程的统一入口 ================================================
    //  把「同步小节号 / 推进物理 / 推进琶音 / 刷新 arpActive」打包成一次调用，
    //  内部只持一次锁 —— 避免多次 try-lock 之间的窗口期造成状态撕裂。
    //  ★ 由 PluginProcessor::processBlock 每个音频块调一次。
    //    bar < 0 表示"本次不对齐小节号"（手动模式由内部 beatsAccum 累计）。
    void audioThreadUpdate (double dt, double bpm, bool playing, int bar)
    {
        // ⚡ 停止播放 → 物理和琶音都不该动，这时**连锁都不用抢**。
        //     原来仍会每块 try-lock + 遍历 32 球做 O(n²) 碰撞检测（约 50 万次/秒），
        //     纯粹白烧 CPU。这里提前返回，待机占用直接归零。
        if (! playing)
        {
            // 仍需把"琶音累加器清零"这件收尾做掉（重开时从对齐拍点起步）——
            // 但只在真的有过 ARP 时才抢锁，避免无谓开销
            if (arpActive)
            {
                const juce::SpinLock::ScopedTryLockType sl (uiLock);
                if (sl.isLocked())
                {
                    for (auto& b : balls) b.arpAccum = 0.0;
                    arpActive = false;
                }
            }
            return;
        }

        const juce::SpinLock::ScopedTryLockType sl (uiLock);
        if (! sl.isLocked())
            return;   // 用户正在增删球/改参数，本块跳过（几十毫秒的物理缺失听不出来）

        if (bar >= 0)
            engine.setBar (bar);

        advancePhysicsLocked (dt, bpm, playing);
        advanceArpLocked (dt, bpm, playing);
        refreshArpActiveLocked();
    }

    bool isArpActive() const { return arpActive; }

    //=== 选择操作 ===
    bool isSelected (int i) const
    {
        return std::find (selection.begin(), selection.end(), i) != selection.end();
    }
    void clearSelection() { selection.clear(); selected = -1; }
    void setSelected (int i)
    {
        selection.clear();
        if (i >= 0) { selection.push_back (i); selected = i; }
        else selected = -1;
    }
    void addToSelection (int i)
    {
        if (i >= 0 && ! isSelected (i)) { selection.push_back (i); selected = i; }
    }
    void toggleSelected (int i)
    {
        auto it = std::find (selection.begin(), selection.end(), i);
        if (it != selection.end())
        {
            selection.erase (it);
            selected = selection.empty() ? -1 : selection.back();
        }
        else addToSelection (i);
    }

    // 全部解除"被抓住"状态（拖拽被意外打断时的兜底：
    // 例如拖动过程中按 Del 删球、宿主夺走鼠标、窗口失焦等）。
    //  ⚠️ 漏了这一步的后果是那颗球**永远不再运动**（物理每帧都 continue 跳过它），
    //    而且 UI 上看不出异常 —— 所以宁可多调几次，也不要留着 held=true。
    void releaseAllHeld()
    {
        const juce::SpinLock::ScopedLockType sl (uiLock);
        for (auto& b : balls)
            b.held = false;
    }

    void addBall (juce::Point<float> pos)
    {
        // 消息线程：可以等（阻塞一小会儿换取音频线程安全）
        const juce::SpinLock::ScopedLockType sl (uiLock);

        if ((int) balls.size() >= maxBalls)
            return;
        Ball b;
        b.pos = pos;
        float ang = (float) juce::Random::getSystemRandom().nextDouble() * juce::MathConstants<float>::twoPi;
        b.vel = { std::cos (ang), std::sin (ang) };
        // 低饱和多彩：浅雅白底上清晰又不刺眼
        static const juce::Colour palette[] = {
            juce::Colour (0xff7d9fc4),   // 雾蓝
            juce::Colour (0xff8aad8f),   // 灰绿
            juce::Colour (0xffc98f88),   // 陶粉
            juce::Colour (0xffc4a468),   // 沙金
            juce::Colour (0xff9a87bd),   // 雾紫
            juce::Colour (0xff7fb0b5)    // 灰青
        };
        // 挑一个"现有球还没用过"的颜色（简单取 size%6 会在删除/加满一轮后撞色）
        {
            const int nPal = 6;
            juce::Colour choice = palette[(int) balls.size() % nPal];
            for (int k = 0; k < nPal; ++k)
            {
                const juce::Colour c = palette[((int) balls.size() + k) % nPal];
                bool used = false;
                for (const auto& other : balls)
                    if (other.colour == c) { used = true; break; }
                if (! used) { choice = c; break; }
            }
            b.colour = choice;
        }

        // 新球参数交给"声部推荐"：按「低音 → 中音 → 高音」依次铺开
        //  声位 = 当前球数（push_back 之前）→ 第 1 个球是 Bass，第 4 个是 Soprano
        b.autoRegister = true;
        const BallPreset pre = recommendPreset (-1, (int) balls.size());
        b.tone   = pre.tone;
        b.note   = pre.note;
        b.rate   = pre.rate;
        b.timbre = pre.timbre;

        // 给个随机的初始相位，避免新加的球都从同一个音级起步听起来一样
        b.arpStep = juce::Random::getSystemRandom().nextInt (64);

        balls.push_back (b);
        setSelected ((int) balls.size() - 1);
    }

    void removeSelected()
    {
        const juce::SpinLock::ScopedLockType sl (uiLock);

        // 从后往前删，避免索引错位
        std::sort (selection.begin(), selection.end(), std::greater<int>());
        for (int idx : selection)
            if (idx >= 0 && idx < (int) balls.size())
                balls.erase (balls.begin() + idx);
        clearSelection();
    }

    // 把 from 位置的球移动到 to 位置（底部卡片条拖动排序用），并修正选中索引
    void moveBall (int from, int to)
    {
        const juce::SpinLock::ScopedLockType sl (uiLock);

        const int n = (int) balls.size();
        from = juce::jlimit (0, n - 1, from);
        to   = juce::jlimit (0, n - 1, to);
        if (from == to) return;

        auto b = balls[(size_t) from];
        balls.erase (balls.begin() + from);
        balls.insert (balls.begin() + to, b);

        // 修正 selected / selection：from→to，区间内整体平移 1
        auto fix = [from, to] (int idx)
        {
            if (idx == from) return to;
            if (from < to && idx > from && idx <= to) return idx - 1;
            if (to < from && idx >= to && idx < from) return idx + 1;
            return idx;
        };
        selected = selected >= 0 ? fix (selected) : selected;
        for (auto& idx : selection)
            idx = fix (idx);
    }

    void clearAll()
    {
        const juce::SpinLock::ScopedLockType sl (uiLock);
        balls.clear();
        clearSelection();
        beatsAccum = 0.0;
        engine.setBar (0);
    }

    // 琶音循环归零（开关 ARP 或改参数时调用，让循环从头开始，不接着上次的相位走）
    void resetArp (int index)
    {
        if (index < 0 || index >= (int) balls.size()) return;
        balls[(size_t) index].arpStep  = 0;
        balls[(size_t) index].arpAccum = 0.0;
    }

    //=== 音色音区层（与 Voices.h 的排列一致）===============================
    //   Low : Sub / Pad / Organ / Strings / Choir        (0-4)
    //   Mid : Rhodes / Pluck / Marimba / Kalimba         (5-8)
    //   High: Bell / Glass / Music Box / Celesta         (9-12)
    static int timbreLayerOf (int timbre)
    {
        static const int layerOf[Timbre::num] = { 0, 0, 0, 0, 0,   1, 1, 1, 1,   2, 2, 2, 2 };
        return layerOf[juce::jlimit (0, Timbre::num - 1, timbre)];
    }

    // 指定音区层里挑一个"其它球没在用"的音色；该层全被占用则取层内第一个
    //  excludeIndex: 被推荐球自身不参与统计（多选时逐个推荐，先改完的也算"已占用"）
    int timbreForLayer (int layer, int excludeIndex) const
    {
        bool used[Timbre::num] = { false };
        forEachOtherBall (excludeIndex, [&] (const Ball& b)
        {
            used[juce::jlimit (0, Timbre::num - 1, b.timbre)] = true;
        });

        int fallback = -1;
        for (int t = 0; t < Timbre::num; ++t)
        {
            if (timbreLayerOf (t) != layer) continue;
            if (fallback < 0) fallback = t;
            if (! used[t]) return t;                     // 优先没被占用的
        }
        return juce::jlimit (0, Timbre::num - 1, fallback >= 0 ? fallback : 0);
    }

    //=== 智能推荐：按「低音 → 中音 → 高音」依次铺开声部 ====================
    //  用于两处：+ Add（新球）与 Ensemble（优化选中球）。
    //
    //  ★ 编排原则（用户确认）：声部不是"哪个档位用得少就补哪个"，
    //    而是自下而上一层一层堆叠 —— 第 1 个球是低音，第 2 个中低，
    //    第 3 个中高，第 4 个高音。音区 / 音色层 / 速度三项同时单调上行：
    //
    //      slot 0  Bass      根音 · C2 区 · 低音音色 · 1/2（铺底，最慢）
    //      slot 1  Baritone  五音 · C3 区 · 低音音色 · 1/4
    //      slot 2  Alto      三音 · C4 区 · 中音音色 · 1/8
    //      slot 3  Soprano   七音 · C5 区 · 高音音色 · 1/16（点缀，最快）
    //
    //    低音区放根音 + 五音（空五度，稳），中高区放三音 + 七音（色彩）——
    //    这是氛围音乐典型的分层；速度也随音区加快，层次一眼可辨。
    //    超过 4 个球后循环一轮（音色会自动错开，不会糊在一起）。
    //
    //  分析维度：其它球的"实际发声音高"按 follow 状态换算
    //    （跟随球 = 当前和弦下的发声），保证避让判断用的是真实听到的音。
    struct BallPreset { int note = 60; int tone = 0; int rate = 2; int timbre = 6; };

    struct VoiceRole
    {
        int         tone;         // 和弦档位 0=根 1=三 2=五 3=七
        int         octave;       // 目标八度（note = (octave+1)*12 + pc）
        int         timbreLayer;  // 0=Low 1=Mid 2=High
        const char* name;
    };

    static const VoiceRole& voiceRole (int slot)
    {
        static const VoiceRole roles[4] = {
            { 0, 2, 0, "Bass"     },
            { 2, 3, 0, "Baritone" },
            { 1, 4, 1, "Alto"     },
            { 3, 5, 2, "Soprano"  },
        };
        return roles[juce::jlimit (0, 3, slot)];
    }
    static constexpr int numVoiceRoles = 4;

    // 速度推荐：分析现有球的 rate 分布 + 声部音区倾向，不是死表
    //  ★ 两条原则：
    //    1) 节奏层次要铺开 —— 某档被占得越多，越不推荐它（避免全员同速）
    //    2) 音区与速度协调 —— 低音天然偏慢（铺底）、高音偏快（点缀），
    //       但这只是"倾向"不是"规定"：低音区被占用时照样可以拿中速档
    //  roleIndex: 声部序号（0=Bass ... 3=Soprano，取 slot % 4）
    int rateForRole (int roleIndex, int excludeIndex) const
    {
        int count[4] = { 0, 0, 0, 0 };
        forEachOtherBall (excludeIndex, [&] (const Ball& b)
        {
            ++count[juce::jlimit (0, 3, b.rate)];
        });

        // 各声部对 4 档速度的偏好顺序（越靠前越贴合该音区的律动）
        static const int prefer[4][4] = {
            { 0, 1, 2, 3 },   // Bass     C2: 1/2 > 1/4 > 1/8 > 1/16
            { 1, 2, 0, 3 },   // Baritone C3: 1/4 > 1/8 > 1/2 > 1/16
            { 2, 1, 3, 0 },   // Alto     C4: 1/8 > 1/4 > 1/16 > 1/2
            { 3, 2, 1, 0 },   // Soprano  C5: 1/16 > 1/8 > 1/4 > 1/2
        };
        const int* pref = prefer[juce::jlimit (0, 3, roleIndex)];

        int best = pref[0], bestScore = -1000000;
        for (int k = 0; k < 4; ++k)
        {
            const int rate = pref[k];
            // 占用越少越好（每多一个重复 -8 分），偏好顺序次之（每退一名 -3 分）
            //  → 分布空旷时按音区倾向给；分布拥挤时自动避开被占的档
            const int score = -count[rate] * 8 - k * 3;
            if (score > bestScore) { bestScore = score; best = rate; }
        }
        return best;
    }

    int effectiveNote (const Ball& b) const
    {
        return b.follow ? engine.followNote (b.note, b.tone, b.octaveShift)
                        : juce::jlimit (0, 127, b.note + b.pitchOffset);
    }

    // 琶音池用的"原始音高"（**不含** pitchOffset）
    //  pitchOffset 统一由 triggerBall 在最后一步加：若这里也加，非跟随球会被偏移两次
    int baseNote (const Ball& b) const
    {
        return b.follow ? juce::jlimit (0, 127, engine.followNote (b.note, b.tone, b.octaveShift))
                        : juce::jlimit (0, 127, b.note);
    }

    template <typename Fn>
    void forEachOtherBall (int excludeIndex, Fn&& fn) const
    {
        for (int i = 0; i < (int) balls.size(); ++i)
        {
            if (i == excludeIndex) continue;
            fn (balls[(size_t) i]);
        }
    }

    // 按声部角色算音高：目标八度 + 该档位的和弦音 pc
    //  ★ 声部八度是编排的骨架，不能为了"离得更远"就随便挪 ——
    //    只有真的撞上（与某球 < 3 半音：同度 / 小二度）才上下挪一个八度避让，
    //    而且"够远就停"，不贪最远（早期版本贪心求远，把球一路推到 C1 / A7）。
    int noteForRole (const VoiceRole& r, int excludeIndex) const
    {
        const int pc   = engine.chordTonePc (r.tone);
        const int base = (r.octave + 1) * 12 + pc;

        std::vector<int> others;
        forEachOtherBall (excludeIndex, [&] (const Ball& b) { others.push_back (effectiveNote (b)); });
        if (others.empty())
            return juce::jlimit (0, 127, base);

        auto minGapTo = [&] (int n)
        {
            int g = 127;
            for (int o : others) g = juce::jmin (g, std::abs (n - o));
            return g;
        };

        if (minGapTo (base) >= 3)
            return juce::jlimit (0, 127, base);      // 不打架 → 严格守本声部八度

        // 撞击 → 按"移动距离从小到大"试，够远（≥3 半音）就立刻采用
        for (int d : { 12, -12, 24, -24 })
        {
            const int cand = base + d;
            if (cand < 24 || cand > 108) continue;
            if (minGapTo (cand) >= 3)
                return juce::jlimit (0, 127, cand);
        }

        // 全都挤在一起（球很多时）→ 退而求其次，选离得最松的那个
        int best = base, bestGap = minGapTo (base);
        for (int d : { 12, -12, 24, -24 })
        {
            const int cand = base + d;
            if (cand < 24 || cand > 108) continue;
            const int g = minGapTo (cand);
            if (g > bestGap) { bestGap = g; best = cand; }
        }
        return juce::jlimit (0, 127, best);
    }

    // 汇总：音高 / 档位 / 速度 / 音色 一次给全
    //   slot:         该球担任的声位 —— 0=最低 ... 3=最高，>3 循环一轮
    //                 （Add 时传"当前球数"，Ensemble 时传"音高排名"）
    //   excludeIndex: Ensemble 时传入选中球自身，它不参与统计（多选时逐个推荐）
    BallPreset recommendPreset (int excludeIndex, int slot) const
    {
        const int roleIndex = ((slot % numVoiceRoles) + numVoiceRoles) % numVoiceRoles;
        const VoiceRole& r = voiceRole (slot);
        BallPreset p;
        p.tone   = r.tone;
        p.rate   = rateForRole (roleIndex, excludeIndex);   // 分析分布，不死表
        p.timbre = timbreForLayer (r.timbreLayer, excludeIndex);
        p.note   = noteForRole (r, excludeIndex);
        return p;
    }

    // Ensemble 按钮行为：把选中的球按"当前音高从低到高"重排声部
    //  ★ 声位由"该球在选中集合里的音高排名"决定 —— 最低的当 Bass，最高的当 Soprano。
    //    所以点一次 Ensemble，整组合奏就被梳理成 低音→中低→中高→高音 的层次。
    //    只改选中的球，不动其它球；音色同时按声部音区层重新分派。
    void applyEnsembleToSelection()
    {
        // 1) 收集要改的球
        std::vector<int> targets;
        if (selection.empty())
        {
            if (selected >= 0 && selected < (int) balls.size())
                targets.push_back (selected);
        }
        else
        {
            for (int idx : selection)
                if (idx >= 0 && idx < (int) balls.size())
                    targets.push_back (idx);
        }
        if (targets.empty()) return;

        // 2) 按当前发声音高升序排 → 排名就是声位
        std::sort (targets.begin(), targets.end(), [&] (int a, int b)
        {
            return effectiveNote (balls[(size_t) a]) < effectiveNote (balls[(size_t) b]);
        });

        // 3) 依次分派：排名 0 = Bass（最低）…… 排名 3 = Soprano（最高）
        for (int k = 0; k < (int) targets.size(); ++k)
        {
            const int idx = targets[(size_t) k];
            const BallPreset p = recommendPreset (idx, k);
            auto& b = balls[(size_t) idx];
            b.note   = p.note;
            b.tone   = p.tone;
            b.rate   = p.rate;
            b.timbre = p.timbre;
        }
    }

    //=== 物理推进（调用方必须已持有 uiLock）================================
    //  ★ 由音频线程按采样时钟调用（见 audioThreadUpdate）。
    //    这是"关掉插件窗口后依然发声"的关键：球必须一直在动、一直在撞，
    //    碰撞才会经 fire() 推触发队列。物理放在 UI 定时器里的话，
    //    窗口一关整个乐器就静止了。
    //  dt 已被音频块大小决定，这里再 clamp 一次防止宿主给超大块时穿透容器。
    void advancePhysicsLocked (double dt, double bpm, bool playing)
    {
        if (! playing)
            return;

        dt = juce::jlimit (0.0, 0.05, dt);

        //=== 内部小节推进：4 拍 = 1 小节（仅手动模式）===
        if (internalBarAdvance)
        {
            beatsAccum += dt * (bpm / 60.0);
            if (beatsAccum >= 4.0)
            {
                beatsAccum -= 4.0;
                engine.setBar (engine.getBar() + 1);
            }
        }

        for (auto& b : balls)
        {
            b.cooldown = juce::jmax (0.0f, b.cooldown - (float) dt);

            //=== 被鼠标抓住的球：位置交给 UI 线程托管，物理完全不碰 ===
            //  ⚠️ 必须 continue，不能只跳过 b.pos 的更新：
            //     下面会"把速度模长归一到目标速度"，被抓时球常常是静止的
            //     （cur ≈ 0 会走 else 分支随机给个方向）→ 松手瞬间球会自己飞出去。
            //     连速度也不动，松手时再由 UI 按鼠标位移方向给一个速度。
            //  同时球-球碰撞那一段也要跳过它（见下面 held 判断），
            //  否则别的球撞上来照样能把它推开。
            if (b.held)
                continue;

            //=== 速度模型（按音乐时值） ===
            // 档位 = 小球完成"一次容器内往返"所占的音符时值：
            //   1/2  → 2 拍往返一次
            //   1/4  → 1 拍往返一次
            //   1/8  → 0.5 拍往返一次（即每拍 2 次往返）
            //   1/16 → 0.25 拍往返一次（即每拍 4 次往返）
            // 往返一次 ≈ 沿半径方向来回，路程约 1.0（用户确认 2.0 仍偏快，减半）
            // 线速度 = 路程 / 该时值对应的秒数
            const double noteBeats  = Ball::rateBeats (b.rate);           // 该档位的拍数
            const double noteSecs   = juce::jmax (0.02, noteBeats * 60.0 / juce::jmax (20.0, bpm));
            const double rawSpeed   = 1.0 / noteSecs;                     // 单位/秒（再减半）
            // 视觉上限：太快会看着糊，限制在 3.5 单位/秒 以内
            const float  targetSpeed = (float) juce::jlimit (0.05, 3.5,
                                                             rawSpeed * (double) globalSpeed);

            // 速度方向保持不变，模长归一到目标速度（避免碰撞后失控累积）
            const float cur = b.vel.getDistanceFromOrigin();
            if (cur > 1e-6f)
            {
                auto dir = b.vel / cur;
                b.vel = dir * targetSpeed;
            }
            else
            {
                float ang = (float) juce::Random::getSystemRandom().nextDouble()
                            * juce::MathConstants<float>::twoPi;
                b.vel = { std::cos (ang) * targetSpeed, std::sin (ang) * targetSpeed };
            }

            b.pos += b.vel * (float) dt;

            // 壁碰撞 → 触发 + 容器边缘水纹
            const float r = b.pos.getDistanceFromOrigin();
            if (r > 1.0f - Ball::radius)
            {
                auto n = b.pos / r;
                b.vel = reflect (b.vel, n);
                b.pos = n * (1.0f - Ball::radius);
                fire (b);
                // 撞得越快，水纹越明显（记录撞点角度 + 球色，供容器内部水波圈使用）
                //  ⚠️ 用 AtAngle 版本：撞壁的撞点在圆周上，还要驱动容器外缘形变
                addRippleAtAngle (std::atan2 (n.y, n.x),
                                  juce::jlimit (0.5f, 1.8f, b.vel.getDistanceFromOrigin() * 0.65f),
                                  b.colour);
            }
        }

        // 球-球碰撞（等质量弹性碰撞）→ 双方触发
        //  ⚠️ 被鼠标抓住的球跳过：否则别的球撞上来会把它推走（"抓住还在动"的一个来源），
        //     而且反弹冲量会写进它的速度，松手后带着一股怪力飞出去。
        for (size_t i = 0; i < balls.size(); ++i)
        {
            if (balls[i].held) continue;

            for (size_t j = i + 1; j < balls.size(); ++j)
            {
                auto& a = balls[i];
                auto& c = balls[j];
                if (c.held) continue;
                auto d = c.pos - a.pos;
                const float dist = d.getDistanceFromOrigin();
                const float minDist = 2.0f * Ball::radius;
                if (dist > 1e-6f && dist < minDist)
                {
                    auto n = d / dist;
                    const float van = dot (a.vel, n);
                    const float vbn = dot (c.vel, n);
                    if (van - vbn > 0) // 相向才处理
                    {
                        // 撞击强度 = 相对接近速度（|van - vbn|），映射到与撞壁同一量纲
                        //   撞壁用的是 vel*0.65（vel 上限 3.5）→ 约 0.5~1.8
                        //   球撞球的最大接近速度可达 2×speed，故再乘 0.45 收敛到相似区间
                        const float impact = juce::jlimit (0.35f, 1.8f, (van - vbn) * 0.45f);

                        a.vel += n * (vbn - van);
                        c.vel += n * (van - vbn);
                        fire (a);
                        fire (c);

                        //=== 球撞球的水纹 =========================================
                        //  撞点 = 两球接触点（取两球心连线的中点，即相切点附近）。
                        //  用 AtPoint 版本：撞点在容器内部任意处，不驱动外缘形变。
                        //  ⚠️ 放在"相向才处理"分支里 —— 否则两球贴着滑过时会每帧刷水纹。
                        //  颜色取两球混合，视觉上同时"属于"两颗球。
                        const float mx = (a.pos.x + c.pos.x) * 0.5f;
                        const float my = (a.pos.y + c.pos.y) * 0.5f;
                        addRippleAtPoint (mx, my, impact,
                                          a.colour.interpolatedWith (c.colour, 0.5f));
                    }
                    // 分离重叠
                    const float push = (minDist - dist) * 0.5f;
                    a.pos -= n * push;
                    c.pos += n * push;
                }
            }
        }
    }

    //=== 琶音器推进（调用方必须已持有 uiLock）==============================
    //  ★ 由音频线程按采样时钟调用（见 audioThreadUpdate），不是 UI 的
    //    60fps 定时器 —— 这样关闭插件窗口后节奏照常走，且不会被 UI 抖动影响。
    //
    //  ⚠️ 被 Mute / Solo 排除的球是"只静音、不停走"：这里不 skip，
    //     照样推进它的 arpStep / arpAccum —— 所以取消 M 之后，
    //     再进来的音正好落在它本来该在的拍位上，不是"重新开始"。
    //     （真正挡住发声的是 triggerBall 里那个闸门，以及输出端的 muteGain。）
    //
    //  playing 语义（与碰撞触发一致）：
    //    followDaw = true  → 只在宿主走带时发声（关掉窗口当然也照走）
    //    followDaw = false → 跟随手动 Run/Stop（此时窗口关掉就停，属预期，
    //                        因为播放状态本来就由界面上的 Run 按钮控制）
    void advanceArpLocked (double dt, double bpm, bool playing)
    {
        if (! playing)
        {
            // 停的时候把累加器清零，重新开始时从对齐的拍点起步
            for (auto& b : balls)
                b.arpAccum = 0.0;
            return;
        }

        const double secPerBeat = 60.0 / juce::jmax (20.0, bpm);

        for (auto& b : balls)
        {
            if (! b.arpOn) continue;
            // ← 这里刻意不判 mute/solo：见上方注释

            const double stepSecs = juce::jmax (0.02, Ball::rateBeats (b.arpRate) * secPerBeat);
            b.arpAccum += dt;

            int guard = 0;                    // dt 已被 clamp，正常最多走 1~2 步
            while (b.arpAccum >= stepSecs && guard++ < 8)
            {
                b.arpAccum -= stepSecs;
                ++b.arpStep;                  // ⚠️ 走位永远推进，与是否发声无关

                //=== 概率触发 ==============================================
                //  按球自己的 probability 决定"这一步发不发声"。
                //  ★ 关键：arpStep 已经在上面 ++ 过了 —— 所以漏掉的这一步
                //    仍然占掉一个循环位置，下一次发声会落在它本该在的拍上，
                //    听起来是"有规律的疏密"，而不是"整体往后拖了一格"。
                //  ★ 放在 arpNotesFor 之前：算音级有点开销（要查和弦），
                //    掷骰子不过就干脆不算了。
                if (! b.rollTrigger())
                    continue;

                int notes[12];
                const int n = arpNotesFor (b, notes);
                // 一次多颗音（Chord）按 1/√n 压一档，免得叠出来爆
                const float velScale = n > 1 ? 1.0f / std::sqrt ((float) n) : 1.0f;

                for (int k = 0; k < n; ++k)
                    if (onTrigger)
                        onTrigger (b, indexOf (b), notes[k], velScale);   // 绕过碰撞去抖：ARP 自己就是节奏源
            }
        }
    }

private:
    // 按引用反查球下标（触发回调要带 index 给音频线程查 Mute/Solo 闸门）。
    //  balls 是 vector<Ball>，地址相减即可，不用逐元素比较。
    int indexOf (const Ball& b) const
    {
        if (balls.empty()) return -1;
        const Ball* first = balls.data();
        const Ball* p = &b;
        if (p < first || p >= first + balls.size()) return -1;
        return (int) (p - first);
    }

    // Point<float> 没有内置点积，手动实现
    static float dot (const juce::Point<float>& a, const juce::Point<float>& b)
    {
        return a.x * b.x + a.y * b.y;
    }

    static juce::Point<float> reflect (juce::Point<float> v, juce::Point<float> n)
    {
        return v - n * (2.0f * dot (v, n));
    }

    //=== 琶音激活状态刷新（调用方必须已持有 uiLock）========================
    //  结果给效果链的静默旁路判断用（ARP 在跑时不能让效果链旁路）
    //  ⚠️ 用 b.targetGain 而不是手写 `soloing ? b.solo : ! b.mute`：
    //     后者在"S 和 M 同时点亮"时会算成 1（认为在响），但实际闸门是 0，
    //     结果效果链被一直挂着不旁路（白烧 CPU）。
    void refreshArpActiveLocked()
    {
        const bool soloing = anySolo();
        for (const auto& b : balls)
            if (b.arpOn && b.targetGain (soloing) > 0.0f)
            {
                arpActive = true;
                return;
            }
        arpActive = false;
    }

    //=== 琶音器音级计算 =====================================================
    //  音级池 = 当前和弦的 4 个和弦音，从"球所在音区的和弦根音"起升序铺开，
    //  按 arpOct 向上叠八度；一个循环的长度 = arpSteps；方向由 arpMode 决定。
    //  返回本次要走出的音数（Chord 模式一次给全部），音写进 out（容量 ≥ 12）
    int arpNotesFor (const Ball& b, int* out) const
    {
        //  ⚠️ 用 baseNote（不含 pitchOffset）：pitchOffset 由 triggerBall 统一加，算两次会跑调
        const int base  = baseNote (b);
        const int oct   = juce::jlimit (1, 3, b.arpOct);
        const int steps = juce::jlimit (2, 8, b.arpSteps);
        const int pool  = 4 * oct;

        // ① 以"和弦根音"为基准算四个和弦音的半音间隔（天然升序 根 < 三 < 五 < 七）
        const int rootPc = engine.chordTonePc (0);
        int iv[4];
        for (int t = 0; t < 4; ++t)
            iv[t] = ((engine.chordTonePc (t) - rootPc) + 12) % 12;

        // ② 找"不高于 base 的最近那个和弦根音" → 琶音落回球自己的音区
        //    ⚠️ 不能从 base 的音级直接加间隔：球可能站在三音/五音上，
        //       那样加出来会是和弦外音（base=五音时 +三度 = 小七度，跑了）
        const int rootNote = base - ((base % 12 - rootPc) + 12) % 12;

        auto makeNote = [&] (int k)
        { return juce::jlimit (0, 127, rootNote + iv[k % 4] + 12 * (k / 4)); };

        if (b.arpMode == 4)   // Chord：一步同时扫出全部和弦音
        {
            for (int k = 0; k < pool; ++k)
                out[k] = makeNote (k);
            return pool;
        }

        int idx = b.arpStep % steps;
        switch (juce::jlimit (0, 4, b.arpMode))
        {
            case 1:  idx = steps - 1 - idx; break;                          // Down
            case 2: { const int period = 2 * steps - 2;                      // Up-Down（三角）
                      const int p = b.arpStep % period;
                      idx = (p < steps) ? p : (period - p); break; }
            case 3:  idx = juce::Random::getSystemRandom().nextInt (steps); break;  // Random
            default: break;                                                  // Up
        }

        out[0] = makeNote (idx % pool);
        return 1;
    }

    void fire (Ball& b)
    {
        if (b.cooldown > 0.0f)
            return;
        b.cooldown = 0.15f;

        //=== 概率触发：这一次碰撞到底响不响 ================================
        //  放在 cooldown / Solo / Mute 之前 —— 语义是"这次碰撞有没有发生音"，
        //  它跟静音是两回事（静音是"想响但不许响"，概率是"本来就没打算响"）。
        //  ⚠️ 掷骰子必须在 cooldown 之后：否则连着撞十下会把去抖窗口里的
        //     十次全摇一遍，实际触发率会偏高于设定值（去抖本来就该先吃掉重复）。
        //  ★ 不触发时直接 return，但 cooldown 已经置上了 —— 保持"碰撞节流"
        //    的节奏一致，不会因为没响就把下一次碰撞提前放进来。
        if (! b.rollTrigger())
            return;

        // Solo 优先于 Mute：只要有球 Solo，未 Solo 的球一律不发声。
        //  ⚠️ 这里是在"源头"挡住触发，语义上等价于"音量减到 0"：
        //     球照常碰撞、照常出水纹（调用方在这之前已经记了），只是不推触发。
        //     谐振音色（Pad / Bell 的长尾）的衰减与取消静音后的衔接，
        //     由 renderVoices 里的 Voice::muteGain 负责 —— 那里才是真正的"音量闸门"。
        if (b.mute && ! anySolo())
            return;
        if (anySolo() && ! b.solo)
            return;
        // 开了琶音器 → 发声交给 ARP 接管，碰撞不再重复触发（否则会糊成一片）
        if (b.arpOn)
            return;

        if (onTrigger)
            onTrigger (b, indexOf (b), -1, 1.0f);
    }

};
