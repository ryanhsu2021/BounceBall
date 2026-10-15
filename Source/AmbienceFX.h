#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <algorithm>
#include <cmath>

//==============================================================================
// 氛围效果链（固定参数，不给选项）：
//   输入 → 立体声延时(反馈+高频阻尼) → Schroeder 混响(梳状+全通) → 湿干混合
// 目标是 ambient 空间感，湿声给得比较足。
//==============================================================================

//------------------------------------------------------------------------------
// 阻尼延时线：线性插值读取 + 一阶低通（模拟空气/软墙吸高频）
//------------------------------------------------------------------------------
class DelayLine
{
public:
    void prepare (double sampleRate, double maxDelaySeconds)
    {
        const int len = (int) std::ceil (sampleRate * maxDelaySeconds) + 4;
        buffer.assign ((size_t) juce::jmax (4, len), 0.0f);
        writePos = 0;
        lpState = 0.0f;
    }

    void clear()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        lpState = 0.0f;
    }

    // 写入 input，返回 delaySamples 之前的输出（带阻尼）
    //  ⚠️ damping 语义 = "低通保留系数"：damping=0 → 直通（不滤波）；damping→1 → 最重阻尼。
    //     公式 lp = lp*damping + raw*(1-damping) 在 damping==1.0 时退化成
    //     lp = lp（恒为初始值 0）→ **输出永远为 0**。
    //     所以"不想滤波"时必须传 0.0，绝不能传 1.0（曾因此让整个混响静音）。
    //  ⚠️ 曾尝试把 while 回绕 + % 取模换成条件分支做"优化"——
    //     delaySamples 可以是负数或带小数（comb 的 d = combDelay + modSamples，modSamples ∈ ±3），
    //     那时 floor 语义与原地回绕不等价（实测 5000 样本里 826 处输出偏差）。
    //     取模在这条链上每采样仅 26 次，现代 CPU 完全吃得消，**不值得为它冒险**。
    float process (float input, double delaySamples, float damping)
    {
        if (buffer.empty()) return input;

        const int len = (int) buffer.size();
        double readF = (double) writePos - delaySamples;
        while (readF < 0.0) readF += (double) len;

        const int i0 = (int) readF;
        const int i1 = (i0 + 1) % len;
        const float frac = (float) (readF - (double) i0);
        const float raw = buffer[(size_t) i0] * (1.0f - frac) + buffer[(size_t) i1] * frac;

        // damping 越接近 0 → 高频保留越多（0 = 完全直通）
        //  ⚡ damping==0 的一阶低通等价于直通 → 跳过 2 次浮点乘加。
        //     混响里 preDelay + 4 个 allpass 都是 damping=0（占这条链的一半条数），
        //     这个分支对它们是稳定命中，收益实打实。
        if (damping > 0.0f)
            lpState = lpState * damping + raw * (1.0f - damping);
        else
            lpState = raw;

        buffer[(size_t) writePos] = input;
        writePos = (writePos + 1) % len;
        return lpState;
    }

private:
    std::vector<float> buffer;
    int writePos = 0;
    float lpState = 0.0f;
};

//------------------------------------------------------------------------------
// Schroeder/Freeverb 混响：8 并联梳状（带反馈阻尼 + LFO 调制） + 4 串联全通
//   相比 4 梳状版本：密度更高、尾巴更长更顺滑，且加了微调制避免金属味
//------------------------------------------------------------------------------
class Reverb
{
public:
    void prepare (double sr, double tuneOffsetMs, double feedback, float damp, double lfoRateHz)
    {
        sampleRate = sr;
        combFeedback  = feedback;
        combFeedbackF = (float) feedback;
        combDamp = damp;
        lfoRate  = lfoRateHz;

        // Freeverb 经典 8 梳状延时长度（44.1kHz 基准），按采样率缩放 + 左右偏移
        static const int combTune[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        // 4 个串联全通（相位扩散，越多越"漫"）
        static const int apTune[4]   = { 556, 441, 341, 225 };

        const double scale = sr / 44100.0;
        const double offset = tuneOffsetMs * 0.001 * sr;

        for (int i = 0; i < 8; ++i)
        {
            combDelay[i] = combTune[i] * scale + offset;
            comb[i].prepare (sr, combDelay[i] / sr + 0.05);
        }
        for (int i = 0; i < 4; ++i)
        {
            apDelay[i] = apTune[i] * scale + offset;
            ap[i].prepare (sr, apDelay[i] / sr + 0.05);
        }

        // 预延时：制造"距离感"（20ms 会明显拉远声源）
        preDelay.prepare (sr, 0.12);
        preDelaySamples = 0.024 * sr;

        // LFO：每个梳状滤波器用不同相位，让延时长度缓慢起伏 → 尾巴"活"起来
        lfoPhase = 0.0;
        for (int i = 0; i < 8; ++i)
            lfoPhaseOffset[i] = (double) i * 0.6180339887;   // 黄金比例错开
        lfoCounter = 0;
        updateMod();

        clear();
    }

    void clear()
    {
        for (int i = 0; i < 8; ++i) { comb[i].clear(); combLp[i] = 0.0f; }
        for (int i = 0; i < 4; ++i) ap[i].clear();
        preDelay.clear();
    }

    float process (float input)
    {
        // 预延时：不滤波（damping 必须传 0 = 直通；传 1 会让输出恒为 0）
        const float pre = preDelay.process (input, preDelaySamples, 0.0f);

        // LFO 相位推进（0.06 Hz ~ 0.2 Hz 量级，缓慢起伏）
        //   ⚠️ 走"控制速率"：每 64 采样更新一次就够（0.1Hz 的信号没必要每采样算
        //      8 次 sin）。原来每采样 16 次 sin（左右各 8 路梳状），是混响里最贵的一笔。
        if ((lfoCounter++ & 63) == 0)
        {
            lfoPhase += 64.0 * lfoRate / sampleRate;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            updateMod();
        }

        // ---- 8 个并联梳状滤波器（反馈路径带低通 + 微调制）----
        float sum = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            const double d = combDelay[i] + modSamples[i];   // ±3 采样
            const float fb = comb[i].process (pre + combLp[i] * combFeedbackF, d, combDamp);
            combLp[i] = fb;
            sum += fb;
        }
        float out = sum * 0.125f;   // 8 路平均

        // ---- 4 个串联全通（扩散）----
        //  damping 传 0 = 不滤波（传 1 会恒输出 0，整条混响就哑了）
        for (int i = 0; i < 4; ++i)
            out = ap[i].process (out, apDelay[i], 0.0f);

        return out;
    }

private:
    // 8 路梳状各自相位错开（黄金比例），一次算出本控制周期的调制量（采样数）
    void updateMod()
    {
        for (int i = 0; i < 8; ++i)
            modSamples[i] = std::sin ((lfoPhase + lfoPhaseOffset[i])
                                      * 2.0 * juce::MathConstants<double>::pi)
                            * modDepthSamples;
    }

    double sampleRate = 44100.0;
    double combFeedback = 0.88;
    float  combFeedbackF = 0.88f;
    float  combDamp = 0.26f;

    double lfoRate = 0.13;
    double lfoPhase = 0.0;
    double lfoPhaseOffset[8] {};
    double modSamples[8] {};      // 当前控制周期各梳状的调制量（采样）
    int    lfoCounter = 0;        // 控制速率分频（每 64 采样刷新一次）
    static constexpr double modDepthSamples = 3.0;

    DelayLine comb[8], ap[4], preDelay;
    double combDelay[8] {};
    double apDelay[4] {};
    double preDelaySamples = 0.0;
    float  combLp[8] {};
};

//------------------------------------------------------------------------------
// 完整氛围链：乒乓延时（跟随 BPM）→ 立体声混响 → 湿干混合
//   · 延时时间按 BPM 换算（左 = 附点八分，右 = 八分），改 BPM 会平滑滑音过渡
//   · 左右交叉反馈（ping-pong）→ 回声在左右间往返，空间更宽敞
//   · 混响用 8 梳状 + 4 全通 + LFO 调制，湿声给得足（ambient 取向）
//------------------------------------------------------------------------------
class AmbienceFX
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        delayL.prepare (sampleRate, 3.0);
        delayR.prepare (sampleRate, 3.0);

        // 混响：左右不同偏置 + 不同反馈/阻尼/LFO 速率 → 立体声更宽、更自然
        //  反馈 0.92 量级 → RT60 ≈ 2s（ambient 长尾，但仍在稳定范围内）
        reverbL.prepare (sampleRate, 0.0,  0.925, 0.22f, 0.11);
        reverbR.prepare (sampleRate, 0.9,  0.915, 0.28f, 0.17);

        updateDelayTimes (currentBpm);
        // 启动时直接落到目标延时（不做渐入滑移，避免开头"甩"一下）
        delaySamplesL = targetDelayL;
        delaySamplesR = targetDelayR;

        // 尾巴长度：静音输入持续这么久后，认为延时/混响已经散尽，可以整个旁路
        tailSamples = (int) (3.0 * sampleRate);
        idleSamples = tailSamples;   // 一开始就是"静默"状态

        clear();
    }

    // 尾巴是否已散尽（供 processBlock 判断是否整条链都能跳过）
    bool isIdle() const { return idleSamples >= tailSamples; }

    void clear()
    {
        delayL.clear(); delayR.clear();
        reverbL.clear(); reverbR.clear();
        fbL = fbR = 0.0f;
    }

    // 由 processBlock 每块调用；内部平滑过渡，不会"啪"地变长变短
    void setBpm (double bpm)
    {
        currentBpm = juce::jlimit (20.0, 300.0, bpm);
        updateDelayTimes (currentBpm);
    }

    void process (float& l, float& r)
    {
        // ---- 静音旁路：这条链（2 条延时 + 2 组 8 梳状/4 全通混响）是整个插件
        //      最贵的一段，每采样要读 30 多次滤波器。没人弹的时候没必要一直算：
        //      输入为 0 且尾巴已散尽 → 直接跳过，输出保持 0。
        const bool silent = std::fabs (l) < 1.0e-7f && std::fabs (r) < 1.0e-7f;
        if (silent)
        {
            if (idleSamples >= tailSamples) { l = 0.0f; r = 0.0f; return; }
            ++idleSamples;
        }
        else
        {
            // 从"彻底静默"恢复 → 先清空残留缓冲，否则会突然弹出上次的尾巴
            if (idleSamples >= tailSamples)
                clear();
            idleSamples = 0;
        }

        // ---- 延时时间平滑（每采样一点点靠近目标，避免改 BPM 时爆音）----
        delaySamplesL += (targetDelayL - delaySamplesL) * 0.00004;
        delaySamplesR += (targetDelayR - delaySamplesR) * 0.00004;

        // ---- 乒乓延时：左声道的回声喂给右声道，反之亦然 ----
        const float dl = delayL.process (l + fbR * delayFeedback, delaySamplesL, 0.42f);
        const float dr = delayR.process (r + fbL * delayFeedback, delaySamplesR, 0.42f);
        fbL = dl;
        fbR = dr;

        const float mixL = l + dl * delayMix;
        const float mixR = r + dr * delayMix;

        // ---- 混响 ----
        const float rvL = reverbL.process (mixL);
        const float rvR = reverbR.process (mixR);

        // ---- 湿干混合 ----
        l = mixL * (1.0f - reverbMix) + rvL * reverbGain * reverbMix;
        r = mixR * (1.0f - reverbMix) + rvR * reverbGain * reverbMix;

        // ---- 总线软限幅（防破音的最后一道保险）----
        //  多个 voice 同时响 + 混响长尾叠加时，硬峰会超 ±1.0。
        //  用"平滑 tanh 式饱和"代替硬削：软拐点从 0.72 开始压缩，
        //  1.0 以内基本透明，超过后渐进饱和，绝不会出现数字硬削的刺耳破音。
        l = softClip (l);
        r = softClip (r);
    }

    // 重置（清缓冲 + 清限幅器状态）
    void reset()
    {
        clear();
        idleSamples = tailSamples;
        limEnvL = limEnvR = 0.0f;
    }

private:
    //=== 软限幅 ============================================================
    //  |x| < knee(0.72) → 线性直通（透明）
    //  |x| ≥ knee      → 用 tanh 曲线渐进饱和，输出始终 < 1.0
    //  好处：不会像硬削那样产生大量高次谐波（那才是"破音/炸"的听感来源），
    //        只会在极响时把峰值柔和地压住。
    static float softClip (float x)
    {
        constexpr float knee = 0.72f;
        constexpr float range = 1.0f - knee;   // 0.28
        const float a = std::fabs (x);
        if (a <= knee) return x;

        // 超出部分映射到 tanh 曲线，渐近上限 1.0
        const float over = (a - knee) / range;             // ≥ 0
        const float shaped = knee + range * std::tanh (over);
        return x < 0.0f ? -shaped : shaped;
    }

    // 按 BPM 换算延时采样数：左 = 附点八分（0.75 拍），右 = 八分（0.5 拍）
    void updateDelayTimes (double bpm)
    {
        const double beatSec = 60.0 / juce::jmax (20.0, bpm);
        targetDelayL = beatSec * 0.75 * sr;
        targetDelayR = beatSec * 0.50 * sr;
    }

    double sr = 44100.0;
    double currentBpm = 120.0;

    DelayLine delayL, delayR;
    Reverb reverbL, reverbR;

    double delaySamplesL = 0.0, delaySamplesR = 0.0;
    double targetDelayL  = 0.0, targetDelayR  = 0.0;
    float  fbL = 0.0f, fbR = 0.0f;

    // 静音旁路用的计数：连续静音超过 tailSamples 就认为尾巴散尽
    int idleSamples = 0;
    int tailSamples = 132300;

    // 软限幅器状态（当前为无状态实现，保留字段以便将来换成有包络的限幅）
    float limEnvL = 0.0f, limEnvR = 0.0f;

    // 固定参数（氛围为主，湿声给足）
    static constexpr float delayFeedback = 0.62f;  // 反馈：更多次、更长的回声链
    static constexpr float delayMix      = 0.48f;  // 延时湿量
    static constexpr float reverbMix     = 0.58f;  // 混响湿量（大）
    // 混响补偿增益：8 梳状平均后脉冲能量 ≈ 0.45（L 路），
    //  原 2.6 是"混响完全没响"时期的补偿值 —— 修好 preDelay/allpass 后
    //  实测 2.6 会让 2 个音就过 1.0（削波），因此收到 1.0（保持原有湿声比例感）
    static constexpr float reverbGain    = 1.0f;
};
