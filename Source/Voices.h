#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

//==============================================================================
// 预览音色表：一套能互相配合的氛围音色（按音区从低到高排列）
// 全部为纯算法合成，无外部采样。
//
// 为什么这几个音色能"配合"成好听的氛围合奏：
//   1) 频谱分工 —— 每个音色占不同的音区，同时发声不会互相掩蔽：
//        Sub / Pad / Organ / Strings / Choir   低频与铺底（只发低次泛音）
//        Rhodes / Pluck / Marimba / Kalimba    中频落点（弹球碰撞的"点"）
//        Bell / Glass / Music Box / Celesta    高频闪耀（空气感）
//   2) 包络分工 —— 持续型（Sub/Pad/Organ/Strings/Choir）铺底长音；
//      打击型（其余）负责落点节奏
//   3) 声像分工 —— 各音色略微左右分开（pan），堆叠时能分辨出层次
//   4) 与"和弦档位分层"配合 —— Root/3rd/5th/7th 各占一个音区（见 ChordEngine），
//      再用本表的音色按档位分派，就是一套完整的氛围合奏（见 UI 的 Ensemble 按钮）
//==============================================================================
class Timbre
{
public:
    static constexpr int num = 13;

    static const char* name (int i)
    {
        static const char* names[num] = {
            "Sub",        // 低频根基：纯正弦 + 极轻二次谐波
            "Pad",        // 铺底长音：柔和低次泛音 + 双振荡器失谐
            "Organ",      // 管风琴：奇数+偶数泛音堆叠，持续稳定
            "Strings",    // 弦乐：锯齿式泛音滚降 + 双振荡器，弓弦感
            "Choir",      // 中音人声垫：人声式泛音轮廓，慢起音
            "Rhodes",     // 电钢：暖基音 + 明亮 tine（4 倍频）
            "Pluck",      // 拨弦：明亮起音 + 快速滚降
            "Marimba",    // 马林巴：木质打击，第 4 泛音突出
            "Kalimba",    // 卡林巴：木质基音 + 二三次簧片感
            "Bell",       // 钟：非谐泛音 + 长尾
            "Glass",      // 玻璃/风铃：高次泛音，通透空气感
            "Music Box",  // 音乐盒：针状起音 + 高位非谐亮片，童话感
            "Celesta"     // 钢片琴：亮而短的分音，晶体感
        };
        return names[juce::jlimit (0, num - 1, i)];
    }

    // 在合奏里担任的角色（UI 提示用）
    static const char* role (int i)
    {
        static const char* roles[num] = {
            "low foundation",
            "sustained bed",
            "sustained drawbar",
            "bowed sustain",
            "mid texture",
            "warm stab",
            "melodic point",
            "wooden point",
            "wooden point II",
            "sparkle",
            "air / shimmer",
            "toy sparkle",
            "crystal point"
        };
        return roles[juce::jlimit (0, num - 1, i)];
    }

    // 起音时间（秒）——持续型音色慢起音，打击型几乎瞬时
    static float attack (int i)
    {
        static const float a[num] = {
            0.020f,   // Sub（略慢，避免低频"砰"）
            0.350f,   // Pad
            0.030f,   // Organ（管风琴起音干脆）
            0.450f,   // Strings（弓弦渐入）
            0.550f,   // Choir（最慢，像人声渐入）
            0.002f,   // Rhodes
            0.001f,   // Pluck
            0.001f,   // Marimba
            0.001f,   // Kalimba
            0.001f,   // Bell
            0.001f,   // Glass
            0.001f,   // Music Box
            0.001f    // Celesta
        };
        return a[juce::jlimit (0, num - 1, i)];
    }

    // 衰减到千分之一的时长（秒）——决定尾巴长短
    static float decay (int i)
    {
        static const float d[num] = {
            2.20f,    // Sub（低音铺底）
            3.40f,    // Pad（最长）
            3.00f,    // Organ
            3.20f,    // Strings
            3.00f,    // Choir
            1.60f,    // Rhodes
            0.90f,    // Pluck
            0.75f,    // Marimba（短促木质）
            0.60f,    // Kalimba（最短）
            3.20f,    // Bell
            2.20f,    // Glass
            1.50f,    // Music Box（清脆，尾音比 Bell 短）
            1.80f     // Celesta
        };
        return d[juce::jlimit (0, num - 1, i)];
    }

    // 持续段电平：> 0 表示"持续型音色"，需要靠 hold 计时自动释放
    static float sustain (int i)
    {
        static const float s[num] = {
            0.40f,    // Sub
            0.55f,    // Pad
            0.50f,    // Organ
            0.52f,    // Strings
            0.48f,    // Choir
            0.0f,     // Rhodes
            0.0f,     // Pluck
            0.0f,     // Marimba
            0.0f,     // Kalimba
            0.0f,     // Bell
            0.0f,     // Glass
            0.0f,     // Music Box
            0.0f      // Celesta
        };
        return s[juce::jlimit (0, num - 1, i)];
    }

    // 持续型音色进入 sustain 后的保持上限（秒）——没有 noteOff 来源，到时自动 release
    static float hold (int i)
    {
        static const float h[num] = {
            1.6f,     // Sub
            1.2f,     // Pad
            1.5f,     // Organ
            1.5f,     // Strings
            1.4f,     // Choir
            0.0f, 0.0f, 0.0f, 0.0f,   // Rhodes / Pluck / Marimba / Kalimba
            0.0f, 0.0f, 0.0f, 0.0f    // Bell / Glass / Music Box / Celesta
        };
        return h[juce::jlimit (0, num - 1, i)];
    }

    // 并发振荡器数（2 = 轻微失谐，制造厚度与立体声宽度）
    static int unison (int i)
    {
        static const int u[num] = {
            1,        // Sub
            2,        // Pad
            1,        // Organ
            2,        // Strings
            2,        // Choir
            1, 1, 1, 1,   // Rhodes / Pluck / Marimba / Kalimba
            1, 1, 1, 1    // Bell / Glass / Music Box / Celesta
        };
        return u[juce::jlimit (0, num - 1, i)];
    }

    // 失谐量（音分）——仅 unison == 2 时生效
    static float detuneCents (int i)
    {
        static const float c[num] = {
            0.0f,     // Sub
            8.0f,     // Pad
            0.0f,     // Organ
            9.0f,     // Strings
            11.0f,    // Choir
            0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f
        };
        return c[juce::jlimit (0, num - 1, i)];
    }

    // 声像摆放（-1 左 .. +1 右）——只做轻微偏移，让音色在空间上分开、减少互相掩蔽
    static float pan (int i)
    {
        static const float p[num] = {
            -0.06f,   // Sub 几乎居中（低频居中更稳）
            -0.16f,   // Pad 略左
            -0.10f,   // Organ 略左
            -0.05f,   // Strings 微左
             0.18f,   // Choir 略右
             0.10f,   // Rhodes 略右
            -0.13f,   // Pluck 略左
             0.14f,   // Marimba 略右
             0.20f,   // Kalimba 右
            -0.19f,   // Bell 左
             0.24f,   // Glass 右（最宽）
            -0.26f,   // Music Box 左（与 Bell 分开）
             0.12f    // Celesta 略右
        };
        return p[juce::jlimit (0, num - 1, i)];
    }

    // 泛音结构：返回第 h 个泛音的振幅（h 从 1 起）
    // 非谐音（Bell/Glass）用带微小偏移的频率系数，故另给 freqMul
    static float harmonicAmp (int i, int h)
    {
        switch (juce::jlimit (0, num - 1, i))
        {
            case 0: // Sub：几乎纯正弦，只加一点二次谐波（小音箱也听得到低音）
                return h == 1 ? 1.0f : (h == 2 ? 0.18f : 0.0f);

            case 1: // Pad：只留低次泛音，滚降快 → 不抢中频
            {
                static const float p[5] = { 1.0f, 0.38f, 0.15f, 0.06f, 0.02f };
                return h <= 5 ? p[h - 1] : 0.0f;
            }

            case 2: // Organ：奇偶都有的"拉杆"堆叠，持续稳定、有体积感
            {
                static const float o[8] = { 1.0f, 0.55f, 0.40f, 0.26f, 0.0f, 0.16f, 0.0f, 0.10f };
                return h <= 8 ? o[h - 1] : 0.0f;
            }

            case 3: // Strings：锯齿式 1/h 滚降（弓弦摩擦感）
            {
                static const float s[8] = { 1.0f, 0.62f, 0.44f, 0.33f, 0.26f, 0.21f, 0.17f, 0.14f };
                return h <= 8 ? s[h - 1] : 0.0f;
            }

            case 4: // Choir：人声"啊"的轮廓 —— 低次饱满、中间凹陷、上方有气息峰
            {
                static const float c[8] = { 1.0f, 0.85f, 0.75f, 0.55f, 0.30f, 0.22f, 0.35f, 0.42f };
                return h <= 8 ? c[h - 1] : 0.0f;
            }

            case 5: // Rhodes：基音 + 明亮 tine（4 倍频）+ 少量 2/6 次
                return h == 1 ? 1.0f
                     : h == 2 ? 0.35f
                     : h == 4 ? 0.42f
                     : h == 6 ? 0.12f
                     : 0.0f;

            case 6: // Pluck：明亮起音后快速滚降（比 1/h 更柔和，适合氛围）
            {
                static const float pl[8] = { 1.0f, 0.46f, 0.28f, 0.19f, 0.14f, 0.10f, 0.08f, 0.06f };
                return h <= 8 ? pl[h - 1] : 0.0f;
            }

            case 7: // Marimba：木质打击，第 4 泛音（约 4 倍频）是标志性"木味"
            {
                static const float m[5] = { 1.0f, 0.32f, 0.20f, 0.28f, 0.08f };
                return h <= 5 ? m[h - 1] : 0.0f;
            }

            case 8: // Kalimba：木质基音 + 二三次（金属簧片感）
            {
                static const float k[5] = { 1.0f, 0.30f, 0.34f, 0.10f, 0.06f };
                return h <= 5 ? k[h - 1] : 0.0f;
            }

            case 9: // Bell：非谐钟音（1 + 2.76x）
                //  ⚠️ 用户反馈"太响又太尖" → 两处调整：
                //    ① 倍音振幅从 1 / 0.62 / 0.28 收到 1 / 0.42 / 0.14
                //       —— 基音占比提高，钟的"亮片感"减弱，不再抢耳朵
                //    ② 2.76x 是最扎耳的那个分音（纯非谐、落在 2~3 kHz 的敏感带），
                //       振幅大压一档；3.26x 只留一点点，做"泛音尾巴"的质感
                //    ③ 高频能量少了，gain 表同步下调（否则会觉得"变闷但还吵"）
                return h == 1 ? 1.0f
                     : h == 2 ? 0.42f
                     : h == 3 ? 0.14f
                     : 0.0f;

            case 10: // Glass：高次泛音，通透
            {
                static const float g[6] = { 1.0f, 0.30f, 0.34f, 0.12f, 0.16f, 0.06f };
                return h <= 6 ? g[h - 1] : 0.0f;
            }

            case 11: // Music Box：音乐盒 —— 针状起音，高位非谐亮片（3.4x / 10.2x）
                return h == 1 ? 1.0f
                     : h == 2 ? 0.44f
                     : h == 3 ? 0.14f
                     : 0.0f;

            case 12: // Celesta：钢片琴 —— 亮而短的分音（约 4x / 9.2x），晶体感
                return h == 1 ? 1.0f
                     : h == 2 ? 0.32f
                     : h == 3 ? 0.12f
                     : 0.0f;

            default:
                return 0.0f;
        }
    }

    // 第 h 个泛音的频率倍数（非谐音色用）
    static float freqMul (int i, int h)
    {
        const int t = juce::jlimit (0, num - 1, i);
        if (t == 9  && h == 2) return 2.76f;   // Bell 的非谐泛音
        if (t == 10 && h == 3) return 2.65f;   // Glass 的非谐泛音
        if (t == 11 && h == 2) return 3.40f;   // Music Box 亮片
        if (t == 11 && h == 3) return 10.2f;   // Music Box 高位闪点
        if (t == 12 && h == 2) return 4.00f;   // Celesta 亮分音
        if (t == 12 && h == 3) return 9.20f;   // Celesta 高位闪点
        return (float) h;
    }

    static int maxPartials() { return 8; }

    // 该音色的总输出增益
    // 按"各泛音振幅平方和"归一化，让不同音色的响度接近（避免切换时忽大忽小）；
    // Sub 略高一点，补偿低频在小音量下的听感损失
    static float gain (int i)
    {
        static const float g[num] = {
            0.24f,    // Sub（刻意略高，补低频听感）
            0.20f,    // Pad
            0.174f,   // Organ（泛音多，等响度归一）
            0.160f,   // Strings（泛音最多，增益最低）
            0.12f,    // Choir（泛音多，增益要低）
            0.19f,    // Rhodes
            0.19f,    // Pluck
            0.197f,   // Marimba
            0.20f,    // Kalimba
            0.135f,   // Bell（用户反馈偏响：从 0.18 下调；配合倍音振幅同步收紧）
            0.19f,    // Glass
            0.175f,   // Music Box（亮片音色，压一点免得刺）
            0.175f    // Celesta
        };
        return g[juce::jlimit (0, num - 1, i)];
    }
};

//==============================================================================
// 一个发声单元：支持多泛音 + ADSR 包络 + 立体声微失谐
//==============================================================================
struct Voice
{
    static constexpr int maxPartials = 8;
    static constexpr int maxUnison   = 2;   // Pad 用双振荡器轻微失谐

    bool  active = false;
    int   timbre = 0;
    int   midiNote = -1;      // 发声的音高（用于"同音复用"，避免持续型音色层层堆叠）
    int   ballIndex = -1;     // 这个 voice 属于哪个球（查 Mute/Solo 闸门用；-1 = 不属于任何球）
    float velocity = 1.0f;

    double phase[maxPartials][maxUnison] {};
    double inc  [maxPartials][maxUnison] {};
    float  amp  [maxPartials] {};
    int    numPartials = 1;

    // 渲染期热路径用的"预计算结果"（prepare 时一次算好，render 里不再查表）：
    //   只遍历真正发声的泛音（activeH），声像与增益也提前展开成左右系数
    int   activeH[maxPartials] {};   // 振幅非零的泛音下标
    int   numActive   = 1;           // activeH 的有效长度
    int   uniCount    = 1;           // Timbre::unison (timbre)
    float panL = 1.0f, panR = 1.0f;  // Timbre::pan (timbre) 展开
    float outGain = 0.2f;            // Timbre::gain (timbre)

    // 包络状态
    enum class Stage { attack, decay, sustain, release };
    Stage stage = Stage::attack;
    float env = 0.0f;
    float muteGain = 1.0f;     // Mute / Solo 的总线增益（0..1，每块线性平滑，绝不重起包络）

    // 音频线程每块刷新（见 BounceAudioProcessor::updateVoiceGates）：
    //   这个 voice 此刻"应该"被衰减到多少（1 = 出声，0 = 静音）
    //   muteGain 再朝它按 gateStep 逐采样线性逼近 —— 快但无爆音
    float gateTarget = 1.0f;
    float gateStep   = 0.015f;   // 每采样最大变化量（0.015 → 约 1.4ms 走完全程 @48k）
    float attackInc = 1.0f;    // 每样本 attack 增量
    float decayMul  = 0.9999f; // 每样本衰减乘数
    float releaseMul = 0.9999f;
    float sustainLevel = 0.0f;

    // 持续型音色（Pad）的最长保持：进入 sustain 后计时，到时自动 release
    double holdCounter = 0.0;
    double holdLimit   = 0.0;

    // decay 阶段用"样本计数"判定结束，**不能用浮点比较**：
    // env≈0.55 处 float32 的 ULP(6.6e-8) 比每步减少量(3e-8)还大，
    // (env - sustainLevel) 会被舍入冻结在 5.4e-4，永远降不到阈值以下
    // → decay 永不结束 → 永远进不了 sustain → Pad 永远不停（2026-10-14 实测）
    double decayCounter = 0.0;
    double decaySamples = 1.0;

    void prepare (double sampleRate, int timbreIndex, int noteNumber, float vel)
    {
        timbre = juce::jlimit (0, Timbre::num - 1, timbreIndex);
        midiNote = noteNumber;
        velocity = juce::jlimit (0.0f, 1.0f, vel);

        const double freq = 440.0 * std::pow (2.0, (noteNumber - 69) / 12.0);

        // 声部归一化：unison 越多，单个振荡器的振幅越低，
        // 这样无论用几个振荡器，总响度都一致（否则 Pad/Choir 会明显更响）
        const int   nUni    = juce::jlimit (1, maxUnison, Timbre::unison (timbre));
        const float uniNorm = 1.0f / (float) nUni;
        const double detune = std::pow (2.0, (double) Timbre::detuneCents (timbre) / 1200.0);

        // 统计有效泛音数（只记振幅非零的，render 里就不必再扫全 8 个槽位）
        numPartials = 0;
        numActive   = 0;
        for (int h = 1; h <= Timbre::maxPartials(); ++h)
        {
            const float a = Timbre::harmonicAmp (timbre, h);
            amp[h - 1] = a * uniNorm;
            if (a > 0.0f) { ++numPartials; activeH[numActive++] = h - 1; }

            // 超过奈奎斯特频率的泛音直接跳过（避免混叠）
            const double f = freq * Timbre::freqMul (timbre, h);
            const bool audible = f < sampleRate * 0.45;

            for (int u = 0; u < maxUnison; ++u)
            {
                phase[h - 1][u] = 0.0;
                if (a > 0.0f && audible && u < nUni)
                {
                    // 第二个振荡器轻微失谐，制造厚度与立体声宽度
                    const double d = (u == 1) ? detune : 1.0;
                    inc[h - 1][u] = juce::MathConstants<double>::twoPi * f * d / sampleRate;
                }
                else
                    inc[h - 1][u] = 0.0;
            }
        }
        if (numPartials <= 0) numPartials = 1;
        if (numActive   <= 0) { numActive = 1; activeH[0] = 0; }

        //=== 渲染热路径预计算：声像 / 增益 / 并发振荡器数 ===
        //  原来这些每采样都要查一次表（3 次查表 × 每采样 × 每个 voice），
        //  现在 prepare 时算好存起来，render 里直接用
        uniCount = juce::jlimit (1, maxUnison, Timbre::unison (timbre));
        const float pan = Timbre::pan (timbre);
        panL = pan > 0.0f ? 1.0f - pan : 1.0f;
        panR = pan < 0.0f ? 1.0f + pan : 1.0f;
        outGain = Timbre::gain (timbre);

        // 包络
        const float atk = Timbre::attack (timbre);
        attackInc = atk > 0.0f ? (float) (1.0 / (atk * sampleRate)) : 1.0f;

        const float dec = Timbre::decay (timbre);
        // 衰减到千分之一：exp(-6.9 / n)
        decayMul = (float) std::exp (-6.9 / juce::jmax (1.0, (double) dec * sampleRate));
        releaseMul = (float) std::exp (-6.9 / juce::jmax (1.0, 0.30 * sampleRate));

        // decay 阶段时长（样本数）——持续型音色靠它判断何时进入 sustain
        decaySamples = juce::jmax (1.0, (double) dec * sampleRate);
        decayCounter = 0.0;

        // 持续型音色（Sub/Pad/Choir）有 sustain 段；其余为打击型（sustain 0，靠衰减自然消失）
        sustainLevel = Timbre::sustain (timbre);

        // 持续型音色保持上限（没有 noteOff 来源，到时自动 release）
        // 弹球碰撞很密，保持太久会层层叠到糊
        holdCounter = 0.0;
        holdLimit   = (double) Timbre::hold (timbre);

        stage = Stage::attack;
        env = 0.0f;
        active = true;

        // Mute / Solo 闸门：重新起音就回到"完全出声"，
        //  ！必须重置：voice 会被复用（抢包络最低的那个），
        //    上一个主人若处于静音，muteGain 留着 0 会让新音永远不响，
        //    直到下一次 updateVoiceGates 才发现 —— 中间会漏一个音。
        //    重置后若新主人此刻确实是静音状态，updateVoiceGates 当块就会把它压到 0。
        muteGain   = 1.0f;
        gateTarget = 1.0f;
    }

    // 返回该样本的立体声值
    void render (float& outL, float& outR, double sampleRate, bool releasing)
    {
        if (! active) { outL = outR = 0.0f; return; }

        // ---- 包络推进 ----
        // 外部请求释放（传输停止）→ 从任何阶段立即进入 release
        if (releasing && stage != Stage::release)
            stage = Stage::release;

        switch (stage)
        {
            case Stage::attack:
                env += attackInc;
                if (env >= 1.0f) { env = 1.0f; stage = Stage::decay; }
                break;

            case Stage::decay:
                if (sustainLevel > 0.0f)
                {
                    env = sustainLevel + (env - sustainLevel) * decayMul;
                    decayCounter += 1.0;
                    // 用样本计数判定（浮点差值会被 ULP 冻结，见成员注释）
                    if (decayCounter >= decaySamples)
                    {
                        env = sustainLevel;
                        stage = Stage::sustain;
                    }
                }
                else
                {
                    env *= decayMul;
                    if (env < 1e-4f) { active = false; outL = outR = 0.0f; return; }
                }
                break;

            case Stage::sustain:
                holdCounter += 1.0 / sampleRate;
                if (releasing || holdCounter >= holdLimit)
                    stage = Stage::release;
                break;

            case Stage::release:
                env *= releaseMul;
                if (env < 1e-4f) { active = false; outL = outR = 0.0f; return; }
                break;
        }

        // ---- Mute / Solo 闸门推进 ----
        //  朝 gateTarget 线性逼近（每次最多移 gateStep）。小步长 → 无爆音；
        //  相对"每块直接跳变"的好处是可以在块内逐样本走完，听感是干净的
        //  极短淡入淡出（约 1.5ms）。取消静音时从当前电平接着往上走，
        //  **不碰包络** —— 所以不会重新起音。
        if (muteGain < gateTarget)      { muteGain += gateStep; if (muteGain > gateTarget) muteGain = gateTarget; }
        else if (muteGain > gateTarget) { muteGain -= gateStep; if (muteGain < gateTarget) muteGain = gateTarget; }

        // ---- 振荡器求和（只走"振幅非零"的泛音，见 prepare 里的 activeH）----
        float l = 0.0f, r = 0.0f;
        const int nUni = uniCount;

        for (int k = 0; k < numActive; ++k)
        {
            const int   h = activeH[k];
            const float a = amp[h];
            if (a <= 0.0f) continue;

            for (int u = 0; u < nUni; ++u)
            {
                if (inc[h][u] == 0.0) continue;
                const float s = (float) std::sin (phase[h][u]) * a;

                // 单振荡器 → 居中；双振荡器 → 左右分开，制造立体声宽度
                if (nUni == 1)     { l += s;         r += s;         }
                else if (u == 0)   { l += s;         r += s * 0.82f; }
                else               { l += s * 0.82f; r += s;         }

                phase[h][u] += inc[h][u];
                if (phase[h][u] > juce::MathConstants<double>::twoPi)
                    phase[h][u] -= juce::MathConstants<double>::twoPi;
            }
        }

        // ---- 声像摆放：各音色轻微左右分开，多个音色堆叠时层次更清楚 ----
        //  panL / panR 在 prepare 里算好，这里直接用
        //  muteGain：Mute / Solo 的总线闸门（只在输出端缩放，包络照常走完，
        //  所以取消静音时是"从当前电平无缝淡回"，不会重新起音 —— 见 renderVoices）
        const float g = outGain * env * velocity * muteGain;
        outL = l * g * panL;
        outR = r * g * panR;
    }
};
