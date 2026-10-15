#pragma once
#include <juce_core/juce_core.h>

// 调式 + 和弦进行引擎
// 内部自己维护"当前小节"，由 UI 定时器按拍/秒推进；
// 这样手动模式（无 DAW 播放头）也能正常换和弦。
class ChordEngine
{
public:
    //=== 调式表 ===
    //  前 6 个是西方教会调式（Ionian=自然大调 / Aeolian=自然小调）
    //  后 4 个是氛围音乐常用的扩展调式：
    //    Harmonic Minor   和声小调   —— 神秘、东方/中东色彩（第 7 级升高）
    //    Melodic Minor    旋律小调   —— 爵士式优雅的忧伤（6、7 级升高）
    //    Phrygian Dom.    弗里吉亚属 —— 异域张力（第 3 级升高成大三度）
    //    Lydian Dominant  利底亚属   —— 属七 + #11，电影配乐空灵色彩
    //  ⚠️ 未收录：
    //    Locrian / Lydian b2 —— 含减五度，无法常规三度叠置，会产出
    //                            2 半音等非三度音程（实测确认），故不收录
    //    五声类              —— 音数不足 7，scaleSteps 索引会越界
    static constexpr int numScales = 10;
    static const char* scaleName (int i)
    {
        static const char* names[numScales] = {
            "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian",
            "Harmonic Minor", "Melodic Minor", "Phrygian Dominant", "Lydian Dominant"
        };
        return names[juce::jlimit (0, numScales - 1, i)];
    }
    static const int* scaleSteps (int i)
    {
        static const int steps[numScales][7] = {
            { 0, 2, 4, 5, 7, 9, 11 },  // Ionian（自然大调）
            { 0, 2, 3, 5, 7, 9, 10 },  // Dorian
            { 0, 1, 3, 5, 7, 8, 10 },  // Phrygian
            { 0, 2, 4, 6, 7, 9, 11 },  // Lydian
            { 0, 2, 4, 5, 7, 9, 10 },  // Mixolydian
            { 0, 2, 3, 5, 7, 8, 10 },  // Aeolian（自然小调）
            { 0, 2, 3, 5, 7, 8, 11 },  // Harmonic Minor（7 级升高）
            { 0, 2, 3, 5, 7, 9, 11 },  // Melodic Minor（6、7 级升高）
            { 0, 1, 4, 5, 7, 8, 10 },  // Phrygian Dominant（3 级升高）
            { 0, 2, 4, 6, 7, 9, 10 }   // Lydian Dominant（#4 + b7）
        };
        return steps[juce::jlimit (0, numScales - 1, i)];
    }

    //=== 和弦进行表 ===
    // 前 6 个是三和弦基础进行，中间 6 个是氛围音乐常用的七和弦/延伸和弦进行，
    // 最后 6 个是配合新增调式（和声小调 / 旋律小调 / 弗里吉亚属 / 利底亚降二）的色彩进行
    //  ⚠️ 内部索引 0..11 是旧工程存档用的，新增只能追加到末尾
    static constexpr int numProgs = 18;
    static const char* progName (int i)
    {
        static const char* names[numProgs] = {
            "I-V-vi-IV",  "vi-IV-I-V",  "i-VI-III-VII", "i-iv-VI-V",
            "I-IV-vi-V",  "I-vi-ii-V",
            "Imaj7-vi7-IV7-V7",  "i7-iv7-VII7-III7", "i9-VI-VII-i", "Imaj9-IV-vi-V",
            "vi7-IV-V-I",  "i7-VI7-III7-VII7",
            // --- 扩展调式色彩进行 ---
            "i-VII-VI-V7",     // 和声小调经典：自然小调 + 属七（7 级升高制造导音张力）
            "i-iv-V7-i",       // 和声小调终止式：最强的小调收束
            "i-ii0-V7-i",      // 和声小调 ii0-V-i（爵士小调终止，b5 色彩）
            "i7-IV7-III7-VI7", // 旋律小调：明亮的 dorian 式上四度链
            "i-bII-bVII-i",    // 弗里吉亚属：bII 大三度异域张力（西班牙/中东）
            "I7#11-IV-II-I"    // 利底亚属：#11 空灵 + 属七色彩
        };
        return names[juce::jlimit (0, numProgs - 1, i)];
    }
    static const int* progDegrees (int i)
    {
        static const int degs[numProgs][4] = {
            { 0, 4, 5, 3 },   // I-V-vi-IV
            { 5, 3, 0, 4 },   // vi-IV-I-V
            { 0, 5, 2, 6 },   // i-VI-III-VII
            { 0, 3, 5, 4 },   // i-iv-VI-V
            { 0, 3, 5, 4 },   // I-IV-vi-V
            { 0, 5, 1, 4 },   // I-vi-ii-V
            { 0, 5, 3, 4 },   // Imaj7-vi7-IV7-V7
            { 0, 3, 6, 2 },   // i7-iv7-VII7-III7
            { 0, 5, 6, 0 },   // i9-VI-VII-i
            { 0, 3, 5, 4 },   // Imaj9-IV-vi-V
            { 5, 3, 4, 0 },   // vi7-IV-V-I
            { 0, 5, 2, 6 },   // i7-VI7-III7-VII7
            // --- 扩展调式色彩进行 ---
            { 0, 6, 5, 4 },   // i-VII-VI-V7（和声小调）
            { 0, 3, 4, 0 },   // i-iv-V7-i（和声小调终止式）
            { 0, 1, 4, 0 },   // i-iiø-V7-i（和声小调爵士终止）
            { 0, 3, 2, 5 },   // i7-IV7-III7-VI7（旋律小调）
            { 0, 1, 6, 0 },   // i-♭II-♭VII-i（弗里吉亚属）
            { 0, 3, 1, 0 }    // I7#11-IV-II-I（利底亚属）
        };
        return degs[juce::jlimit (0, numProgs - 1, i)];
    }

    // 该进行是否为"七和弦型"（氛围色彩更浓）
    //  ⚠️ 索引 11 之后新增的 12..17 也都是七和弦/延伸型
    static bool progIsSeventh (int i)
    {
        return i >= 6;
    }

    int key   = 0;  // 0=C ... 11=B
    int scale = 0;
    int prog  = 0;

    // 当前小节（内部维护）
    void setBar (int bar) { currentBar = juce::jmax (0, bar); }
    int  getBar() const   { return currentBar; }

    // 当前和弦的调内度数
    int chordDegree() const { return progDegrees (prog)[currentBar % 4]; }

    // 和弦音的音高级（pitch class, 0-11）
    //   chordTone: 0=根音 1=三音 2=五音 3=七音（氛围色彩音）
    // 若是"七和弦型"进行，七音档使用真正的七度音（而非六度）
    int chordTonePc (int chordTone) const
    {
        const int* sc = scaleSteps (scale);
        const int t  = juce::jlimit (0, 3, chordTone);

        // 三和弦进行：七音档用"调内六度"（近似 6th 和弦，柔和不冲突）
        // 七和弦进行：七音档用"调内七度"（真正的 7th，氛围悬疑感的来源）
        const int stepOffset = (t <= 2) ? (t * 2) : (progIsSeventh (prog) ? 6 : 5);

        const int deg = chordDegree() + stepOffset;
        return (key + sc[deg % 7] + 12 * (deg / 7)) % 12;
    }

    juce::String currentChordName() const
    {
        static const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int* sc = scaleSteps (scale);
        const int deg = chordDegree();
        const int root = (key + sc[deg % 7]) % 12;
        const int third = sc[(deg + 2) % 7] + (deg + 2 >= 7 ? 12 : 0) - sc[deg];
        const bool minorish = (third == 3);
        return juce::String (noteNames[root]) + (minorish ? "m" : "");
    }

    // 跟随和弦时的绝对音高（以调的主音为基准，忽略用户选的基准音）
    // 目前 UI 走的是 followNote()，此函数保留作为"严格调内"的备选/参考。
    int chordNote (int tone, int octaveShift = 0) const
    {
        const int* sc = scaleSteps (scale);
        const int deg = chordDegree() + juce::jlimit (0, 2, tone) * 2;
        const int base = 48 + key + sc[deg % 7] + 12 * (deg / 7);
        return juce::jlimit (0, 127, base + 12 * octaveShift);
    }

    // 跟随和弦：发声 = 把"基准音"移到当前和弦的对应和弦音上（就近移动，音区不变）
    //   例：基准音 C4，当前和弦 G → 根音档发 G4，3rd 档发 B4，5th 档发 D5 附近
    //   和弦进行每小节变化时，发声跟着整体平移
    //   anchorNote: 用户在 pitchBox 选的基准音
    //   tone:       0=根音 1=三音 2=五音 3=七音(氛围色彩音)
    int followNote (int anchorNote, int tone = 0, int octaveShift = 0) const
    {
        const int chordPc  = chordTonePc (tone);
        const int anchorPc = ((anchorNote % 12) + 12) % 12;

        int diff = chordPc - anchorPc;
        if (diff > 6)   diff -= 12;
        if (diff < -6)  diff += 12;

        return juce::jlimit (0, 127, anchorNote + diff + 12 * octaveShift);
    }

    // 音区分层：给球的"档位"配默认音区，让和声向上堆叠、不挤在一个八度
    //   根音→低音区(C3)、三音→中音区(E4)、五音→中音区、七音→高音区
    //   并做基础 voice leading：五音不低于三音，避免声部交叉
    int chordToneInRegister (int chordTone, int octaveShift = 0) const
    {
        const int t = juce::jlimit (0, 3, chordTone);

        // 目标音区八度：根=3、三/五=4、七=5
        static const int baseOctave[4] = { 3, 4, 4, 5 };
        int note = (baseOctave[t] + 1) * 12 + chordTonePc (t);

        // ---- voice leading：保证 根 < 三 < 五 < 七 的堆叠顺序 ----
        // 每个音都只向上借"最近的一个八度"，避免出现跨八度的突兀跳跃
        if (t == 2)   // 五音不得低于三音
        {
            const int third = (baseOctave[1] + 1) * 12 + chordTonePc (1);
            while (note < third) note += 12;
        }
        else if (t == 3)  // 七音不得低于五音（五音已经过 voice leading）
        {
            int fifth = (baseOctave[2] + 1) * 12 + chordTonePc (2);
            const int third = (baseOctave[1] + 1) * 12 + chordTonePc (1);
            while (fifth < third) fifth += 12;

            // 七音从"五音之上最近的同 pc 音"开始，避免整八度跳跃
            const int targetPc = chordTonePc (3);
            note = fifth + (((targetPc - fifth % 12) % 12 + 12) % 12);
        }
        else if (t == 1)  // 三音不得低于根音
        {
            const int root = (baseOctave[0] + 1) * 12 + chordTonePc (0);
            while (note < root) note += 12;
        }

        return juce::jlimit (0, 127, note + 12 * octaveShift);
    }

private:
    int currentBar = 0;
};
