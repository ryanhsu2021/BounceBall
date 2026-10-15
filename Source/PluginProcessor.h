#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>
#include "World.h"
#include "TriggerQueue.h"
#include "Voices.h"
#include "AmbienceFX.h"

//==============================================================================
class BounceAudioProcessor : public juce::AudioProcessor
{
public:
    BounceAudioProcessor();
    ~BounceAudioProcessor() override;

    //==== 生命周期 ====
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==== 编辑器 ====
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==== 元信息 ====
    const juce::String getName() const override { return "BounceBall"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    // ⚠️ 这是"无限尾音"插件：它是自走的信号发生器（弹球 + 琶音），
    //    发不发声完全由内部状态决定，跟输入信号无关。
    //    声明为"无限尾音"是告诉宿主：不要因为暂时没声就停止调用 processBlock。
    //    （声明 1.0 秒时，某些宿主会在静音后判定"尾巴已结束"从而冻结插件 ——
    //     窗口关掉后的表现就是再也醒不过来。）
    //    Note: JUCE 约定负数/极大值表示"无限"；VST3 会据此设置 kInfiniteTail。
    double getTailLengthSeconds() const override { return 1.0e9; }

    //==== programs ====
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==== DAW 播放状态 ====
    bool isPlaying() const;
    double getCurrentBpm() const;
    int getCurrentBar() const;

    //==== 世界（UI 线程访问）====
    World& getWorld() { return world; }

    //==== 传输状态：已由音频线程自己判断（processBlock 内），无需 UI 同步 ====

    //==== 切换音色 → 让已发声的旧音色立刻淡出（否则 Pad 会继续响好几秒）====
    void releaseAllVoices() { voiceReset.fetch_add (1, std::memory_order_relaxed); }

    //==== Mute / Solo 闸门：只改"音量"，不重起包络 ==========================
    //  与 releaseAllVoices 的区别很关键：
    //    · releaseAllVoices 让 voice 进 release 段 → 音会断、取消后从头起音
    //      （这是"切换音色"要的效果，因为音色都换了）
    //    · setBallGate 只改输出增益 → 声音"还在走"，只是听不见；取消后
    //      从当前电平无缝淡回。这正是 Mute / Solo 应有的语义。
    //  index 为球在 world.balls 里的下标；任何越界/无球都会被视为"全部发声"。
    void setBallGate (int index, bool audible);

    //==== 碰撞 / 琶音触发（音频线程调用）====
    //  index             → 该球在 balls 里的下标（查 Mute/Solo 闸门）
    //  overrideNote < 0 → 用球自己的音高（碰撞）；≥ 0 → 用这个音高（琶音器接管）
    //  velScale          → 力度缩放（琶音 Chord 模式一次多颗音时压一档）
    void triggerBall (const Ball& b, int index, int overrideNote = -1, float velScale = 1.0f);

    //==== MIDI 输出端口 ====
    void setMidiOutput (const juce::String& deviceIdentifier);
    juce::String getMidiOutputId() const { return midiOutId; }

    // 由 Editor 的定时器驱动（保证在消息线程）
    void flushMidi();

    //==== 状态保存 ====
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    // 物理世界（UI 线程所有）
    World world;

    // UI → 音频 触发队列
    TriggerQueue triggerQueue;

    // 内置音源 voices（多泛音 + 包络，见 Voices.h）
    static constexpr int maxVoices = 64;
    Voice voices[maxVoices];
    double currentSampleRate = 44100.0;

    // 上一块结束时仍在发声的 voice 数（音频线程独占；静默快速通道据此跳过整条链）
    int activeVoiceCount = 0;

    // 传输停止时释放预览 voice
    std::atomic<bool> previewReleasing { false };

    // 切换音色时递增 → 音频线程发现变化就把所有 voice 强制进入 release
    std::atomic<uint32_t> voiceReset { 0 };
    uint32_t lastVoiceReset = 0;

    //==== Mute / Solo 闸门 ==================================================
    //  语义：把（被排除的）声部音量降到 0 —— 不是"停止发声"。
    //  所以音量变化不走 voiceReset（那会让 voice 进 release、重新起音），
    //  而是写进这张位图，音频线程把它换算成每个 voice 的目标增益，
    //  再逐采样线性平滑（取消 S/M 时从当前电平无缝淡回，绝不重起）。
    //
    //  编码：第 i 个球占 bit 2i / 2i+1，两位同值（0=不发声，3=发声）。
    //  ★ 两位一起读写 → 天然原子：消息线程只存整张表，音频线程只读整张表，
    //    不存在"读到改了一半的某一个球"的中间态。
    //    （球上限 32 个，正好一球两位。）
    std::atomic<uint32_t> gainMask { 0xffffffffu };

    // 氛围效果链（延时 + 混响，固定参数）
    AmbienceFX ambience;

    //==== 渲染 ====
    void renderVoices (juce::AudioBuffer<float>& buffer);
    void startVoice (int midiNote, float velocity, int timbre, int ballIndex);
    void updateVoiceGates();   // 把 gainMask 换算成 voice 的目标增益（音频线程）

    // MIDI 端口输出（message thread）
    struct PendingMidi { double dueTime; juce::MidiMessage msg; };
    std::vector<PendingMidi> pendingMidi;
    std::mutex midiMutex;
    std::unique_ptr<juce::MidiOutput> midiOutput;
    juce::String midiOutId;
    // 端口是否打开（原子，供 flushMidi 无锁快速返回：没开端口就不用抢互斥锁）
    std::atomic<bool> midiOutOpen { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BounceAudioProcessor)
};
