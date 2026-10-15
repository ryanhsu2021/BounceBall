#pragma once
#include <juce_core/juce_core.h>

// 一个球：归一化坐标，容器中心(0,0)，容器半径 1
struct Ball
{
    juce::Point<float> pos { 0.0f, 0.0f };
    juce::Point<float> vel { 0.3f, 0.2f };

    int  note      = 60;   // 基准音高（follow=off 时即发声；follow=on 时作为跟随之锚点）
    bool follow    = true; // 是否跟随和弦进行
    int  tone      = 0;    // 跟随和弦时：0=根音 1=三音 2=五音 3=七音
    bool autoRegister = false; // true=按档位自动分层音区（低/中/高），和声更清晰
    int  octaveShift = 0;  // 八度偏移（-2..+2），用于微调音高
    int  pitchOffset = 0;  // 半音偏移（-12..+12），两种模式下都生效
    int  rate      = 2;    // 速度档: 0=1/2 1=1/4 2=1/8 3=1/16
    int  channel   = 1;    // MIDI 通道 1-16
    int  timbre    = 6;    // 音色索引（0..Timbre::num-1，见 Voices.h），每个球独立；默认 Pluck

    //=== 独奏 / 静音（与 DAW 的 Solo/Mute 语义一致）===
    //  Mute：单独静音这个球（不出声，但仍参与物理运动与视觉）
    //  Solo：只要列表里有任一球 Solo，就只播 Solo 的球（Solo 优先于 Mute）
    //  ⚠️ 语义是"音量降到 0"，不是"停止发声"：
    //     只作用于输出增益（见 World 的触发闸门 + BounceAudioProcessor 的 Voice::muteGain），
    //     包络 / 琶音走位 / 物理全部照常推进 —— 所以取消 S 或 M 之后
    //     声部是从当前电平无缝淡回，**不会重新从头起音**。
    bool mute = false;
    bool solo = false;

    //=== 每球音量（0..1）===
    //  调节方式：鼠标悬停在小球卡片上滚动滚轮；卡片里"球色浅底"的填充高度就是音量
    float volume = 1.0f;

    //=== 触发概率（0..1）===
    //  球每次碰撞（或琶音走步）时，按这个概率决定"这一次到底发不发声"。
    //  1.0 = 每次都触发（默认，行为与旧版一致）；0.5 = 大约一半的碰撞会响。
    //  ⚠️ 与 volume 的区别，别混：
    //     · volume      = 响的时候有多响（连续量，每次触发都生效）
    //     · probability = 响不响（离散量，逐次掷骰子）
    //  用途：让节奏有"疏密"变化 —— 同一个球可以一会儿密一会儿疏，
    //        做出 humanize / 随机漏音的感觉，而不是死板地每次都响。
    float probability = 1.0f;

    //=== S / M 的目标增益（单一来源，UI 与音频线程共用）===
    //  返回"这个球此刻应该被衰减到多少"：
    //    有球 Solo → 非 Solo 的球 0，Solo 的球 1（Solo 优先于 Mute）
    //    没有 Solo → Mute 的球 0，其余 1
    //  ⚠️ 纯函数，不读任何"平滑后的结果值" —— 否则连续两次调用会
    //     第一次把它置 0、第二次读到 0 又判成"本来就是 0"。
    float targetGain (bool anySoloActive) const
    {
        if (anySoloActive) return solo ? 1.0f : 0.0f;
        return mute ? 0.0f : 1.0f;
    }

    // 该球此刻是否"被静默"（Solo 排除 或 Mute）—— 供 UI 置灰
    bool silenced (bool anySoloActive) const { return targetGain (anySoloActive) <= 0.0f; }

    //=== 琶音器（每球独立；开启后发声由 ARP 接管，碰撞不再重复触发）===
    bool arpOn    = false;  // 开关
    int  arpRate  = 3;      // 步进时长 0=1/2 1=1/4 2=1/8 3=1/16
    int  arpMode  = 0;      // 0=Up 1=Down 2=Up-Down 3=Random 4=Chord（一步扫出全部和弦音）
    int  arpOct   = 1;      // 八度跨度 1..3
    int  arpSteps = 4;      // 一个循环的步数 2..8

    // 运行态（不序列化）
    int    arpStep  = 0;
    double arpAccum = 0.0;  // 累积秒（到达一个步进时长就走一步）

    //=== 是否正被鼠标拖拽 ==================================================
    //  音频线程每帧推进物理时会跳过它（球"被抓住"就该停在指针下不动）。
    //  ⚠️ 它不是"暂停"而是"交给鼠标托管"：
    //     · 物理不推进位置 → 也不会刷新速度方向
    //     · 松开时由 UI 线程用"鼠标位移方向"重新给一个速度（见 BallPanel::mouseUp）
    //       否则松手瞬间会用它被抓之前的老速度飞出去，看着很突兀
    //  不序列化：纯交互瞬态
    bool held = false;

    static constexpr int numArpModes = 5;
    static const char* arpModeName (int i)
    {
        static const char* names[] = { "Up", "Down", "Up-Down", "Random", "Chord" };
        return names[juce::jlimit (0, 4, i)];
    }

    //=== 掷骰子：这一次碰撞 / 琶音步要不要发声 ============================
    //  ① probability >= 1 → 恒真（1.0 是默认值，走这条短路，行为与旧版完全一致）
    //  ② probability <= 0 → 恒假（不是"静音"：琶音仍在走位，只是不发声）
    //  ③ 其余 → 真随机
    //  ★ 用 getSystemRandom 而不是全局 Random：音频线程与 UI 线程各跑各的，
    //    共享全局 Random 会互相扰动状态（虽然不会崩，但随机序列会串）。
    //    这里只要"统计上符合概率"，不需要可复现序列 —— 也没法在音频线程播种。
    //  ★ 返回值是 bool 而不是在调用点直接 continue：碰撞路径与琶音路径都要用，
    //    抽在这里才能保证两边语义永远一致。
    bool rollTrigger() const
    {
        if (probability >= 1.0f) return true;
        if (probability <= 0.0f) return false;
        return juce::Random::getSystemRandom().nextFloat() < probability;
    }

    float cooldown = 0.0f; // 触发去抖（秒）
    juce::Colour colour { juce::Colours::steelblue };

    static constexpr float radius = 0.028f;

    static const char* rateName (int i)
    {
        static const char* names[] = { "1/2", "1/4", "1/8", "1/16" };
        return names[juce::jlimit (0, 3, i)];
    }

    // 每拍碰撞次数（音乐语义：数值越大碰撞越频繁）
    static double rateHits (int i)
    {
        static const double hits[] = { 0.5, 1.0, 2.0, 4.0 };
        return hits[juce::jlimit (0, 3, i)];
    }

    static float rateBeats (int i)
    {
        static const float beats[] = { 2.0f, 1.0f, 0.5f, 0.25f };
        return beats[juce::jlimit (0, 3, i)];
    }
};
