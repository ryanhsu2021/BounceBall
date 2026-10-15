#include "PluginEditor.h"
#include <cmath>

//==============================================================================
BounceAudioProcessorEditor::BounceAudioProcessorEditor (BounceAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), ballPanel (p), ballStrip (p)
{
    auto& world = audioProcessor.getWorld();

    setLookAndFeel (&lnf);   // 统一黑底低饱和外观

    // 碰撞 / 琶音 → 发声/MIDI 输出
    //  ⚠️ 不要在这里绑定！onTrigger 会由音频线程调用（ARP 推进），而编辑器窗口
    //     可以被宿主随时关闭 —— 绑定到 `this` 就是悬空引用。
    //     这个回调现在在 BounceAudioProcessor 构造里绑好（生命周期与插件一致）。

    addAndMakeVisible (ballPanel);

    // ---- 顶部：进行 / 调式 / 调 / MIDI out / Preview ----
    addAndMakeVisible (progBox);
    // 前 6 个是三和弦进行，后 6 个是七和弦/氛围进行
    for (int i = 0; i < ChordEngine::numProgs; ++i)
    {
        if (i == 6)
            progBox.addSectionHeading ("Ambient / 7th");
        progBox.addItem (ChordEngine::progName (i), i + 1);
    }
    progBox.setSelectedItemIndex (world.engine.prog);
    progBox.onChange = [this] { audioProcessor.getWorld().engine.prog = progBox.getSelectedItemIndex(); };

    addAndMakeVisible (scaleBox);
    for (int i = 0; i < ChordEngine::numScales; ++i)
        scaleBox.addItem (ChordEngine::scaleName (i), i + 1);
    scaleBox.setSelectedItemIndex (world.engine.scale);
    scaleBox.onChange = [this] { audioProcessor.getWorld().engine.scale = scaleBox.getSelectedItemIndex(); };
    // 提示：这是音阶（调式）选择
    scaleBox.setTooltip ("Scale / Mode - 用哪套音（决定整体色彩）");
    progBox.setTooltip ("Chord progression - 每小节用哪个和弦");
    keyBox.setTooltip ("Key - 主音（整首以哪个音为中心）");

    addAndMakeVisible (keyBox);
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int i = 0; i < 12; ++i) keyBox.addItem (names[i], i + 1);
    }
    keyBox.setSelectedItemIndex (world.engine.key);
    keyBox.onChange = [this] { audioProcessor.getWorld().engine.key = keyBox.getSelectedItemIndex(); };

    refreshMidiList();
    addAndMakeVisible (midiBox);
    midiBox.onChange = [this]
    {
        const int idx = midiBox.getSelectedItemIndex();
        if (idx <= 0)
        {
            audioProcessor.setMidiOutput ({});
            return;
        }
        auto devices = juce::MidiOutput::getAvailableDevices();
        const int d = idx - 1;
        if (d < (int) devices.size())
            audioProcessor.setMidiOutput (devices[d].identifier);
    };

    addAndMakeVisible (previewToggle);
    previewToggle.setToggleState (world.preview, juce::dontSendNotification);
    previewToggle.onClick = [this] { audioProcessor.getWorld().preview = previewToggle.getToggleState(); };

    // ---- 统一音色总设置已移除：每个球用自己的音色（下方属性区 Tone）----
    // 新建球的默认音色沿用 World::timbre（Pluck）

    // ---- 传输控制：默认用自己的 BPM（dawToggle = 跟随 DAW，默认关）----
    addAndMakeVisible (dawToggle);
    dawToggle.setToggleState (world.followDaw, juce::dontSendNotification);
    // 提示：这一步决定"什么时候发声"，对关闭窗口后的行为影响最大
    dawToggle.setTooltip ("Follow DAW - ON: 跟随宿主走带（推荐）。关掉插件窗口后 "
                          "ARP 与碰撞触音照常发声；OFF: 由界面上的 Play/Stop 控制，"
                          "窗口关掉即静音");
    dawToggle.onClick = [this]
    {
        auto& w = audioProcessor.getWorld();
        w.followDaw = dawToggle.getToggleState();
        bpmSlider.setEnabled (! w.followDaw);
        runButton.setEnabled (! w.followDaw);
    };

    addAndMakeVisible (bpmLabel);
    bpmLabel.setText ("BPM", juce::dontSendNotification);
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    bpmLabel.setColour (juce::Label::textColourId, Ui::inkSoft);

    addAndMakeVisible (bpmSlider);
    bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 30);
    bpmSlider.setRange (40.0, 300.0, 1.0);
    bpmSlider.setValue (world.manualBpm, juce::dontSendNotification);
    bpmSlider.onValueChange = [this] { audioProcessor.getWorld().manualBpm = bpmSlider.getValue(); };
    bpmSlider.setEnabled (! world.followDaw);
    bpmSlider.setScrollWheelEnabled (false);   // 关掉自带滚轮，走 WheelCatcher（固定 1 BPM 步进）

    addAndMakeVisible (runButton);
    runButton.setClickingTogglesState (true);
    runButton.setColour (juce::TextButton::buttonOnColourId, Ui::run);      // 运行中：灰绿
    runButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white.withAlpha (0.96f));
    runButton.setToggleState (world.manualPlaying, juce::dontSendNotification);
    runButton.setButtonText (world.manualPlaying ? "Stop" : "Play");   // 默认在跑 → 显示 Stop
    runButton.onClick = [this]
    {
        auto& w = audioProcessor.getWorld();
        w.manualPlaying = runButton.getToggleState();
        runButton.setButtonText (w.manualPlaying ? "Stop" : "Play");
    };
    runButton.setEnabled (! world.followDaw);

    // 小球列表
    addAndMakeVisible (ballStrip);
    ballStrip.onSelect = [this] { refreshBallControls(); };

    // 速度固定 0.5x，不开放调节（用户确认的手感）

    // ---- 底部：球属性 ----
    addAndMakeVisible (addCenterButton);
    addCenterButton.setTooltip ("+ Add - the new ball takes the next voice from low to high: "
                                "Bass (root, low) -> Baritone (5th) -> Alto (3rd) -> Soprano (7th, high). "
                                "Note / chord tone / rate / timbre are all set to match.");
    addCenterButton.onClick = [this]
    {
        auto& w = audioProcessor.getWorld();
        float ang = (float) juce::Random::getSystemRandom().nextDouble()
                    * juce::MathConstants<float>::twoPi;
        w.addBall ({ std::cos (ang) * 0.5f, std::sin (ang) * 0.5f });
        pushGatesToProcessor();     // 新球默认全开（不推的话它会被上一轮的位图复位逻辑影响）
        refreshBallControls();
    };

    addAndMakeVisible (deleteButton);
    deleteButton.onClick = [this]
    {
        audioProcessor.getWorld().removeSelected();
        pushGatesToProcessor();     // 删完球索引会变，闸门位图必须重推
        refreshBallControls();
    };

    addAndMakeVisible (clearButton);
    clearButton.onClick = [this]
    {
        audioProcessor.getWorld().clearAll();
        pushGatesToProcessor();
        refreshBallControls();
    };

    // 一键"氛围合奏"：分析现有所有球，给选中球推荐互补的 音高/档位/速度/音色
    addAndMakeVisible (ensembleButton);
    ensembleButton.setColour (juce::TextButton::buttonColourId,   Ui::ensemble.withAlpha (0.16f));  // 雾紫淡底
    ensembleButton.setColour (juce::TextButton::textColourOffId,  Ui::ensemble.darker (0.42f));
    ensembleButton.setTooltip ("Ensemble - sort the selected balls by pitch and re-assign voices "
                               "from low to high (Bass -> Baritone -> Alto -> Soprano), "
                               "spreading the chord across registers with matching timbres and rates");
    ensembleButton.onClick = [this]
    {
        auto& w = audioProcessor.getWorld();
        w.applyEnsembleToSelection();        // 只改选中球：按其它球推荐互补的 音高/档位/速度/音色
        audioProcessor.releaseAllVoices();   // 旧音色立刻淡出
        refreshBallControls();
        ballStrip.refresh();
    };

    addAndMakeVisible (pitchBox);
    {
        // 专业排布：由低到高，越往下音高越低（与 DAW / 音高表惯例一致）
        //   C2 在最下，B5 在最上；每个八度前加分组标题，方便快速定位
        //   列表里两项之间是"半音"，等距排列，方便上下微调半音
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int oct = 5; oct >= 2; --oct)
        {
            pitchBox.addSectionHeading ("Octave " + juce::String (oct));
            for (int n = 11; n >= 0; --n)     // 从 B 往 C 降序
                pitchBox.addItem (juce::String (names[n]) + juce::String (oct),
                                  (oct - 2) * 12 + n + 1);   // itemId 仍与 note 对应，不改数据映射
        }
    }
    pitchBox.setTooltip ("Note - 这个球的基础音（C2–B5）\n"
                         "· Follow Chord 关：就是发声的音高\n"
                         "· Follow Chord 开：作为锚点，实际发声音高会落到当前和弦的对应音上");
    pitchBox.onChange = [this]    {
        // 两种模式下都生效：
        //   follow=off → 直接就是发声音高
        //   follow=on  → 作为"基准音"，实际发声 = 基准音 + 和弦进行偏移
        // ⚠️ 用 itemId（= note-23）而非 index：列表里有八度分组标题，
        //    index 会被标题占位，必须按 id 映射才不会错位
        auto& w = audioProcessor.getWorld();
        if (w.selected < 0) return;
        const int id = pitchBox.getSelectedId();
        if (id <= 0) return;
        w.balls[(size_t) w.selected].note = 24 + (id - 1);
        ballStrip.refresh();
    };

    addAndMakeVisible (followToggle);
    followToggle.onClick = [this]
    {
        auto& w = audioProcessor.getWorld();
        if (w.selected >= 0)
            w.balls[(size_t) w.selected].follow = followToggle.getToggleState();
        // 重新按当前状态刷新控件可用性
        refreshBallControls();
    };

    // 跟随和弦时的八度偏移（可调音高），左侧 "oct" 小标签说明单位
    addAndMakeVisible (octLabel);
    octLabel.setColour (juce::Label::textColourId, Ui::inkSoft);
    octLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (octaveSlider);
    octaveSlider.setSliderStyle (juce::Slider::IncDecButtons);
    // 文本框 30：列宽 124 减去标签后控件只有 76，
    // 若文本框留 40，剩下 36 ≤ 行高 32 → JUCE 会把 +/- 摆成"上下堆叠"（各 16 高、很挤）；
    // 收到 30 后剩余 42 > 32 → 恢复左右并排（各 21×32），与 BPM 那一组观感一致
    octaveSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 30, 30);
    octaveSlider.setRange (-2.0, 2.0, 1.0);
    octaveSlider.setValue (0.0, juce::dontSendNotification);
    octaveSlider.setScrollWheelEnabled (false);   // 同上，走 WheelCatcher
    octaveSlider.onValueChange = [this]
    {
        auto& w = audioProcessor.getWorld();
        if (w.selected >= 0)
            w.balls[(size_t) w.selected].octaveShift = (int) octaveSlider.getValue();
    };

    //=== 触发概率 Prob（行A 最右列）========================================
    //  0..100%，步进 5（滚轮一格 = 5%，20 格走满）
    //  ⚠️ 文本框宽度必须跟着列宽算（★ 易错项，见自检清单第 12 条）：
    //     JUCE 判 incDecButtonsSideBySide = (控件宽 − 文本框宽 − 4) > 行高 32，
    //     不满足就把 +/- 竖着摆（各占半行高，很丑）。
    //     行A 6 格栅格下本控件宽 = 67 → 文本框只能给 30（剩 33 > 32 ✓）。
    //     给 32 就退化成竖排了 —— 改行A 栅格后务必重算这一条。
    addAndMakeVisible (probLabel);
    probLabel.setColour (juce::Label::textColourId, Ui::inkSoft);
    probLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (probSlider);
    probSlider.setSliderStyle (juce::Slider::IncDecButtons);
    probSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 42, 30);
    probSlider.setRange (0.0, 100.0, 5.0);
    probSlider.setValue (100.0, juce::dontSendNotification);
    probSlider.setScrollWheelEnabled (false);   // 同上，走 WheelCatcher
    probSlider.setTooltip ("Probability - the chance (0-100%) that a collision "
                           "actually makes a sound. 100% = every hit. "
                           "Use it to thin out a busy part, or to add variation.");
    probSlider.textFromValueFunction = [] (double v)
    { return juce::String ((int) std::lround (v)) + "%"; };
    probSlider.onValueChange = [this]
    {
        auto& w = audioProcessor.getWorld();
        const float p = (float) juce::jlimit (0.0, 1.0, probSlider.getValue() / 100.0);

        auto apply = [&] (int idx)
        {
            if (idx >= 0 && idx < (int) w.balls.size())
                w.balls[(size_t) idx].probability = p;
        };

        if (w.selection.empty()) apply (w.selected);
        else for (int idx : w.selection) apply (idx);
    };

    addAndMakeVisible (toneBox);
    toneBox.addItem ("Root", 1);
    toneBox.addItem ("3rd", 2);
    toneBox.addItem ("5th", 3);
    toneBox.addItem ("7th", 4);
    toneBox.onChange = [this]
    {
        auto& w = audioProcessor.getWorld();
        if (w.selected >= 0)
            w.balls[(size_t) w.selected].tone = toneBox.getSelectedItemIndex();
        ballStrip.refresh();
    };

    // 自动音区分层（Layers）已默认开启且不在界面显示：
    // 根音低、三音中、五/七音高，和声更干净（Ball::autoRegister 默认 true）

    addAndMakeVisible (rateBox);
    for (int i = 0; i < 4; ++i)
        rateBox.addItem (Ball::rateName (i), i + 1);
    rateBox.setTooltip ("Rate - how fast this ball moves (time for one round trip). "
                        "With Arp on, collisions stop triggering and the Arp rate takes over.");
    rateBox.onChange = [this]
    {
        auto& w = audioProcessor.getWorld();
        if (w.selected >= 0)
            w.balls[(size_t) w.selected].rate = rateBox.getSelectedItemIndex();
    };

    addAndMakeVisible (chanBox);
    for (int i = 1; i <= 16; ++i)
        chanBox.addItem ("Ch " + juce::String (i), i);
    chanBox.onChange = [this]
    {
        auto& w = audioProcessor.getWorld();
        if (w.selected >= 0)
            w.balls[(size_t) w.selected].channel = chanBox.getSelectedItemIndex() + 1;
    };

    // 每个球自己的音色（多选时批量设置）
    addAndMakeVisible (toneLabel);
    toneLabel.setColour (juce::Label::textColourId, Ui::inkSoft);
    toneLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (ballTimbreBox);
    for (int i = 0; i < Timbre::num; ++i)
    {
        if (i == 0)  ballTimbreBox.addSectionHeading ("Low / Bed");
        if (i == 5)  ballTimbreBox.addSectionHeading ("Mid / Points");
        if (i == 9)  ballTimbreBox.addSectionHeading ("High / Sparkle");
        ballTimbreBox.addItem (Timbre::name (i), i + 1);
    }
    ballTimbreBox.setTooltip ("This ball's tone - different balls can use different tones "
                              "(multi-select to set in bulk)");
    ballTimbreBox.onChange = [this]
    {
        auto& w = audioProcessor.getWorld();
        const int t = juce::jlimit (0, Timbre::num - 1, ballTimbreBox.getSelectedItemIndex());

        if (w.selection.empty())
        {
            if (w.selected >= 0 && w.selected < (int) w.balls.size())
                w.balls[(size_t) w.selected].timbre = t;
        }
        else
        {
            for (int idx : w.selection)
                if (idx >= 0 && idx < (int) w.balls.size())
                    w.balls[(size_t) idx].timbre = t;
        }

        // 改了音色，已在响的旧音色换掉
        audioProcessor.releaseAllVoices();
        ballStrip.refresh();
    };

    //=== 第三排：琶音器（每球独立） ==========================================
    //  开启后：碰撞不再触发发声，改由 ARP 按拍走出音级序列（当前和弦的 4 个和弦音，
    //  从球所在音区的和弦根音起升序铺开）。
    //  Rate = 每步时值 · Mode = 走法 · Oct = 向上叠几个八度 · Step = 一个循环几步
    addAndMakeVisible (arpToggle);
    arpToggle.setTooltip ("Arp - this ball plays an arpeggio of the current chord "
                          "instead of triggering on collisions");
    arpToggle.onClick = [this]
    {
        auto& w = audioProcessor.getWorld();
        if (w.selected >= 0 && w.selected < (int) w.balls.size())
        {
            w.balls[(size_t) w.selected].arpOn = arpToggle.getToggleState();
            w.resetArp (w.selected);          // 循环从头开始，不接上次的相位
        }
        audioProcessor.releaseAllVoices();    // 切换瞬间把已在响的音收掉
        refreshBallControls();
    };

    addAndMakeVisible (arpRateBox);
    for (int i = 0; i < 4; ++i)
        arpRateBox.addItem (Ball::rateName (i), i + 1);
    arpRateBox.setTooltip ("Arp rate - how long one step lasts");
    arpRateBox.onChange = [this]
    {
        auto& w = audioProcessor.getWorld();
        if (w.selected >= 0 && w.selected < (int) w.balls.size())
            w.balls[(size_t) w.selected].arpRate = juce::jlimit (0, 3, arpRateBox.getSelectedItemIndex());
    };

    addAndMakeVisible (arpModeBox);
    for (int i = 0; i < Ball::numArpModes; ++i)
        arpModeBox.addItem (Ball::arpModeName (i), i + 1);
    arpModeBox.setTooltip ("Arp mode - Up / Down / Up-Down / Random / Chord (whole chord at once)");
    arpModeBox.onChange = [this]
    {
        auto& w = audioProcessor.getWorld();
        if (w.selected >= 0 && w.selected < (int) w.balls.size())
        {
            w.balls[(size_t) w.selected].arpMode =
                juce::jlimit (0, Ball::numArpModes - 1, arpModeBox.getSelectedItemIndex());
            w.resetArp (w.selected);
        }
    };

    // Oct / Step 用 IncDecButtons：列宽 129 − 标签 36 − 间距 4 = 89，文本框留 30
    // → 剩 59 > 行高 32 → +/- 左右并排（留 40 就会变成上下堆叠，见踩坑 #12）
    addAndMakeVisible (arpOctSlider);
    arpOctSlider.setSliderStyle (juce::Slider::IncDecButtons);
    arpOctSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 30, 30);
    arpOctSlider.setRange (1.0, 3.0, 1.0);
    arpOctSlider.setScrollWheelEnabled (false);
    arpOctSlider.setTooltip ("Arp octaves - how many octaves the pattern spans (1..3)");
    arpOctSlider.onValueChange = [this]
    {
        auto& w = audioProcessor.getWorld();
        if (w.selected >= 0 && w.selected < (int) w.balls.size())
            w.balls[(size_t) w.selected].arpOct = juce::jlimit (1, 3, (int) arpOctSlider.getValue());
    };

    addAndMakeVisible (arpStepSlider);
    arpStepSlider.setSliderStyle (juce::Slider::IncDecButtons);
    arpStepSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 30, 30);
    arpStepSlider.setRange (2.0, 8.0, 1.0);
    arpStepSlider.setScrollWheelEnabled (false);
    arpStepSlider.setTooltip ("Arp steps - notes per loop (2..8)");
    arpStepSlider.onValueChange = [this]
    {
        auto& w = audioProcessor.getWorld();
        if (w.selected >= 0 && w.selected < (int) w.balls.size())
        {
            w.balls[(size_t) w.selected].arpSteps = juce::jlimit (2, 8, (int) arpStepSlider.getValue());
            w.resetArp (w.selected);          // 循环长度变了，重新从头走
        }
    };

    // 顶部不再放操作提示文字（保持两行撑满、干净）

    // ---- Solo / Mute：本球的独奏 / 静音（语义与 DAW 一致） ----
    //   Mute：只静音本球（仍在运动、仍有水纹，只是不出声）
    //   Solo：全局生效 —— 只要有球 Solo，其余球一律不发声（Solo 优先于 Mute）
    //   两者都用 TextButton 的"点灯"态：开启时按钮填成对应色，一眼可辨
    for (auto* btn : { &soloButton, &muteButton })
    {
        addAndMakeVisible (*btn);
        btn->setClickingTogglesState (true);
    }

    soloButton.setTooltip ("Solo - 只让独奏的球发声（可多选，影响全局）");
    soloButton.setColour (juce::TextButton::buttonColourId,   Ui::surfaceAlt);
    soloButton.setColour (juce::TextButton::buttonOnColourId, Ui::solo);   // 琥珀
    soloButton.setColour (juce::TextButton::textColourOffId,  Ui::inkSoft);
    soloButton.setColour (juce::TextButton::textColourOnId,   juce::Colour (0xff4a3a12));   // 琥珀压深字
    soloButton.onClick = [this] { applySoloMuteToSelection (soloButton.getToggleState(), true); };

    muteButton.setTooltip ("Mute - 只静音这个球（仍在运动，只是不发声）");
    muteButton.setColour (juce::TextButton::buttonColourId,   Ui::surfaceAlt);
    muteButton.setColour (juce::TextButton::buttonOnColourId, Ui::mute);   // 陶粉
    muteButton.setColour (juce::TextButton::textColourOffId,  Ui::inkSoft);
    muteButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white.withAlpha (0.95f));
    muteButton.onClick = [this] { applySoloMuteToSelection (muteButton.getToggleState(), false); };

    // 属性区其余小标签（浅灰说明字，右对齐贴控件）
    for (auto* lbl : { &pitchLabel, &chordLabel, &rateLabel, &chLabel, &soloLabel, &muteLabel,
                       &probLabel, &arpRateLabel, &arpModeLabel, &arpOctLabel, &arpStepLabel })
    {
        addAndMakeVisible (*lbl);
        lbl->setColour (juce::Label::textColourId, Ui::inkSoft);
        lbl->setJustificationType (juce::Justification::centredRight);
    }

    //=== 鼠标滚轮：直接在控件上接住事件，统一走一格步进 =====================
    //   向上滚 = 下一个 / 更大；向下滚 = 上一个 / 更小
    //   不走"冒泡到编辑器"那条路（宿主差异会吞事件），改为 WheelCatcher
    //   在控件类里覆写 mouseWheelMove，与 JUCE 自带滚轮行为同一条链路，最可靠
    for (auto* box : { &progBox, &scaleBox, &keyBox, &midiBox, &pitchBox,
                       &toneBox, &rateBox, &chanBox, &ballTimbreBox,
                       &arpRateBox, &arpModeBox })
        box->onWheelStep = [this, box] (int d) { stepComboBox (*box, d); };

    for (auto* s : { &bpmSlider, &octaveSlider, &probSlider, &arpOctSlider, &arpStepSlider })
        s->onWheelStep = [this, s] (int d) { stepSlider (*s, d); };

    refreshBallControls();

    // 编辑器可能是在"工程已恢复"之后才被创建的（宿主先 setStateInformation 再 createEditor）
    //  → 这里推一次初始闸门，保证界面上看到的 solo/mute 与音频线程一致
    pushGatesToProcessor();

    // 键盘：让画布可聚焦并接收 Del 键
    ballPanel.setWantsKeyboardFocus (true);
    ballPanel.addKeyListener (this);
    setWantsKeyboardFocus (true);

    // 高度比上一版 +40：新增的第三排（琶音器）吃掉了 40px，
    // 这里补回来，保证中间圆形容器尺寸不变（画布 680×648）
    setSize (720, 1060);
    startTimerHz (60); // 物理 + 重绘
}

BounceAudioProcessorEditor::~BounceAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void BounceAudioProcessorEditor::refreshMidiList()
{
    midiBox.clear (juce::dontSendNotification);
    midiBox.addItem ("MIDI Out: None", 1);
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        midiBox.addItem ("MIDI Out: " + d.name, midiBox.getNumItems() + 1);

    const juce::String cur = audioProcessor.getMidiOutputId();
    int sel = 0;
    if (cur.isNotEmpty())
    {
        auto devices = juce::MidiOutput::getAvailableDevices();
        for (int i = 0; i < (int) devices.size(); ++i)
            if (devices[i].identifier == cur)
            {
                sel = i + 1;
                break;
            }
    }
    midiBox.setSelectedItemIndex (sel, juce::dontSendNotification);
}

void BounceAudioProcessorEditor::refreshBallControls()
{
    auto& w = audioProcessor.getWorld();
    const bool has = w.selected >= 0 && w.selected < (int) w.balls.size();

    //=== 属性区随所选小球变色：整块背景 + 控件内部底色/描边/勾选框都用球色 ===
    accentOn = has;
    if (has)
        accentCol = w.balls[(size_t) w.selected].colour;

    juce::Colour fillCol   = juce::Colours::white;          // 控件内部底色
    juce::Colour borderCol (Ui::line), tickCol (Ui::run);
    if (has)
    {
        const auto& c = w.balls[(size_t) w.selected].colour;
        fillCol   = juce::Colours::white.interpolatedWith (c, 0.22f);
        borderCol = c.darker (0.25f);
        tickCol   = c.darker (0.10f);
    }
    for (auto* box : { &pitchBox, &toneBox, &rateBox, &chanBox, &ballTimbreBox,
                       &arpRateBox, &arpModeBox })
    {
        box->setColour (juce::ComboBox::backgroundColourId, fillCol);
        box->setColour (juce::ComboBox::outlineColourId, borderCol);
    }
    for (auto* sl : { &octaveSlider, &probSlider, &arpOctSlider, &arpStepSlider })
    {
        sl->setColour (juce::Slider::textBoxBackgroundColourId, fillCol);
        sl->setColour (juce::Slider::textBoxOutlineColourId, borderCol);
        // oct / arp 的 −/+ 按钮也一起染色
        for (auto* child : sl->getChildren())
            if (auto* tb = dynamic_cast<juce::TextButton*> (child))
                tb->setColour (juce::TextButton::buttonColourId, fillCol);
    }
    followToggle.setColour (juce::ToggleButton::tickColourId, tickCol);
    arpToggle.setColour   (juce::ToggleButton::tickColourId, tickCol);

    //=== Solo / Mute 底色同样跟随所选球 =====================================
    //  未点亮：与其它控件一致的"白 + 球色"淡底（fillCol）
    //  已点亮：在球色基础上加深，保持"点灯"语义（Solo 略偏暖、Mute 略偏深）
    //          这样即使换了球，开/关状态依然一眼可辨
    const juce::Colour soloOn = has ? w.balls[(size_t) w.selected].colour.darker (0.10f)
                                    : Ui::solo;
    const juce::Colour muteOn = has ? w.balls[(size_t) w.selected].colour.darker (0.30f)
                                    : Ui::mute;
    const juce::Colour onText = juce::Colours::white.withAlpha (0.95f);   // 点亮时用浅色字

    soloButton.setColour (juce::TextButton::buttonColourId,   fillCol);
    soloButton.setColour (juce::TextButton::buttonOnColourId, soloOn);
    soloButton.setColour (juce::TextButton::textColourOffId,  Ui::inkSoft);
    soloButton.setColour (juce::TextButton::textColourOnId,   onText);

    muteButton.setColour (juce::TextButton::buttonColourId,   fillCol);
    muteButton.setColour (juce::TextButton::buttonOnColourId, muteOn);
    muteButton.setColour (juce::TextButton::textColourOffId,  Ui::inkSoft);
    muteButton.setColour (juce::TextButton::textColourOnId,   onText);

    followToggle.setEnabled (has);
    rateBox.setEnabled (has);
    chanBox.setEnabled (has);
    ballTimbreBox.setEnabled (has);
    deleteButton.setEnabled (has);
    soloButton.setEnabled (has);
    muteButton.setEnabled (has);
    arpToggle.setEnabled (has);

    if (! has)
    {
        pitchBox.setEnabled (false);
        toneBox.setEnabled (false);
        octaveSlider.setEnabled (false);
        arpRateBox.setEnabled (false);
        arpModeBox.setEnabled (false);
        arpOctSlider.setEnabled (false);
        arpStepSlider.setEnabled (false);
        return;
    }

    auto& b = w.balls[(size_t) w.selected];

    followToggle.setToggleState (b.follow, juce::dontSendNotification);
    // ⚠️ pitchBox 用 id 回显（列表含八度分组标题，index 会错位）
    pitchBox.setSelectedId (juce::jlimit (1, 48, b.note - 23), juce::dontSendNotification);
    toneBox.setSelectedItemIndex (juce::jlimit (0, 3, b.tone), juce::dontSendNotification);
    octaveSlider.setValue (juce::jlimit (-2.0, 2.0, (double) b.octaveShift), juce::dontSendNotification);
    rateBox.setSelectedItemIndex (juce::jlimit (0, 3, b.rate), juce::dontSendNotification);
    chanBox.setSelectedItemIndex (juce::jlimit (0, 15, b.channel - 1), juce::dontSendNotification);
    ballTimbreBox.setSelectedItemIndex (juce::jlimit (0, Timbre::num - 1, b.timbre),
                                        juce::dontSendNotification);

    // 触发概率回显（0..1 → 0..100%）。多选时用第一颗球的读数，
    //  和 batch 修改"改的就是所有选中球"配合起来语义自洽
    probSlider.setValue (juce::jlimit (0.0, 100.0, (double) b.probability * 100.0),
                         juce::dontSendNotification);

    //=== 琶音器回显 ===
    arpToggle.setToggleState (b.arpOn, juce::dontSendNotification);
    arpRateBox.setSelectedItemIndex (juce::jlimit (0, 3, b.arpRate), juce::dontSendNotification);
    arpModeBox.setSelectedItemIndex (juce::jlimit (0, Ball::numArpModes - 1, b.arpMode),
                                     juce::dontSendNotification);
    arpOctSlider.setValue  (juce::jlimit (1.0, 3.0, (double) b.arpOct),   juce::dontSendNotification);
    arpStepSlider.setValue (juce::jlimit (2.0, 8.0, (double) b.arpSteps), juce::dontSendNotification);
    // ARP 的子项只在开启时可用 —— 关着的时候整排灰掉，语义一眼清楚
    arpRateBox.setEnabled   (b.arpOn);
    arpModeBox.setEnabled   (b.arpOn);
    arpOctSlider.setEnabled (b.arpOn);
    arpStepSlider.setEnabled (b.arpOn);

    // Solo / Mute 回显：选中单个球时反映该球状态；多选时反映"是否全部开启"
    //  ⚠️ 两个按钮互斥（见 applySoloMuteToSelection），所以这里回显出来的
    //     也必然是"至多一个亮"，不会出现两颗灯同时亮着让用户困惑
    soloButton.setToggleState (b.solo, juce::dontSendNotification);
    muteButton.setToggleState (b.mute, juce::dontSendNotification);
    if (w.selection.size() > 1)
    {
        bool allSolo = true, allMute = true;
        for (int idx : w.selection)
        {
            if (idx < 0 || idx >= (int) w.balls.size()) continue;
            allSolo = allSolo && w.balls[(size_t) idx].solo;
            allMute = allMute && w.balls[(size_t) idx].mute;
        }
        soloButton.setToggleState (allSolo, juce::dontSendNotification);
        muteButton.setToggleState (allMute, juce::dontSendNotification);
    }

    // Pitch 始终可选：
    //   不跟随 → 就是本球发声音高
    //   跟随和弦 → 是"基准音"，发声时从它就近落到当前和弦音上（Pitch 依然决定音区）
    pitchBox.setEnabled (true);
    toneBox.setEnabled (b.follow);
    octaveSlider.setEnabled (b.follow);

    deleteButton.setButtonText (w.selection.size() > 1
                                ? "Delete (" + juce::String ((int) w.selection.size()) + ")"
                                : "Delete");

    // 同步刷新列表显示（属性可能变了）
    ballStrip.refresh();
}

void BounceAudioProcessorEditor::timerCallback()
{
    // 物理不再由这里推进（已搬到音频线程），所以不需要 dt 了
    auto& w = audioProcessor.getWorld();

    // 选中集合变化 → 刷新属性面板
    if (w.selected != lastSelected || w.selection.size() != lastSelCount)
    {
        lastSelected = w.selected;
        lastSelCount = (int) w.selection.size();
        panelDirty = true;          // 选中环变了，画面要重画一次
        refreshBallControls();
    }

    // 球数量变化 → 刷新列表
    if ((int) w.balls.size() != lastBallCount)
    {
        const int prevCount = lastBallCount;
        lastBallCount = (int) w.balls.size();
        panelDirty = true;
        ballStrip.refresh();

        // 从"有球"变成"没球" → 释放所有正在发声的 voice
        // （否则 Pad 这类持续型音色会一直吊着响）
        if (w.balls.empty() && prevCount > 0)
            audioProcessor.releaseAllVoices();
    }

    // 同步 DAW 播放状态（仅用于界面重绘判断；发声相关的判断都在音频线程做）
    const bool playing = w.followDaw ? audioProcessor.isPlaying() : w.manualPlaying;

    // ★ 物理 / 琶音 / 预览释放 已全部搬到音频线程
    //   （BounceAudioProcessor::processBlock），球在窗口关闭后照样运动、照样撞壁。
    //   编辑器定时器只负责：刷新属性面板、列表、重绘节流、MIDI flush。
    //   ⚠️ 不要在这里推进物理，否则音频线程与 UI 线程会同时改球的位置。

    // 刷新待发送的 MIDI 消息（保证在消息线程）
    audioProcessor.flushMidi();

    //=== 重绘节流 =========================================================
    //  圆形容器那一整套（144 段描边路径 + 水波纹）是 UI 线程最贵的一笔。
    //  停止播放、水纹也散尽之后画面就是静止的，没必要继续 60fps 空转——
    //  只在"在跑 / 还有水纹 / 状态刚变过"时才 repaint。
    //  ⚠️ 判断"还有没有水纹"必须走线程安全查询：ripples 由音频线程写，
    //     这里直接读 .empty() 是数据竞争（且可能撕裂 vector 内部指针）。
    if (playing || w.hasRipples() || panelDirty)
    {
        panelDirty = false;
        ballPanel.repaint();
    }
}

//==============================================================================
// 滚轮调节：所有下拉框 + 数值控件都支持"鼠标悬停时滚轮微调"
//   向上滚 → 下一个 / 更大；向下滚 → 上一个 / 更小
//   八度分组标题（addSectionHeading）会被自动跳过，不会停在标题上
//==============================================================================
bool BounceAudioProcessorEditor::stepComboBox (juce::ComboBox& box, int dir)
{
    if (! box.isEnabled())
        return false;

    const int curId = box.getSelectedId();
    if (curId <= 0)
        return false;

    // 从当前 id 出发按方向找"下一个真实选项"，最多找 64 次防死循环
    int next = curId;
    for (int guard = 0; guard < 64; ++guard)
    {
        next += dir;
        if (next < 1)
            return false;
        if (box.indexOfItemId (next) >= 0)      // 命中真实选项
            break;
        if (next > 4096)                        // 越界保护
            return false;
    }

    if (box.indexOfItemId (next) < 0 || next == curId)
        return false;

    box.setSelectedId (next);   // 发送通知 → 触发各框自己的 onChange
    return true;
}

void BounceAudioProcessorEditor::stepSlider (juce::Slider& s, int dir)
{
    if (! s.isEnabled())
        return;

    const double step = juce::jmax (1.0e-6, s.getInterval());
    s.setValue (s.getValue() + dir * step);   // 会触发 onValueChange
}

//==============================================================================
void BounceAudioProcessorEditor::applySoloMuteToSelection (bool state, bool isSolo)
{
    auto& w = audioProcessor.getWorld();

    auto apply = [&] (int idx)
    {
        if (idx < 0 || idx >= (int) w.balls.size()) return;
        auto& ball = w.balls[(size_t) idx];
        // S 和 M 互斥：点亮一个就把另一个熄掉。
        //  ⚠️ 不互斥的话会出现"S 和 M 同时亮"的球 —— 语义上等于"点了 Solo 但被自己 Mute"，
        //     算下来该球不发声，DAW 用户看到两颗灯都亮会以为出错。互斥后语义一目了然：
        //       S 亮 = 只留它（其余球音量→0）
        //       M 亮 = 只灭它（音量→0）
        //       S 和 M 全灭 = 正常发声
        if (isSolo) { ball.solo = state; if (state) ball.mute = false; }
        else        { ball.mute = state; if (state) ball.solo = false; }
    };

    if (w.selection.empty())
        apply (w.selected);
    else
        for (int idx : w.selection)
            apply (idx);

    // ★ 关键：这里**绝对不能**调 releaseAllVoices()！
    //   Solo / Mute 的语义是"把其他声部的音量减到 0"，不是"停止发声"。
    //   releaseAllVoices 会把所有 voice 推进 release 段 —— 于是取消 S / M 之后
    //   那些声部只能从头重新起音（Pad 的渐入、琶音的相位全部重来），
    //   这正是用户听到的"其他声部重新开始播放"。
    //
    //   正确做法：把每颗球的可听状态写成"闸门"，音频线程据此把对应 voice 的
    //   muteGain 平滑到 0 / 1 —— 包络照常走完，取消静音时从当前电平无缝淡回。
    pushGatesToProcessor();

    ballStrip.refresh();
    refreshBallControls();
}

//==============================================================================
// 把「每颗球此刻是否可听」全量推给音频线程（见 BounceAudioProcessor::setBallGate）
//  · balls 每颗球都要推一次：内部位图是"这一颗球的开/关"，不推的球会被复位成 1
//    —— 所以任何会改动 solo / mute / 球列表的地方，都要调一次这个函数。
void BounceAudioProcessorEditor::pushGatesToProcessor()
{
    auto& w = audioProcessor.getWorld();

    const int n = (int) w.balls.size();
    for (int i = 0; i < n; ++i)
        audioProcessor.setBallGate (i, ! w.balls[(size_t) i].silenced (w.anySolo()));

    // 多出来的槽位（刚删完球）显式复位，免得残留静音
    for (int i = n; i < World::maxBalls; ++i)
        audioProcessor.setBallGate (i, true);
}

//==============================================================================
void BounceAudioProcessorEditor::refreshFromProcessor()
{
    auto& w = audioProcessor.getWorld();

    progBox.setSelectedItemIndex (juce::jlimit (0, ChordEngine::numProgs - 1, w.engine.prog),
                                  juce::dontSendNotification);
    scaleBox.setSelectedItemIndex (juce::jlimit (0, ChordEngine::numScales - 1, w.engine.scale),
                                   juce::dontSendNotification);
    keyBox.setSelectedItemIndex (juce::jlimit (0, 11, w.engine.key), juce::dontSendNotification);

    previewToggle.setToggleState (w.preview, juce::dontSendNotification);
    dawToggle.setToggleState (w.followDaw, juce::dontSendNotification);
    bpmSlider.setValue (w.manualBpm, juce::dontSendNotification);
    bpmSlider.setEnabled (! w.followDaw);
    runButton.setEnabled (! w.followDaw);
    runButton.setToggleState (w.manualPlaying, juce::dontSendNotification);
    runButton.setButtonText (w.manualPlaying ? "Stop" : "Play");

    refreshMidiList();
    refreshBallControls();

    // 工程刚被恢复（球列表 + 每球的 solo/mute 都换了）→ 闸门位图必须重推一遍
    pushGatesToProcessor();
}

//==============================================================================
bool BounceAudioProcessorEditor::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        // 兜底：如果鼠标正抓着球时按了 Del，held 标志会留在世界里的球上
        // （被删的那颗球没事，但同批多选的其他球会永远不再运动）→ 先全部解除
        audioProcessor.getWorld().releaseAllHeld();
        audioProcessor.getWorld().removeSelected();
        pushGatesToProcessor();
        refreshBallControls();
        return true;
    }
    return false;
}

//==============================================================================
void BounceAudioProcessorEditor::paint (juce::Graphics& g)
{
    // 扁平背景：整块纯色面（去掉纵向渐变）
    //   氛围感交给"画布比背景亮、卡片比画布亮"这三层面色差来表达，
    //   不再靠渐变——渐变在浅色主题里会显脏、也削弱扁平质感
    g.setColour (Ui::bgTop);
    g.fillRect (getLocalBounds());

    // 底部属性区：压一层更暖的雾面（比背景深一档），形成"承托"感而不是画线
    if (accentOn && ! propArea.isEmpty())
    {
        auto band = propArea.toFloat().expanded (6.0f);
        g.setColour (accentCol.withAlpha (0.10f));
        g.fillRoundedRectangle (band, 10.0f);
    }
    else if (! propArea.isEmpty())
    {
        auto band = propArea.toFloat().expanded (6.0f);
        g.setColour (Ui::bgBottom.withAlpha (0.75f));
        g.fillRoundedRectangle (band, 10.0f);
    }
}

void BounceAudioProcessorEditor::resized()
{
    //=== 布局规格：4/8/16/20 间距体系（间距体现从属关系） ==================
    //   同一组内间距（8）< 组与组之间（16）→ 不用画线也能看出分组
    //   上一版 24/12/24 偏松，整页发散发空 → 整体收紧一档
    auto area = getLocalBounds().reduced (20);   // 页面留白 20
    const int gap    = 8;     // 组内：相邻控件
    const int secGap = 16;    // 组间：区块分隔

    //--- 顶栏第 1 行：和弦进行 / 调式 / 调（撑满整行） ---
    auto row1 = area.removeFromTop (32);
    keyBox.setBounds   (row1.removeFromRight (152));
    row1.removeFromRight (gap);
    scaleBox.setBounds (row1.removeFromRight (216));
    row1.removeFromRight (gap);
    progBox.setBounds  (row1);

    area.removeFromTop (gap);

    //--- 顶栏第 2 行：MIDI out / Preview / Sync DAW ... BPM / Stop（两端撑满） ---
    auto row2 = area.removeFromTop (32);
    // 右组（传输）宽度固定，左组里只有 midiBox 是弹性的
    // → 中间空隙自动等于 secGap，不用手算，也不会溢出
    runButton.setBounds (row2.removeFromRight (68));
    row2.removeFromRight (gap);
    bpmSlider.setBounds (row2.removeFromRight (112));
    row2.removeFromRight (4);
    bpmLabel.setBounds  (row2.removeFromRight (34));

    row2.removeFromRight (secGap);   // 左组 / 右组之间 = 组间距 16

    const int fixedLeft = gap + 92 + gap + 110;   // Preview + Sync DAW（含组内间距）
    midiBox.setBounds       (row2.removeFromLeft (row2.getWidth() - fixedLeft));
    row2.removeFromLeft (gap);
    previewToggle.setBounds (row2.removeFromLeft (92));
    row2.removeFromLeft (gap);
    dawToggle.setBounds     (row2.removeFromLeft (110));

    //--- 组间留白取代分隔线：16px 已足够分组，不需要再画线 ---
    area.removeFromTop (secGap);

    //--- 底部整块：小球条 + 属性三行 + 按钮行 ---
    //  92(卡片条) + 16 + 32(行A) + 8 + 32(行B) + 8 + 32(行C:Arp) + 16 + 32(按钮行) = 268
    auto bottom = area.removeFromBottom (268);
    area.removeFromBottom (secGap);   // 画布底边留白 16（与顶部对称）

    // 小球卡片条（92 = 卡片 74 + 余量，滚动条落在卡片下方不遮挡）
    ballStrip.setBounds (bottom.removeFromTop (92));

    bottom.removeFromTop (secGap);   // 卡片条与属性区是两组 → 16

    //=== 属性区：两行共用同一套 5 列栅格，列边完全对齐 ======================
    //  栅格：5 等列 + 4 个 gap；每列 = 「44px 右对齐标签 + 控件撑满余下宽度」
    //  行A：Follow(占1列) | Note(1列) | oct(1列) | Chord(占2列，和弦名最长)
    //  行B：Tone(1列) | Rate(1列) | Ch(1列) | Solo(1列) | Mute(1列)
    //  → 两行的列分割线严格落在同一 x 上，纵向对齐、横向撑满
    //========================================================================
    //  36px 标签（最宽 "Chord"）+ 4px 间距，其余全给控件 —— 让 "Music Box"
    //  这类较长音色名能以统一字号完整显示，不被 JUCE 横向压扁
    const int labelW = 36;
    const int cols   = 5;

    const int propTop = bottom.getY();
    const int propW   = bottom.getWidth();
    const int unit    = (propW - (cols - 1) * gap) / cols;   // 单列宽（整除）
    // 除不尽的余数（680 → 每列 129 余 3）并进最后一列，
    // 否则属性区右边缘会比上面两行短几个 px，破坏整页的右对齐
    const int extra   = propW - (cols * unit + (cols - 1) * gap);

    // 第 i 列的起点 x / 宽度
    auto colX = [&] (int i) { return bottom.getX() + i * (unit + gap); };
    auto colW = [&] (int i) { return unit + (i == cols - 1 ? extra : 0); };

    // 行A：Follow | Note | oct | Chord(2列) | Prob
    //  ⚠️ 行A 现在有 5 个逻辑位（Chord 要双倍宽），但栅格只有 5 列。
    //     解决办法：把栅格再对半分（半列 = half），Chord 占 2 个半列、其余各占 1 个，
    //     正好 1+1+1+2+1 = 6 半列 = 3 整列 …… 不够。
    //   → 实际用"以 half 为单位重新分配"：总宽 6 个 half 位，
    //     且 Follow 不需要标签（Toggle 自带文字），省下的宽度转给 Prob。
    auto rA = bottom.removeFromTop (32);
    const int yA = rA.getY();

    // 行A 自己的 6 份栅格（gap 沿用全局 8）
    const int aSlots = 6;
    const int aUnit  = (propW - (aSlots - 1) * gap) / aSlots;
    const int aExtra = propW - (aSlots * aUnit + (aSlots - 1) * gap);
    auto aX = [&] (int i) { return bottom.getX() + i * (aUnit + gap); };
    auto aW = [&] (int i) { return aUnit + (i == aSlots - 1 ? aExtra : 0); };

    followToggle.setBounds (aX (0), yA, aW (0), rA.getHeight());

    pitchLabel.setBounds (aX (1), yA, labelW, rA.getHeight());
    pitchBox.setBounds   (aX (1) + labelW + 4, yA, aW (1) - labelW - 4, rA.getHeight());

    octLabel.setBounds (aX (2), yA, labelW, rA.getHeight());
    octaveSlider.setBounds (aX (2) + labelW + 4, yA, aW (2) - labelW - 4, rA.getHeight());

    // Chord 占第 3、4 两格，但收窄 24px 让给 Prob（"100%" 文字不能被截断）
    constexpr int chordShrink = 24;
    chordLabel.setBounds (aX (3), yA, labelW, rA.getHeight());
    toneBox.setBounds (aX (3) + labelW + 4, yA,
                       aX (5) - chordShrink - (aX (3) + labelW + 4), rA.getHeight());

    // Prob 占最后一格 + 从 Chord 借来的 24px
    probLabel.setBounds  (aX (5) - chordShrink, yA, labelW, rA.getHeight());
    probSlider.setBounds (aX (5) - chordShrink + labelW + 4, yA,
                          aW (5) + chordShrink - labelW - 4, rA.getHeight());

    bottom.removeFromTop (gap);   // 属性两行属同组 → 8

    // 行B（与行A同栅格，列边对齐）
    auto rB = bottom.removeFromTop (32);
    const int yB = rB.getY();
    toneLabel.setBounds (colX (0), yB, labelW, rB.getHeight());
    ballTimbreBox.setBounds (colX (0) + labelW + 4, yB, colW (0) - labelW - 4, rB.getHeight());
    rateLabel.setBounds (colX (1), yB, labelW, rB.getHeight());
    rateBox.setBounds   (colX (1) + labelW + 4, yB, colW (1) - labelW - 4, rB.getHeight());
    chLabel.setBounds (colX (2), yB, labelW, rB.getHeight());
    chanBox.setBounds (colX (2) + labelW + 4, yB, colW (2) - labelW - 4, rB.getHeight());
    soloLabel.setBounds (colX (3), yB, labelW, rB.getHeight());
    soloButton.setBounds (colX (3) + labelW + 4, yB, colW (3) - labelW - 4, rB.getHeight());
    muteLabel.setBounds (colX (4), yB, labelW, rB.getHeight());
    muteButton.setBounds (colX (4) + labelW + 4, yB, colW (4) - labelW - 4, rB.getHeight());

    bottom.removeFromTop (gap);   // 属性行之间属同组 → 8

    // 行C：琶音器（与行A/行B共用同一栅格，列边严格对齐）
    //  Arp(占1列) | Rate(1列) | Mode(1列) | Oct(1列) | Step(1列)
    auto rC = bottom.removeFromTop (32);
    const int yC = rC.getY();
    arpToggle.setBounds (colX (0), yC, colW (0), rC.getHeight());
    arpRateLabel.setBounds (colX (1), yC, labelW, rC.getHeight());
    arpRateBox.setBounds   (colX (1) + labelW + 4, yC, colW (1) - labelW - 4, rC.getHeight());
    arpModeLabel.setBounds (colX (2), yC, labelW, rC.getHeight());
    arpModeBox.setBounds   (colX (2) + labelW + 4, yC, colW (2) - labelW - 4, rC.getHeight());
    arpOctLabel.setBounds  (colX (3), yC, labelW, rC.getHeight());
    arpOctSlider.setBounds (colX (3) + labelW + 4, yC, colW (3) - labelW - 4, rC.getHeight());
    arpStepLabel.setBounds (colX (4), yC, labelW, rC.getHeight());
    arpStepSlider.setBounds (colX (4) + labelW + 4, yC, colW (4) - labelW - 4, rC.getHeight());

    // 属性区背景范围（随球变色的淡色底）
    propArea = { bottom.getX(), propTop, propW, rC.getBottom() - propTop };

    bottom.removeFromTop (secGap);   // 属性区与操作按钮是两组 → 16

    // 行D：4 个等宽按钮撑满整行（间隔同样 8）
    auto rD = bottom.removeFromTop (32);
    const int btnW = (rD.getWidth() - 3 * gap) / 4;
    addCenterButton.setBounds (rD.removeFromLeft (btnW));
    rD.removeFromLeft (gap);
    deleteButton.setBounds (rD.removeFromLeft (btnW));
    rD.removeFromLeft (gap);
    clearButton.setBounds (rD.removeFromLeft (btnW));
    rD.removeFromLeft (gap);
    ensembleButton.setBounds (rD);

    //--- 中间画布：上下各留白，容器在其中上下居中 -------------------------
    //  画布区域 = area 减去上下两段留白，BallPanel 内部再按 min(w,h) 取正圆，
    //  这样圆形容器不会顶满上下边缘，四周都有呼吸感
    //  留白取 min(32, area高*5%)：窗口变矮时按比例缩，不把容器压没了
    const int canvasPad = juce::jlimit (10, 32, area.getHeight() * 5 / 100);
    area.removeFromTop (canvasPad);
    area.removeFromBottom (canvasPad);
    ballPanel.setBounds (area);
}
