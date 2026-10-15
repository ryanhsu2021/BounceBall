#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

//==============================================================================
BounceAudioProcessor::BounceAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // 注意：不要在构造函数里 startTimerHz()！
    // 宿主扫描插件时在非消息线程构造，启动 Timer 会导致加载失败。
    // MIDI flush 由 Editor 的定时器驱动（见 PluginEditor.cpp）。

    // ---- 碰撞 / 琶音 → 发声：绑在这里，不绑在 Editor 上 ----
    //  ★ 关键：ARP 推进是音频线程干的（processBlock），而 Editor 会被宿主随时关闭。
    //    绑到 Editor 的 `this` 上，关窗后音频线程一调就是悬空指针。
    //    绑到 processor（生命周期与插件一致）才是安全的：
    //      · 碰撞触发：由 UI 定时器驱动，窗口关掉自然不再发生
    //      · 琶音触发：由音频线程驱动，窗口关掉后照常发声  ← 这正是我们要的
    world.onTrigger = [this] (const Ball& b, int index, int overrideNote, float velScale)
    { triggerBall (b, index, overrideNote, velScale); };
}

BounceAudioProcessor::~BounceAudioProcessor()
{
    // 先摘掉回调（捕获 this），再拆 MIDI 端口 —— 否则销毁期间若有残留在跑的回调就危险
    world.onTrigger = nullptr;

    std::lock_guard<std::mutex> lk (midiMutex);
    midiOutput.reset();
}

//==============================================================================
void BounceAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    for (auto& v : voices)
        v.active = false;

    ambience.prepare (sampleRate);
}

void BounceAudioProcessor::releaseResources() {}

bool BounceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet().isDisabled();
}

//==============================================================================
bool BounceAudioProcessor::isPlaying() const
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            return pos->getIsPlaying();
    return false;
}

double BounceAudioProcessor::getCurrentBpm() const
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                return *bpm;
    return 120.0;
}

int BounceAudioProcessor::getCurrentBar() const
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto ppq = pos->getPpqPositionOfLastBarStart())
                return (int) std::floor (*ppq / 4.0 + 1e-6);
    return 0;
}

//==============================================================================
// 把 gainMask 换算成每个 voice 的目标增益
//  ① 先把所有现有 voice 的目标按"它所属的球"定下来
//  ② 再看待处理触发：某个球不发声 → 它接下来要响的那个 voice 也先闸住
//  ⚠️ mask 里"没被写过"的球都是 1（见 setBallGate），所以不用清理状态；
//     球被删掉后位图里那两位自然回到 1，不会留下幽灵静音。
void BounceAudioProcessor::updateVoiceGates()
{
    const uint32_t mask = gainMask.load (std::memory_order_relaxed);

    auto ballAudible = [mask] (int idx)
    {
        if (idx < 0) return true;
        return ((mask >> (2 * idx)) & 0x3u) != 0;
    };

    // ① 已在响的 voice
    for (auto& v : voices)
        v.gateTarget = ballAudible (v.ballIndex) ? 1.0f : 0.0f;

    // ② 待处理的触发：提前把"即将被占用"的 voice 也闸住，
    //    否则被静音的球会漏出第一个音的起音（约几十毫秒的"咳"一声）
    for (auto it = triggerQueue.begin(); it != triggerQueue.end(); ++it)
    {
        if (ballAudible (it->ballIndex)) continue;
        for (auto& v : voices)
            if (v.midiNote == it->note && v.timbre == it->timbre)
                v.gateTarget = 0.0f;
    }
}

//==============================================================================
void BounceAudioProcessor::setBallGate (int index, bool audible)
{
    // 位图编码：第 i 个球占 bit 2i / 2i+1 → 两位同值。
    //  读的时候从任意起点取两位都是完整的（不存在被写一半的球）。
    uint32_t mask = 0;

    for (int i = 0; i < World::maxBalls; ++i)
    {
        // 只有目标球按 audible 写，其余一律写"发声"(1)：
        //  → 每次调用都是"这一颗球发声 / 不发声"，顺手把别的球复位成 1，
        //    所以不需要维护"谁在静音"的额外状态，也不会因为漏复位而串音。
        //  → 想同时静音多颗球时要逐个调用（或调用 Editor 的全量刷新）。
        const bool on = (i == index) ? audible : true;
        mask |= (on ? 0x3u : 0x0u) << (2 * i);
    }

    // 越界（无球 / 删除后）→ 全部发声：宁可出声，也不要静默
    if (index < 0 || index >= World::maxBalls)
        mask = 0xffffffffu;

    gainMask.store (mask, std::memory_order_relaxed);
}

//==============================================================================
void BounceAudioProcessor::triggerBall (const Ball& b, int ballIndex, int overrideNote, float velScale)
{
    // ---- Mute / Solo 闸门（"音量减到 0"，不是"停止发声"）----
    //  被排除的球不推触发；而已经在响的 voice 靠 renderVoices 里的
    //  Voice::muteGain 淡到 0（包络照常走完），所以取消 S / M 之后
    //  是从当前电平无缝淡回，绝不会重新起音。
    //  ⚠️ 读的是 UI 线程写好的 Ball 快照（音频线程持有 uiLock 期间读，安全），
    //     不用再动 PluginProcessor 里的位图状态。
    if (b.mute && ! world.anySolo())
        return;
    if (world.anySolo() && ! b.solo)
        return;

    // overrideNote ≥ 0 → 琶音器指定的音（已经算好了和弦/八度，直接用）
    // follow = false   → 固定音高 b.note
    // follow = true    → b.note 是"基准音"：发声 = 把基准音就近移到当前和弦的对应和弦音上
    //                    （用户选的 Pitch 始终有效，只是发声音高会被和弦进行牵着走）
    int note;
    if (overrideNote >= 0)
        note = overrideNote;
    else if (! b.follow)
        note = b.note;
    else
        note = world.engine.followNote (b.note, b.tone, b.octaveShift);

    note = juce::jlimit (0, 127, note + b.pitchOffset);
    const int chan = juce::jlimit (1, 16, b.channel);
    const float vs = juce::jlimit (0.05f, 1.0f, velScale);

    // 内置音源（预览）
    if (world.preview)
    {
        // 力度随音区轻微变化（高音轻、低音重），让旋律有自然的动态起伏
        const float vel = juce::jlimit (0.45f, 1.0f, 1.0f - (note - 48) * 0.006f);
        Trigger t;
        t.note     = note;
        t.channel  = chan;
        t.velocity = vel * vs * juce::jlimit (0.0f, 1.0f, b.volume);   // 每球音量 × 琶音缩放
        t.timbre   = juce::jlimit (0, Timbre::num - 1, b.timbre);
        t.ballIndex = ballIndex;                                       // 供 Mute/Solo 闸门追到"哪个球"
        triggerQueue.push (t);
    }

    // MIDI 端口输出 —— 与 Preview 互斥（2026-10-15 用户定稿）：
    //  Preview 开 = 内置音源本地听，不往外发 MIDI（避免内外重复发声）；
    //  Preview 关 = 只往 MIDI 端口（如 loopMIDI）输出。
    if (! world.preview)
    {
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        std::lock_guard<std::mutex> lk (midiMutex);
        if (midiOutput != nullptr)
        {
            // MIDI 输出的力度也跟随每球音量（与预览听感一致）
            const int vel7 = juce::jlimit (1, 127,
                (int) std::lround (115.0f * vs * juce::jlimit (0.0f, 1.0f, b.volume)));
            pendingMidi.push_back ({ now, juce::MidiMessage::noteOn (chan, note, (juce::uint8) vel7) });
            pendingMidi.push_back ({ now + 0.35, juce::MidiMessage::noteOff (chan, note) });
        }
    }
}

//==============================================================================
void BounceAudioProcessor::startVoice (int midiNote, float velocity, int timbre, int ballIndex)
{
    timbre = juce::jlimit (0, Timbre::num - 1, timbre);

    // 同音高 + 同音色已经在发声 → 复用同一个 voice，
    // 否则 Pad/Bell 这类长尾音色会被反复触发、层层堆到糊
    for (auto& v : voices)
        if (v.active && v.timbre == timbre && v.midiNote == midiNote)
        {
            v.ballIndex = ballIndex;   // 换主人的球要跟着换（闸门按它查）
            if (v.sustainLevel > 0.0f && v.stage != Voice::Stage::release)
            {
                // 持续型（Pad）：**不重置包络**，只把"保持计时"归零
                // → 球一直在动时 Pad 连续铺底；不再出现"断一下又重新起来"
                v.holdCounter = 0.0;
                v.velocity = juce::jlimit (0.0f, 1.0f, velocity);
            }
            else
                v.prepare (currentSampleRate, timbre, midiNote, velocity);  // 打击型：重新起音
            return;
        }

    // 找空闲 voice；没有空闲则抢包络最低的（最不显眼的）
    Voice* best = nullptr;
    for (auto& v : voices)
    {
        if (! v.active) { best = &v; break; }
        if (best == nullptr || v.env < best->env) best = &v;
    }
    if (best == nullptr) return;

    best->prepare (currentSampleRate, timbre, midiNote, velocity);
    best->ballIndex = ballIndex;
}

void BounceAudioProcessor::renderVoices (juce::AudioBuffer<float>& buffer)
{
    // ---- 切换音色：把此刻已在发声的旧音色直接推入释放段（新触发的音色不受影响）----
    const uint32_t reset = voiceReset.load (std::memory_order_relaxed);
    if (reset != lastVoiceReset)
    {
        lastVoiceReset = reset;
        for (auto& v : voices)
            if (v.active && v.stage != Voice::Stage::release)
                v.stage = Voice::Stage::release;
    }

    // ---- 先算 Mute / Solo 闸门（必须在消费触发之前！）----
    //  updateVoiceGates 要读"还没被吃掉"的触发队列，才知道某个被静音的球
    //  接下来要占哪个 voice。顺序反了就会晚一块才静下来。
    updateVoiceGates();

    // 消费触发
    Trigger t;
    while (triggerQueue.pop (t))
        startVoice (t.note, t.velocity, t.timbre, t.ballIndex);

    // ---- 再刷一次闸门（消费触发之后）----
    //  startVoice 会新建 / 复用 voice，并把它重置成"出声"（见 Voice::prepare）。
    //  如果这一批触发里有被静音的球，就必须在这里把它们压回 0 ——
    //  否则会漏出每个音开头的一小段起音（听感上是"咳"一下）。
    //  两次调用都保留：第一次负责"已在响的 voice"，这一次负责"这一块新起的 voice"。
    updateVoiceGates();

    const int numSamples = buffer.getNumSamples();
    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    const double sr = currentSampleRate;
    const bool releasing = previewReleasing.load (std::memory_order_relaxed);

    int live = 0;
    for (auto& v : voices)
    {
        if (! v.active) continue;

        for (int i = 0; i < numSamples; ++i)
        {
            float l = 0.0f, r = 0.0f;
            v.render (l, r, sr, releasing);
            left[i] += l;
            if (right != nullptr) right[i] += r;
            if (! v.active) break;   // 该 voice 已自然结束
        }
        if (v.active) ++live;
    }
    activeVoiceCount = live;
}

void BounceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // ---- 1. 物理 + 琶音：全部由音频线程按采样时钟推进 ----
    //  ★ 这是"关掉插件窗口后依然发声"的关键。球必须一直在动、一直在撞，
    //    碰撞才会经 fire() 推触发队列；ARP 也要按拍走步。
    //    以前这两件事都挂在编辑器的 60fps 定时器上 —— 窗口一关就整个静止了。
    //  ⚠️ 顺序很关键：先推物理/琶音（往 triggerQueue 塞 Trigger），
    //     再由下面的 renderVoices 消费。反过来的话本块的音要等下一块才响。
    {
        const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;
        const double dt = (double) buffer.getNumSamples() / sr;
        const bool   playing = world.followDaw ? isPlaying() : world.manualPlaying;
        const double bpm     = world.followDaw ? getCurrentBpm() : world.manualBpm;

        // 跟随 DAW 时用小节号对齐；手动模式由 World 内部累计推进（在 advancePhysicsLocked 里）
        world.internalBarAdvance = ! world.followDaw;
        const int bar = (world.followDaw && playing) ? getCurrentBar() : -1;

        // 一次调用把所有"需要写 World 的活"都做完（内部统一持锁，避免多次进出）
        world.audioThreadUpdate (dt, bpm, playing, bar);

        // 传输停止 → 让所有预览 voice 进入释放段（Pad 这类持续型音色跟着停）。
        //  ⚠️ 以前这行在编辑器定时器里，窗口一关就不再更新 —— 现在搬进音频线程，
        //     保证关窗后行为与开窗一致。
        previewReleasing.store (! playing, std::memory_order_relaxed);
    }

    // ---- 2. 渲染（内部会先消费 triggerQueue）----
    renderVoices (buffer);

    // ---- 静默快速通道 ----
    //  没有 voice 在响 + 没有待处理的触发 + 效果器尾巴已散尽 → 整条链都不用算了。
    //  这是待机时 CPU 占用能压到接近 0 的关键（否则 64 voice 循环 + 30 次/采样的
    //  延时/混响滤波会一直空转）。
    //  ⚠️ ARP 在跑时不能旁路：ARP 的每个音是"脉冲式"的（triggerQueue 随进随出），
    //     两个音之间的空隙会让 activeVoiceCount 掉到 0、ambience 也判定 idle，
    //     结果下个琶音音一进来要从零开始跑效果链 —— 听感上是尾巴被切掉。
    if (activeVoiceCount == 0 && triggerQueue.isEmpty()
        && ambience.isIdle() && ! world.isArpActive())
        return;

    // ---- 氛围效果：乒乓延时（跟随 BPM）+ 混响 ----
    //  跟随 DAW 时用宿主 BPM；手动模式用 World 里设的 BPM
    ambience.setBpm (world.followDaw ? getCurrentBpm() : world.manualBpm);

    const int numSamples = buffer.getNumSamples();
    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        float l = left[i];
        float r = right != nullptr ? right[i] : l;
        ambience.process (l, r);
        left[i] = l;
        if (right != nullptr) right[i] = r;
    }
}

//==============================================================================
void BounceAudioProcessor::setMidiOutput (const juce::String& deviceIdentifier)
{
    std::lock_guard<std::mutex> lk (midiMutex);
    midiOutput.reset();
    pendingMidi.clear();
    midiOutId = deviceIdentifier;
    midiOutOpen.store (false, std::memory_order_relaxed);

    if (deviceIdentifier.isNotEmpty())
    {
        midiOutput = juce::MidiOutput::openDevice (deviceIdentifier);
        midiOutOpen.store (midiOutput != nullptr, std::memory_order_relaxed);
    }
}

void BounceAudioProcessor::flushMidi()
{
    // 没开端口 → 无锁直接返回（原来每 16ms 都会白抢一次互斥锁）
    if (! midiOutOpen.load (std::memory_order_relaxed))
        return;

    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    std::lock_guard<std::mutex> lk (midiMutex);
    if (midiOutput == nullptr)
    {
        pendingMidi.clear();
        return;
    }
    for (size_t i = 0; i < pendingMidi.size();)
    {
        if (pendingMidi[i].dueTime <= now)
        {
            midiOutput->sendMessageNow (pendingMidi[i].msg);
            pendingMidi[i] = pendingMidi.back();
            pendingMidi.pop_back();
        }
        else
            ++i;
    }
}

//==============================================================================
juce::AudioProcessorEditor* BounceAudioProcessor::createEditor()
{
    return new BounceAudioProcessorEditor (*this);
}

//==============================================================================
void BounceAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto xml = std::make_unique<juce::XmlElement> ("BounceBallState");

    xml->setAttribute ("prog",    world.engine.prog);
    xml->setAttribute ("scale",   world.engine.scale);
    xml->setAttribute ("key",     world.engine.key);
    xml->setAttribute ("preview", world.preview);
    xml->setAttribute ("midiDev", midiOutId);
    xml->setAttribute ("followDaw", world.followDaw);
    xml->setAttribute ("manualBpm", world.manualBpm);
    xml->setAttribute ("manualPlaying", world.manualPlaying);
    xml->setAttribute ("timbre", world.timbre);

    auto* ballsXml = xml->createNewChildElement ("Balls");
    for (auto& b : world.balls)
    {
        auto* bx = ballsXml->createNewChildElement ("Ball");
        bx->setAttribute ("x", b.pos.x);
        bx->setAttribute ("y", b.pos.y);
        bx->setAttribute ("note", b.note);
        bx->setAttribute ("follow", b.follow);
        bx->setAttribute ("autoreg", b.autoRegister);
        bx->setAttribute ("tone", b.tone);
        bx->setAttribute ("oct", b.octaveShift);
        bx->setAttribute ("poff", b.pitchOffset);
        bx->setAttribute ("rate", b.rate);
        bx->setAttribute ("ch", b.channel);
        bx->setAttribute ("col", b.colour.toString());
        bx->setAttribute ("timbre", b.timbre);
        bx->setAttribute ("mute", b.mute);
        bx->setAttribute ("solo", b.solo);
        bx->setAttribute ("vol", (double) b.volume);
        bx->setAttribute ("prob", (double) b.probability);
        // 琶音器（每球独立）
        bx->setAttribute ("arp",      b.arpOn);
        bx->setAttribute ("arpRate",  b.arpRate);
        bx->setAttribute ("arpMode",  b.arpMode);
        bx->setAttribute ("arpOct",   b.arpOct);
        bx->setAttribute ("arpSteps", b.arpSteps);
    }

    copyXmlToBinary (*xml, destData);
}

void BounceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName ("BounceBallState"))
        {
            world.engine.prog    = xml->getIntAttribute ("prog", 0);
            world.engine.scale   = xml->getIntAttribute ("scale", 0);
            world.engine.key     = xml->getIntAttribute ("key", 0);
            world.preview        = xml->getBoolAttribute ("preview", true);
            world.followDaw      = xml->getBoolAttribute ("followDaw", false);
            world.manualBpm      = xml->getDoubleAttribute ("manualBpm", 60.0);
            world.manualPlaying  = xml->getBoolAttribute ("manualPlaying", true);
            world.timbre         = juce::jlimit (0, Timbre::num - 1, xml->getIntAttribute ("timbre", 6));
            const juce::String devId = xml->getStringAttribute ("midiDev");

            world.balls.clear();
            world.clearSelection();
            if (auto* ballsXml = xml->getChildByName ("Balls"))
                for (auto* bx : ballsXml->getChildWithTagNameIterator ("Ball"))
                {
                    Ball b;
                    b.pos.x = (float) bx->getDoubleAttribute ("x", 0.0);
                    b.pos.y = (float) bx->getDoubleAttribute ("y", 0.0);
                    b.note   = bx->getIntAttribute ("note", 60);
                    b.follow = bx->getBoolAttribute ("follow", true);
                    b.autoRegister = bx->getBoolAttribute ("autoreg", true);
                    b.tone   = bx->getIntAttribute ("tone", 0);
                    b.octaveShift = bx->getIntAttribute ("oct", 0);
                    b.pitchOffset = bx->getIntAttribute ("poff", 0);
                    b.rate   = bx->getIntAttribute ("rate", 2);
                    b.channel = bx->getIntAttribute ("ch", 1);
                    b.colour = juce::Colour::fromString (bx->getStringAttribute ("col", "ff42a5f5"));
                    b.timbre = juce::jlimit (0, Timbre::num - 1,
                                             bx->getIntAttribute ("timbre", world.timbre));
                    b.mute = bx->getBoolAttribute ("mute", false);
                    b.solo = bx->getBoolAttribute ("solo", false);
                    b.volume = juce::jlimit (0.0f, 1.0f, (float) bx->getDoubleAttribute ("vol", 1.0));
                    // 触发概率（旧工程没有这一项 → 缺省 1.0 = 每次都响，行为不变）
                    b.probability = juce::jlimit (0.0f, 1.0f,
                                                  (float) bx->getDoubleAttribute ("prob", 1.0));
                    // 琶音器（缺省关闭，保持旧工程行为不变）
                    b.arpOn    = bx->getBoolAttribute ("arp", false);
                    b.arpRate  = juce::jlimit (0, 3, bx->getIntAttribute ("arpRate", 3));
                    b.arpMode  = juce::jlimit (0, Ball::numArpModes - 1, bx->getIntAttribute ("arpMode", 0));
                    b.arpOct   = juce::jlimit (1, 3, bx->getIntAttribute ("arpOct", 1));
                    b.arpSteps = juce::jlimit (2, 8, bx->getIntAttribute ("arpSteps", 4));

                    float ang = (float) juce::Random::getSystemRandom().nextDouble()
                                * juce::MathConstants<float>::twoPi;
                    b.vel = { std::cos (ang), std::sin (ang) };
                    world.balls.push_back (b);
                }

            // 延迟到 message thread 恢复 MIDI 端口 + 刷新编辑器控件
            juce::MessageManager::callAsync ([this, devId]
            {
                setMidiOutput (devId);

                // 若编辑器已打开，同步控件到刚恢复的状态
                if (auto* ed = dynamic_cast<BounceAudioProcessorEditor*> (getActiveEditor()))
                    ed->refreshFromProcessor();
            });
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BounceAudioProcessor();
}
