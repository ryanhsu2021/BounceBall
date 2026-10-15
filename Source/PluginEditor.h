#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <functional>
#include "PluginProcessor.h"

//==============================================================================
// ★ 高 DPI 清晰度：把字号对齐到"整数物理像素"
//
//   Windows 上插件 UI 发虚的主因（尤其在 125% / 150% 这类非整数缩放下）：
//   字号 × 缩放 = 物理像素数，一旦落在非整数上（如 14px × 1.25 = 17.5px），
//   字体 hinting 失效、字形被抗锯齿抹开，看起来就是"糊"。
//   Standalone 之所以清晰，是因为它是独立进程，能拿到正确的 DPI 并按整数渲染。
//
//   解法（业界通用）：反推一个"乘回去正好是整数物理像素"的逻辑字号。
//   例：scale=1.25 → 14.0 的物理值 17.5 → 取整 18 → 逻辑字号 18/1.25 = 14.4
//      → 实际渲染 18 物理像素，整数、锐利。
inline float snapFontSize (float logicalSize, float scale)
{
    if (scale <= 0.0f || logicalSize <= 0.0f)
        return logicalSize;

    const float physical = logicalSize * scale;
    const float snapped  = std::round (physical);
    return juce::jmax (1.0f, snapped / scale);
}

//==============================================================================
// ★ 配色令牌层（Design Tokens）
//
//   设计目标：扁平质感 · 设计感 · 氛围感。三条硬规则：
//     ① 扁平 = 一个面一个色块。全界面不再用"上亮下暗"的拟物渐变，
//        立体感全靠「同色系不同明度的面 + 1px 发丝描边」表达。
//     ② 设计感 = 色彩有语义、有层级。中性色分 4 档（底/面/线/字），
//        语义色（Solo/Mute/Arp/Run/Ensemble）各自只承担一个含义。
//     ③ 氛围感 = 克制。背景不是纯白，而是带极轻暖调的"雾白"；
//        球色一律低饱和，靠柔光而非高饱和抢视线。
//
//   ⚠️ 改色请只改这里，不要在绘制点写 0xffXXXXXX 硬编码。
//==============================================================================
namespace Ui
{
    //--- 中性层：从底到字，明度逐级加深 ---
    static const juce::Colour bgTop      { 0xfff7f6f3 };  // 窗口背景（极轻暖雾白）
    static const juce::Colour bgBottom   { 0xfff1efe9 };  // 窗口背景（略带暖灰）
    static const juce::Colour surface    { 0xfffdfcfa };  // 卡片 / 面板面
    static const juce::Colour surfaceAlt { 0xfff5f3ee };  // 次级面（选中、轨道）
    static const juce::Colour canvas     { 0xfffcfbf8 };  // 圆形容器填充

    static const juce::Colour line       { 0xffe4e0d8 };  // 发丝描边（控件）
    static const juce::Colour lineSoft   { 0xffeceae3 };  // 更浅的线（卡片、水波）

    //--- 文字层：三档，靠明度而非字重区分主次 ---
    static const juce::Colour ink        { 0xff35342f };  // 主文字
    static const juce::Colour inkMid     { 0xff56544e };  // 次主文字
    static const juce::Colour inkSoft    { 0xff8b8880 };  // 标签 / 说明字

    //--- 语义层：每个颜色只对一个含义 ---
    static const juce::Colour accent     { 0xff7d9fc4 };  // 主强调（= 首个球色，雾蓝）
    static const juce::Colour solo       { 0xffcfa44e };  // Solo：琥珀
    static const juce::Colour mute       { 0xffb08989 };  // Mute：陶粉
    static const juce::Colour arp        { 0xff8299bb };  // Arp：雾蓝灰
    static const juce::Colour run        { 0xff7fa08d };  // 运行中：灰绿
    static const juce::Colour ensemble   { 0xff9d92bd };  // Ensemble：雾紫
}

//==============================================================================
// 统一外观：扁平化浅色主题
//   面 = 纯色（无渐变）｜线 = 1px 发丝｜字 = 三档中性灰
//   立体感来自"面色差 + 描边"，不来自阴影或渐变
class BounceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BounceLookAndFeel()
    {
        // ★ 用系统 UI 字体（Windows 上是 Segoe UI）：它是为屏幕渲染做过 hinting 优化的，
        //   小字号下比 JUCE 默认字体明显锐利。跨平台各自用本系统默认。
       #if JUCE_WINDOWS
        setDefaultSansSerifTypefaceName ("Segoe UI");
       #elif JUCE_MAC
        setDefaultSansSerifTypefaceName ("Helvetica Neue");
       #endif

        // 窗口 / 基础
        setColour (juce::ResizableWindow::backgroundColourId, Ui::bgTop);

        // 下拉框（统一发丝级描边：全界面同一种 hairline 色）
        setColour (juce::ComboBox::backgroundColourId,  juce::Colours::white);
        setColour (juce::ComboBox::outlineColourId,     Ui::line);
        setColour (juce::ComboBox::arrowColourId,       Ui::inkSoft);
        setColour (juce::ComboBox::textColourId,        Ui::ink);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colours::white);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Ui::surfaceAlt);
        setColour (juce::PopupMenu::textColourId,       Ui::ink);
        setColour (juce::PopupMenu::highlightedTextColourId, Ui::ink);
        setColour (juce::PopupMenu::headerTextColourId, Ui::inkSoft);

        // 按钮：默认白面；悬停/按下由 drawButtonBackground 处理（纯色，不用渐变）
        setColour (juce::TextButton::buttonColourId,   juce::Colours::white);
        setColour (juce::TextButton::buttonOnColourId, Ui::surfaceAlt);
        setColour (juce::TextButton::textColourOffId,  Ui::inkMid);
        setColour (juce::TextButton::textColourOnId,   Ui::ink);

        // 开关
        setColour (juce::ToggleButton::textColourId,        Ui::inkMid);
        setColour (juce::ToggleButton::tickColourId,        Ui::run);
        setColour (juce::ToggleButton::tickDisabledColourId, juce::Colour (0xffc9c5bc));

        // 标签 / 滑块 / 滚动条（描边统一为 hairline 色）
        setColour (juce::Label::textColourId,                 Ui::inkSoft);
        setColour (juce::Slider::textBoxTextColourId,         Ui::ink);
        setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::white);
        setColour (juce::Slider::textBoxOutlineColourId,      Ui::line);
        setColour (juce::Slider::textBoxHighlightColourId,    Ui::surfaceAlt);
        setColour (juce::Slider::backgroundColourId,          Ui::lineSoft);
        setColour (juce::ScrollBar::backgroundColourId,       juce::Colours::transparentBlack);
        setColour (juce::ScrollBar::thumbColourId,            juce::Colour (0xffcbc7be));
    }

    //=== 字号层级：控件正文 15px（2026-10-15 由 14 整体上调一档），次要说明 13px，标题另用 ===
    //  不做"全部同一档"——层级拉开才显专业（大小本身就是一种分组）
    // 字号都过一遍 snapFontSize：保证物理像素是整数，避免高 DPI 下发虚
    juce::Font getTextButtonFont (juce::TextButton& b, int) override
    { return { juce::FontOptions (snapFontSize (15.0f, b.getDesktopScaleFactor())) }; }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    { return { juce::FontOptions (snapFontSize (15.0f, box.getDesktopScaleFactor())) }; }

    juce::Font getPopupMenuFont() override
    {
        // 弹出菜单没有组件可问，退回全局缩放
        const float s = juce::Desktop::getInstance().getGlobalScaleFactor();
        return { juce::FontOptions (snapFontSize (15.0f, s > 0.0f ? s : 1.0f)) };
    }

    // 小标签统一 12px（也做物理像素对齐）
    //  ⚠️ 不要在构造里 setFont —— 那时组件还没挂到桌面，getDesktopScaleFactor() 拿不到真实值
    juce::Font getLabelFont (juce::Label& l) override
    { return { juce::FontOptions (snapFontSize (13.0f, l.getDesktopScaleFactor())) }; }

    //=== 按钮文字：彻底居中 ===
    // 默认实现会按 cornerSize 算左右缩进，小方形按钮（如 +/-）的字会被挤到角落
    void drawButtonText (juce::Graphics& g, juce::TextButton& btn, bool, bool) override
    {
        g.setFont (getTextButtonFont (btn, btn.getHeight()));
        g.setColour (btn.findColour (btn.getToggleState() ? juce::TextButton::textColourOnId
                                                          : juce::TextButton::textColourOffId)
                        .withMultipliedAlpha (btn.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (btn.getButtonText(), btn.getLocalBounds().reduced (2, 0),
                          juce::Justification::centred, 1);
    }

    //=== 开关：扁平方形勾选框（纯色 + 发丝边，无渐变） ===
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& t,
                           bool, bool) override
    {
        auto bounds = t.getLocalBounds().toFloat();
        const float box = 16.0f;
        auto boxRect = juce::Rectangle<float> (bounds.getX(), bounds.getCentreY() - box * 0.5f,
                                               box, box);
        const float r = box * 0.5f;   // 全圆角 → 扁平"圆角方"观感

        const bool on = t.getToggleState();
        auto tickCol = t.findColour (juce::ToggleButton::tickColourId);
        if (! t.isEnabled()) tickCol = t.findColour (juce::ToggleButton::tickDisabledColourId);

        // 勾选框：选中 = 纯色填充（扁平）；未选 = 白面 + 发丝边
        g.setColour (on ? tickCol : juce::Colours::white);
        g.fillRoundedRectangle (boxRect, r);
        g.setColour (on ? tickCol.darker (0.10f) : Ui::line);
        g.drawRoundedRectangle (boxRect.reduced (0.5f), r - 0.5f, 1.0f);

        if (on)
        {
            juce::Path check;
            const float cx = boxRect.getCentreX(), cy = boxRect.getCentreY();
            check.startNewSubPath (cx - 3.2f, cy + 0.1f);
            check.lineTo (cx - 0.9f, cy + 2.4f);
            check.lineTo (cx + 3.4f, cy - 2.3f);
            g.setColour (juce::Colours::white);
            g.strokePath (check, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // 文字：15px（与下拉框同档），禁用时减淡
        auto textArea = bounds.withLeft (boxRect.getRight() + 8.0f);
        g.setFont (juce::FontOptions (snapFontSize (15.0f, t.getDesktopScaleFactor())));
        auto textCol = t.findColour (juce::ToggleButton::textColourId);
        if (! t.isEnabled()) textCol = textCol.withAlpha (0.40f);
        g.setColour (textCol);
        g.drawText (t.getButtonText(), textArea, juce::Justification::centredLeft);
    }

    //=== 滚动条：无轨道、4px 圆角细条，悬停加深 ===
    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&,
                        int x, int y, int width, int height,
                        bool isVertical, int thumbStart, int thumbSize,
                        bool mouseOver, bool mouseDown) override
    {
        if (thumbSize <= 0) return;
        juce::Rectangle<float> thumb;
        if (isVertical)
            thumb = { (float) x + (width - 4.0f) * 0.5f, (float) thumbStart, 4.0f, (float) thumbSize };
        else
            thumb = { (float) thumbStart, (float) y + (height - 4.0f) * 0.5f, (float) thumbSize, 4.0f };

        auto col = juce::Colour (0xffd0ccc3);
        if (mouseOver || mouseDown) col = Ui::inkSoft.withAlpha (0.55f);
        g.setColour (col);
        g.fillRoundedRectangle (thumb, 2.0f);
    }

    //=== 下拉框：白面（纯色，扁平） + 发丝边 + 细线箭头 ===
    //   描边色可被单框覆写为球色（属性区随球变色时用）
    void drawComboBox (juce::Graphics& g, int w, int h, bool,
                       int, int, int, int, juce::ComboBox& cb) override
    {
        auto b = juce::Rectangle<float> (0.5f, 0.5f, w - 1.0f, h - 1.0f);
        g.setColour (cb.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (b, radius);
        g.setColour (cb.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (b, radius, 1.0f);

        // 箭头：细线 V 形（比实心三角更轻、更扁平）
        juce::Path arrow;
        const float ax = w - 15.0f, ay = h * 0.5f;
        arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
        arrow.lineTo (ax, ay + 2.2f);
        arrow.lineTo (ax + 4.0f, ay - 2.0f);
        g.setColour (Ui::inkSoft);
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    //=== 下拉文字区域：JUCE 默认预留"宽-30"（左1 右29）太浪费，长音色名会被压扁 ===
    //   这里收窄为"左5 右20"（箭头已移到 w-12±4.5，文字不会碰到），配合加宽后的
    //   控件（81→89），"Music Box" 等长名字能以统一 14px 字号完整显示
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (5, 1, box.getWidth() - 25, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    //=== 按钮：纯色面（扁平）+ 发丝边 ===
    //   状态靠"面色明度"表达：默认白 → 悬停浅灰 → 按下再深一档
    //   不再用上下渐变，扁平但有明确的交互反馈
    void drawButtonBackground (juce::Graphics& g, juce::Button& btn, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        auto b = btn.getLocalBounds().toFloat().reduced (0.5f);
        auto base = btn.findColour (btn.getToggleState() ? juce::TextButton::buttonOnColourId
                                                         : juce::TextButton::buttonColourId);
        if (down)             base = base.darker (0.07f);
        else if (highlighted) base = base.darker (0.035f);

        g.setColour (base);
        g.fillRoundedRectangle (b, radius);
        g.setColour (down ? Ui::inkSoft.withAlpha (0.45f) : Ui::line);
        g.drawRoundedRectangle (b, radius, 1.0f);
    }

    // 全界面统一圆角（4/6/8 混用会显得不讲究）
    static constexpr float radius = 6.0f;
};

//==============================================================================
// 圆形容器画布：绘制大圆 + 小球
// 交互：双击空白加球 / 点球选中拖动 / 拖空白框选 / Ctrl+点球多选
class BallPanel : public juce::Component
{
public:
    BallPanel (BounceAudioProcessor& p) : processor (p) {}

    // 圆形容器半径：按画布较短边取正圆，并留出内边距
    //   绘制与鼠标命中都用它，保证"看到的圆"就是"点得到的圆"
    static float radiusFor (juce::Rectangle<float> bounds)
    {
        constexpr float inner = 10.0f;   // 圆与画布边缘的内边距（呼吸感）
        return juce::jmax (20.0f, juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - inner);
    }

    void paint (juce::Graphics& g) override
    {
        auto& world = processor.getWorld();
        auto bounds = getLocalBounds().toFloat();
        // 圆形容器在画布内居中：圆心取 bounds 正中，半径按较短边算并留出内边距，
        // 这样窗口宽高比变化时圆始终是正圆、且上下左右都留白
        const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
        const float R = radiusFor (bounds);

        //=== 球的位姿快照（线程安全）========================================
        //  ⚠️ 音频线程每块都在写 balls[i].pos（物理推进），UI 线程若直接遍历
        //     world.balls 读 pos 就是数据竞争（撕裂读 → 球闪现/抖动）。
        //     这里用 try-lock 一次拷出"画这一帧需要的字段"，之后不再碰 world.balls。
        //     抢不到锁（音频线程正持有）→ 本帧不画球，下一帧 16ms 后再来，
        //     绝不阻塞 UI 线程、更不会阻塞音频线程。
        struct BallVis { float x, y; juce::Colour colour; bool selected; };
        juce::Array<BallVis> vis;
        {
            const juce::SpinLock::ScopedTryLockType sl (world.uiLock);
            if (sl.isLocked())
            {
                vis.ensureStorageAllocated ((int) world.balls.size());
                for (int i = 0; i < (int) world.balls.size(); ++i)
                {
                    auto& b = world.balls[(size_t) i];
                    vis.add ({ b.pos.x, b.pos.y, b.colour, world.isSelected (i) });
                }
            }
        }

        // 容器填充：扁平纯色面（去掉径向渐变）
        //   氛围感改由"面比背景亮一档 + 发丝外缘"表达，
        //   这样球与光晕就是画面里唯一的色彩来源，更聚焦
        g.setColour (Ui::canvas);
        g.fillEllipse (cx - R, cy - R, R * 2.0f, R * 2.0f);

        //=== 容器外缘：基础圆 + 碰撞水波形变（撞点凸起、随时间扩散回弹） ===
        const double nowSec = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        //  ⚠️ 水纹由音频线程 addRipple 写入 → 这里取线程安全快照（锁内拷贝 + 清过期），
        //     之后整个 paint 只用这份快照，不再碰 world.ripples
        //     （原来在 paint 里直接 erase/遍历 → 与音频线程 push_back 数据竞争）
        const auto ripples = world.snapshotRipples (nowSec, 3.0);

        //=== 容器内部：撞点处向外扩散的水波（裁剪在容器圆内，只保留内部弧段） ===
        //  与容器外缘形变共用同一"生命周期 3.0s + 衰减 exp(-1.5t)"的时间底座，
        //  保证内外同步：外缘还在荡的时候，内部波纹也还在，不会各跑各的。
        {
            juce::Path clipCircle;
            clipCircle.addEllipse (cx - R + 1.0f, cy - R + 1.0f, (R - 1.0f) * 2.0f, (R - 1.0f) * 2.0f);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (clipCircle, juce::AffineTransform());

            const float lifeSec = 3.0f;   // 与外缘一致
            for (const auto& rp : ripples)
            {
                const float age = (float) (nowSec - rp.time);
                if (age < 0.0f || age > lifeSec) continue;

                // 扩散中心 = 碰撞点本身（归一化坐标 → 画布坐标）
                //  · 撞壁：撞点在容器圆周上（px/py 由角度算出，radius=1）
                //  · 球撞球：撞点在容器内部任意处（px/py 是两球接触点）
                //  ⚠️ 早前这里写过 `inR = R * 0.66f`（把中心往圆心内移 34%），
                //     导致波纹"不在碰撞位置发生"；已改为直接用 rp.px/rp.py。
                const float hx = cx + rp.px * R;
                const float hy = cy + rp.py * R;

                for (int k = 0; k < 4; ++k)
                {
                    const float ak = age - (float) k * 0.55f;   // 圈间隔拉到 0.55s（波与波分得开）
                    if (ak <= 0.0f) continue;

                    // 扩散范围：约 72 px/s —— 比原来更慢（92 → 72），但配合更长的
                    //   生命周期，第 1 圈到 3s 末仍能走约 216px ≈ 66% R；
                    //   strength 权重提高到 30：撞得重的波纹明显推得更开
                    const float rad = 3.0f + ak * (72.0f + rp.strength * 30.0f);

                    // 更柔的过渡：
                    //  · 渐入拉长到 ~0.14s（原来 0.06s 太急）
                    //  · 淡出走平滑的 pow 曲线，不用 exp 的陡降
                    //  · 峰值压到 0.72，整体更轻
                    const float fadeIn  = juce::jmin (1.0f, ak / 0.14f);
                    const float fadeOut = std::pow (1.0f - juce::jmin (1.0f, ak / 3.0f), 1.8f);
                    float alpha = 0.72f * fadeIn * fadeOut;
                    alpha *= 1.0f - (float) k * 0.20f;
                    if (alpha < 0.02f) continue;

                    // 与容器边缘同一质感的细线；随扩散极缓慢变细（0.2 → 0.8 非常慢）
                    g.setColour (Ui::line.withAlpha (juce::jmin (1.0f, alpha)));
                    g.drawEllipse (hx - rad, hy - rad, rad * 2.0f, rad * 2.0f,
                                   juce::jmax (0.8f, 1.3f - ak * 0.12f));
                }
            }
        }

        const int segs = 144;
        juce::Path edge;
        for (int s = 0; s <= segs; ++s)
        {
            const float th = s / (float) segs * juce::MathConstants<float>::twoPi;
            float dr = 0.0f;
            for (const auto& rp : ripples)
            {
                //  ⚠️ 只有"撞壁"才驱动外缘形变：撞点必在圆周上，用角度定位才成立。
                //     球撞球的撞点在容器内部，拿它的角度去推外缘纯属瞎猜，
                //     会让容器边缘毫无理由地乱抖 → 必须跳过。
                if (! rp.fromEdge) continue;
                const float age = (float) (nowSec - rp.time);
                if (age < 0.0f || age > 3.0f) continue;
                // 角度差归到 [-π, π]
                float dth = th - rp.angle;
                dth = std::fmod (dth + juce::MathConstants<float>::pi,
                                 juce::MathConstants<float>::twoPi);
                if (dth < 0) dth += juce::MathConstants<float>::twoPi;
                dth -= juce::MathConstants<float>::pi;

                const float window = std::exp (dth * dth / (-2.0f * (0.30f + age * 1.4f) * (0.30f + age * 1.4f)));
                // 撞点鼓起 → 反向回弹 → 缓慢衰减：行波 + 阻尼（幅度加大、衰减变慢）
                dr += rp.strength * 17.0f * std::exp (age * -1.5f)
                    * std::cos (6.0f * dth - age * 7.0f) * window;
            }
            const float rr = R + dr;
            const float px = cx + std::cos (th) * rr;
            const float py = cy + std::sin (th) * rr;
            if (s == 0) edge.startNewSubPath (px, py);
            else        edge.lineTo (px, py);
        }
        edge.closeSubPath();
        g.setColour (Ui::line);
        g.strokePath (edge, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // 当前和弦名（画在圆内顶部）
        g.setColour (Ui::inkSoft);
        g.setFont (juce::FontOptions (snapFontSize (18.0f, getDesktopScaleFactor()),
                                      juce::Font::bold));
        g.drawText (world.engine.currentChordName(),
                    juce::Rectangle<int> (0, (int) (cy - R + 20), getWidth(), 26),
                    juce::Justification::centred);

        // 小球（数据来自上面的线程安全快照，这里不再碰 world.balls）
        for (int i = 0; i < vis.size(); ++i)
        {
            const auto& b = vis.getReference (i);
            const auto col = b.colour;
            const float br = Ball::radius * R;
            const float bx = cx + b.x * R;
            const float by = cy + b.y * R;

            // 柔光晕：两层半透明扩散（扁平化后去掉最外层，光晕更收敛、球更实）
            //   底色是浅雾白，光晕只需"一呼一吸"两档就够氛围，再多会显脏
            g.setColour (col.withAlpha (0.10f));
            g.fillEllipse (bx - br * 3.1f, by - br * 3.1f, br * 6.2f, br * 6.2f);
            g.setColour (col.withAlpha (0.19f));
            g.fillEllipse (bx - br * 1.95f, by - br * 1.95f, br * 3.9f, br * 3.9f);

            g.setColour (col);
            g.fillEllipse (bx - br, by - br, br * 2.0f, br * 2.0f);

            // 高光点：左上角一点柔白，扁平色块上也能"立"起来（不做渐变球）
            g.setColour (juce::Colours::white.withAlpha (0.34f));
            g.fillEllipse (bx - br * 0.62f, by - br * 0.66f, br * 0.62f, br * 0.62f);

            if (b.selected)
            {
                // 选中环：留白 + 同色环，比深灰环更轻、也更贴扁平体系
                g.setColour (col.darker (0.36f));
                g.drawEllipse (bx - br - 3.5f, by - br - 3.5f,
                               (br + 3.5f) * 2.0f, (br + 3.5f) * 2.0f, 1.6f);
            }
        }

        // 框选矩形
        if (draggingBox)
        {
            g.setColour (Ui::accent.withAlpha (0.12f));   // 低饱和选区
            g.fillRect (selectionRect);
            g.setColour (Ui::accent);
            g.drawRect (selectionRect, 1.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto& world = processor.getWorld();
        auto bounds = getLocalBounds().toFloat();
        const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
        const float R = radiusFor (bounds);

        auto toWorld = [&] (juce::Point<float> px)
        { return juce::Point<float> ((px.x - cx) / R, (px.y - cy) / R); };

        const auto p = toWorld (e.position);

        // 找被点中的球（上层优先）
        int hit = -1;
        float bestDist = juce::jmax (Ball::radius * 3.0f, 0.045f);   // 球的视觉半径变小，点选容差放宽
        for (int i = (int) world.balls.size() - 1; i >= 0; --i)
        {
            const float d = world.balls[(size_t) i].pos.getDistanceFrom (p);
            if (d < bestDist) { bestDist = d; hit = i; }
        }

        if (hit >= 0)
        {
            if (e.mods.isCtrlDown() || e.mods.isShiftDown())
                world.toggleSelected (hit);   // 加选 / 减选
            else if (! world.isSelected (hit))
                world.setSelected (hit);      // 单选

            dragBalls = true;
            dragStart = p;
            dragVel   = { 0.0f, 0.0f };

            // 标记"这一批球被抓住了" → 音频线程的物理不再推进它们的位置
            //  （见 World::advancePhysicsLocked 的 held 判断）。
            //  UI 线程持锁改，音频线程 try-lock 抢不到就跳过本块，安全。
            {
                const juce::SpinLock::ScopedLockType sl (world.uiLock);
                for (int idx : world.selection)
                    if (idx >= 0 && idx < (int) world.balls.size())
                        world.balls[(size_t) idx].held = true;
            }

            repaint();
            return;
        }

        // 双击空白 = 加球
        if (e.getNumberOfClicks() >= 2 && p.getDistanceFromOrigin() < 1.0f - Ball::radius)
        {
            world.addBall (p);
            repaint();
            return;
        }

        // 拖空白 = 框选
        if (! e.mods.isCtrlDown() && ! e.mods.isShiftDown())
            world.clearSelection();

        boxStartScreen = e.position;
        draggingBox = true;
        selectionRect = juce::Rectangle<float> (e.position, e.position);
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& world = processor.getWorld();
        auto bounds = getLocalBounds().toFloat();
        const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
        const float R = radiusFor (bounds);

        if (dragBalls)
        {
            const juce::Point<float> cur { (e.position.x - cx) / R, (e.position.y - cy) / R };
            const juce::Point<float> delta = cur - dragStart;

            if (delta.getDistanceFromOrigin() > 1.0e-6f)
                dragVel = delta;   // 最近一次的位移向量 → 松手时据此给速度

            // 拖拽期间也要持锁：音频线程可能正好在推进物理/做碰撞
            const juce::SpinLock::ScopedLockType sl (world.uiLock);

            for (int idx : world.selection)
                if (idx >= 0 && idx < (int) world.balls.size())
                {
                    auto np = world.balls[(size_t) idx].pos + delta;
                    const float lim = 1.0f - Ball::radius;
                    const float r = np.getDistanceFromOrigin();
                    if (r > lim) np = np / r * lim;
                    world.balls[(size_t) idx].pos = np;
                }

            dragStart = cur;
            repaint();
            return;
        }

        if (draggingBox)
        {
            selectionRect = juce::Rectangle<float> (boxStartScreen, e.position);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        auto& world = processor.getWorld();

        if (draggingBox)
        {
            // 把屏幕矩形转成世界坐标，选出矩形内的球
            auto bounds = getLocalBounds().toFloat();
            const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
            const float R = radiusFor (bounds);

            juce::Rectangle<float> worldRect ((selectionRect.getX() - cx) / R,
                                              (selectionRect.getY() - cy) / R,
                                              selectionRect.getWidth() / R,
                                              selectionRect.getHeight() / R);

            if (selectionRect.getWidth() > 4.0f || selectionRect.getHeight() > 4.0f)
            {
                for (int i = 0; i < (int) world.balls.size(); ++i)
                {
                    const auto& bp = world.balls[(size_t) i].pos;
                    if (worldRect.contains (bp))
                        world.addToSelection (i);
                }
            }
        }

        dragBalls = false;
        draggingBox = false;

        //=== 松手：把球交还给物理 ===========================================
        //  · 清 held → 音频线程下一块起重新推进它
        //  · 用"最后的拖拽方向"给一个速度：
        //      球被抓期间物理停摆，它的速度还是被抓之前那个值（可能是 0，也可能是
        //      反向的），直接用会看着很突兀。这里按松手瞬间的甩动方向重新赋速度，
        //      手感就是"把球甩出去"。
        //  · 停手不动（dragVel ≈ 0）→ 不赋值，保留原速度，球自己接着飘
        //  ⚠️ 遍历"所有球里 held 的那些"，而不是 world.selection：
        //     拖拽期间选中集合可能被改（比如另一路操作），按 selection 找会漏掉
        //     已经 held 的球 → 它永远静止。按 held 标志找才是自洽的。
        {
            const juce::SpinLock::ScopedLockType sl (world.uiLock);

            const float throwSpeed = 0.9f;         // 甩出去的初速（单位/秒）
            const float vlen = dragVel.getDistanceFromOrigin();
            const bool  fling = vlen > 0.002f;     // 有明显甩动才改速度

            for (auto& b : world.balls)
                if (b.held)
                {
                    b.held = false;
                    if (fling)
                        b.vel = dragVel / vlen * throwSpeed;
                }
        }

        dragVel = { 0.0f, 0.0f };
        repaint();
    }

private:
    BounceAudioProcessor& processor;

    bool dragBalls = false;
    juce::Point<float> dragStart;
    juce::Point<float> dragVel;      // 最近一次拖拽位移（松手甩出用）

    bool draggingBox = false;
    juce::Point<float> boxStartScreen;
    juce::Rectangle<float> selectionRect;

protected:
    //=== 兜底：拖拽被意外打断时解除 held ===================================
    //  mouseUp 不一定收得到（宿主夺走鼠标、窗口失焦、拖到别的控件上松开…
    //  虽然 JUCE 用 mouse capture 基本能保证收到，但不能赌）。
    //  漏掉的后果很隐蔽：那颗球永远不再运动，而且界面看不出异常。
    void parentHierarchyChanged() override
    {
        // 从窗口里被摘下来（编辑器关闭）→ 确保没有球卡在"被抓住"
        if (getParentComponent() == nullptr)
            processor.getWorld().releaseAllHeld();
    }

    void visibilityChanged() override
    {
        if (! isVisible())
            processor.getWorld().releaseAllHeld();
    }
};

//==============================================================================
// 底部小球条（内容层）：横向排列的"小球卡片"
//   每张卡片 = 色点 + 角色/音高 + 音色 + 通道；点击选中，Ctrl/Shift 加选
class BallStripChips : public juce::Component
{
public:
    explicit BallStripChips (BounceAudioProcessor& p) : processor (p) {}

    std::function<void()> onSelect;

    static constexpr int chipW = 88;
    static constexpr int gap   = 8;

    int preferredWidth() const
    {
        const int n = (int) processor.getWorld().balls.size();
        // 末尾多留一个 gap：最后一卡的描边/选中环不会被内容层右边缘裁掉
        return n > 0 ? (n + 1) * gap + n * chipW : 1;
    }

    void refresh() { repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto& world = processor.getWorld();
        const int n = (int) world.balls.size();

        // 当前 DPI 缩放：所有字号都按它对齐到整数物理像素（见 snapFontSize）
        const float uiScale = juce::jmax (0.5f, getDesktopScaleFactor());

        // 有球 Solo 时，其余球处于"音量→0"状态（见 Ball::targetGain）
        const bool anySolo = world.anySolo();

        for (int i = 0; i < n; ++i)
        {
            const auto& b = world.balls[(size_t) i];
            auto chip = chipBounds (i).toFloat();
            const bool sel = world.isSelected (i);
            const bool quiet = b.silenced (anySolo);   // 被 Solo 排除 或 Mute

            // 卡片底：扁平纯色面（去掉上下渐变）
            //   选中 = 浅一档的雾面；未选 = 白面。层级纯靠"面色差 + 描边"表达
            //   描边内缩到卡片内（0.5 + 描边半宽），保证粗描边不被自身裁剪
            const float strokeW = sel ? 1.6f : 1.0f;
            g.setColour (sel ? Ui::surfaceAlt : Ui::surface);
            g.fillRoundedRectangle (chip, 8.0f);
            g.setColour (sel ? b.colour.withAlpha (0.90f) : Ui::lineSoft);
            g.drawRoundedRectangle (chip.reduced (strokeW * 0.5f + 0.5f), 8.0f - strokeW * 0.5f, strokeW);

            //=== 音量显示：球色"更浅"的填充层，从卡片底部向上，高度 = 音量 ===
            //   平时淡淡的（能一眼对比各球音量）；鼠标悬停的卡片加深一档
            //   滚轮在卡片上滚动即可调节（见 mouseWheelMove）
            //   ⚠️ 被 Mute / Solo 排除的球：音量层淡到几乎看不见 —— 与它"音量已降到 0"
            //      的实际听感一致，也顺手让整张卡片显得"退后"（下面再压一层雾）
            {
                const float v = juce::jlimit (0.0f, 1.0f, b.volume);
                auto fill = chip;
                fill.setY (fill.getBottom() - fill.getHeight() * v);
                fill.setHeight (fill.getHeight() * v);

                juce::Path clip;
                clip.addRoundedRectangle (chip, 8.0f);
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (clip);                       // 裁进卡片圆角内
                const float volAlpha = (i == hoverIndex ? 0.52f : 0.28f) * (quiet ? 0.28f : 1.0f);
                g.setColour (b.colour.brighter (0.42f).withAlpha (volAlpha));
                g.fillRect (fill);
                // 音量层顶边画一条同色细线，读数更清楚
                g.setColour (b.colour.brighter (0.20f)
                               .withAlpha ((i == hoverIndex ? 0.75f : 0.45f) * (quiet ? 0.28f : 1.0f)));
                g.fillRect (fill.getX(), fill.getY() - 0.5f, fill.getWidth(), 1.0f);
            }

            // 色点：顶部居中，光晕收紧在点周围（不碰下方文字）
            const float ccx = chip.getCentreX();
            const float ccy = chip.getY() + 15.0f;
            const float r = 6.0f;
            g.setColour (b.colour.withAlpha (0.18f));
            g.fillEllipse (ccx - r * 1.9f, ccy - r * 1.9f, r * 3.8f, r * 3.8f);
            g.setColour (b.colour);
            g.fillEllipse (ccx - r, ccy - r, r * 2.0f, r * 2.0f);

            // 主信息：音高（13px 粗）
            g.setFont (juce::FontOptions (snapFontSize (13.0f, uiScale), juce::Font::bold));
            g.setColour (sel ? Ui::ink : Ui::inkMid);
            g.drawText (pitchText (b), (int) chip.getX(), (int) chip.getY() + 28,
                        (int) chip.getWidth(), 14, juce::Justification::centred);
            // 次要信息：音色 / 通道（11px 浅灰）
            //  ⚠️ 通道那一行在 probability < 100% 时改为显示 "Ch1 · 60%"：
            //     概率是个"扫一眼就该知道"的参数（不然曲子变稀疏了会以为是 bug），
            //     100% 是默认值、绝大多数球都是它，就不占版面了。
            g.setFont (juce::FontOptions (snapFontSize (12.0f, uiScale)));
            g.setColour (Ui::inkSoft);
            g.drawText (Timbre::name (juce::jlimit (0, Timbre::num - 1, b.timbre)),
                        (int) chip.getX(), (int) chip.getY() + 42,
                        (int) chip.getWidth(), 13, juce::Justification::centred);

            juce::String foot = "Ch" + juce::String (b.channel);
            if (b.probability < 0.999f)
                foot += " . P" + juce::String ((int) std::lround (b.probability * 100.0f)) + "%";
            g.drawText (foot, (int) chip.getX(), (int) chip.getY() + 55,
                        (int) chip.getWidth(), 13, juce::Justification::centred);

            //=== M / S / A 角标（有状态才显示，避免卡片噪声）===
            //   Mute = 陶粉，Solo = 琥珀，ARP = 雾蓝灰（与属性区语义色一致）
            if (b.arpOn)
            {
                auto bg = juce::Rectangle<float> (chip.getRight() - 33.0f, chip.getY() + 4.0f, 13.0f, 13.0f);
                g.setColour (Ui::arp);
                g.fillRoundedRectangle (bg, 4.0f);
                g.setColour (juce::Colours::white.withAlpha (0.94f));
                g.setFont (juce::FontOptions (snapFontSize (10.5f, uiScale), juce::Font::bold));
                g.drawText ("A", bg, juce::Justification::centred);
            }
            if (b.mute)
            {
                auto bg = juce::Rectangle<float> (chip.getRight() - 18.0f, chip.getY() + 4.0f, 13.0f, 13.0f);
                g.setColour (Ui::mute);
                g.fillRoundedRectangle (bg, 4.0f);
                g.setColour (juce::Colours::white.withAlpha (0.94f));
                g.setFont (juce::FontOptions (snapFontSize (10.5f, uiScale), juce::Font::bold));
                g.drawText ("M", bg, juce::Justification::centred);
            }
            if (b.solo)
            {
                auto bg = juce::Rectangle<float> (chip.getX() + 4.0f, chip.getY() + 4.0f, 13.0f, 13.0f);
                g.setColour (Ui::solo);
                g.fillRoundedRectangle (bg, 4.0f);
                g.setColour (juce::Colour (0xff4a3a12));
                g.setFont (juce::FontOptions (snapFontSize (10.5f, uiScale), juce::Font::bold));
                g.drawText ("S", bg, juce::Justification::centred);
            }
            else if (anySolo)
            {
                // 有球 Solo 时，未 Solo 的球"音量已被降到 0"→ 压一层极淡雾面提示
                g.setColour (Ui::inkSoft.withAlpha (0.07f));
                g.fillRoundedRectangle (chip, 8.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // 只记录按下位置；选中放到 mouseUp 里判定 ——
        // 按下后拖动的手势让给视口做"整片滚动"（见 BallStrip 的 setScrollOnDragEnabled）
        pressPos = e.getPosition();
        dragMoved = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // 只标记"这是拖动"，不移动任何球：滚动由视口的 drag-to-scroll 完成
        if (! dragMoved && pressPos.getDistanceFrom (e.getPosition()) > 6)
            dragMoved = true;
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        // 没有拖动 → 这是一次"点击" → 执行选中（Ctrl/Shift 加选）
        if (dragMoved)
        {
            dragMoved = false;
            return;
        }

        const int i = indexAt (e.getPosition());
        if (i < 0) return;

        auto& world = processor.getWorld();
        if (e.mods.isCtrlDown() || e.mods.isShiftDown())
            world.toggleSelected (i);
        else
            world.setSelected (i);

        repaint();
        if (onSelect) onSelect();
    }

    //=== 悬停追踪：决定哪张卡片的音量层加深 =================================
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.getPosition());
        if (i != hoverIndex)
        {
            hoverIndex = i;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverIndex != -1)
        {
            hoverIndex = -1;
            repaint();
        }
    }

    //=== 滚轮调音量：悬停在哪张卡片上就调哪颗球 =============================
    //   上滚 = 增大，下滚 = 减小，每次 5%（0..1 共 20 格）
    //
    //   ⚠️ 事件可能落在三个层级：本层（content）/ Viewport / BallStrip。
    //   尤其 Viewport 会先于我们把滚轮拿去横向滚动 —— 所以调节逻辑统一放在
    //   wheelAdjustVolume()（public，供 BallStrip 转发），三个层级都接，
    //   再用 eventTime 去重，保证同一格滚轮只步进一次。
    //   返回 true = 本次滚轮已被消费（命中卡片并调节，或刚被别的层级处理过）
    bool wheelAdjustVolume (juce::Point<int> posInContent,
                            const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        if (e.eventTime == lastWheelHandled)      // 同一次滚轮事件已由其它层级处理
            return true;

        const int i = indexAt (posInContent);
        if (i < 0)
            return false;                          // 不在卡片上 → 不消费，让视口滚动

        float d = w.deltaY;
        if (std::abs (d) < 1.0e-4f) d = w.deltaX;  // 触控板横滑也算
        if (std::abs (d) < 1.0e-4f)
            return false;

        auto& b = processor.getWorld().balls[(size_t) i];
        const float before = b.volume;
        b.volume = juce::jlimit (0.0f, 1.0f, b.volume + (d > 0.0f ? 0.05f : -0.05f));

        lastWheelHandled = e.eventTime;
        if (b.volume != before)
            repaint();
        return true;
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        wheelAdjustVolume (e.getPosition(), e, w);   // 未命中 → 不消费 → 冒泡给 Viewport
    }

private:
    juce::Point<int> pressPos;     // 按下位置（区分"点击选中"与"拖动滚动"）
    bool dragMoved = false;        // 按下后移动超过阈值 → 是拖动，不触发选中
    int  hoverIndex = -1;    // 鼠标悬停的卡片（音量层加深 + 滚轮调音量的目标）
    juce::Time lastWheelHandled;   // 去重：同一次滚轮事件可能被多个层级收到

    juce::Rectangle<int> chipBounds (int i) const
    {
        // 高度跟随内容层：内容层 82 → 卡片 74（上下各留 4 的余量）
        //  上方 4px 给选中/角标留出呼吸位，下方 4px 给音量层与描边留位，
        //  保证 1.6px 的选中描边完整落在卡片内、不被内容层裁掉
        return { gap + i * (chipW + gap), 4, chipW, juce::jmax (64, getHeight() - 8) };
    }

    int indexAt (juce::Point<int> p) const
    {
        const int n = (int) processor.getWorld().balls.size();
        for (int i = 0; i < n; ++i)
            if (chipBounds (i).contains (p))
                return i;
        return -1;
    }

    static juce::String noteName (int n)
    {
        n = juce::jlimit (0, 127, n);
        static const char* nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (nn[n % 12]) + juce::String (n / 12 - 1);
    }

    static juce::String pitchText (const Ball& b)
    {
        static const char* tones[4] = { "Root", "3rd", "5th", "7th" };
        // 跟随和弦：显示"基准音 + 和弦音位置"（基准音依然有效，发声就近落到和弦音）
        juce::String s = b.follow ? (noteName (b.note + b.pitchOffset) + " +" + juce::String (tones[juce::jlimit (0, 3, b.tone)]))
                                  : noteName (b.note + b.pitchOffset);

        if (b.octaveShift != 0)
            s += juce::String (b.octaveShift > 0 ? " +" : " -")
               + juce::String (std::abs (b.octaveShift)) + "oct";
        return s;
    }

    BounceAudioProcessor& processor;
};

//==============================================================================
// 感知滚轮的视口
//   JUCE 的 Viewport 会把落在自己身上的滚轮直接拿去横向滚动，
//   卡片层（content）根本收不到事件 —— 必须在 Viewport 这一层拦截。
//   onCardWheel 返回 true = 该滚轮已用于调音量，不要再滚动；
//   返回 false = 鼠标不在卡片上，按普通视口滚动。
class WheelAwareViewport : public juce::Viewport
{
public:
    std::function<bool (const juce::MouseEvent&, const juce::MouseWheelDetails&)> onCardWheel;

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (onCardWheel && onCardWheel (e, w))
            return;
        Viewport::mouseWheelMove (e, w);
    }
};

//==============================================================================
// 底部小球条：横向滚动容器（球多时可左右滚动）
class BallStrip : public juce::Component
{
public:
    explicit BallStrip (BounceAudioProcessor& p) : content (p)
    {
        view.setViewedComponent (&content, false);
        view.setScrollBarsShown (false, true);
        view.setScrollBarThickness (6);
        view.setScrollOnDragEnabled (true);   // 按住卡片拖动 = 整片滚动（手机触摸手感）
        addAndMakeVisible (view);

        content.onSelect = [this] { if (onSelect) onSelect(); };

        // 滚轮落在 Viewport 层时转发给卡片层判断（坐标系按事件源换算）
        view.onCardWheel = [this] (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
        {
            auto* src = e.eventComponent;
            auto posInContent = (src != nullptr ? content.getLocalPoint (src, e.getPosition())
                                                : content.getLocalPoint (this, e.getPosition()));
            return content.wheelAdjustVolume (posInContent, e, w);
        };
    }

    std::function<void()> onSelect;

    void refresh()
    {
        relayout();
        content.refresh();
    }

    void resized() override
    {
        view.setBounds (getLocalBounds());
        relayout();
    }

private:
    void relayout()
    {
        // 内容层固定 82（卡片 74 + 上下各 4 余量），比视口内容区（92 - 6 滚动条 = 86）矮，
        // 滚动条落在卡片下方，不再遮住"Ch1"那行
        content.setSize (juce::jmax (view.getWidth(), content.preferredWidth()), 82);
    }

    WheelAwareViewport view;
    BallStripChips content;
};

//==============================================================================
// 滚轮捕获器
//   JUCE 的滚轮事件只在"鼠标下最底层控件"上触发，再靠 Component::mouseWheelMove
//   往父组件冒泡——这条链在部分宿主/平台下会被吃掉（父组件收不到）。
//   这里改成直接在控件本身覆写 mouseWheelMove（与 JUCE 自带滚轮行为同一条链路，
//   命中率 100%），再转发给编辑器统一走"一格步进"。
//==============================================================================
template <class BaseT>
class WheelCatcher : public BaseT
{
public:
    std::function<void (int)> onWheelStep;   // +1 = 向上滚（下一个/更大），-1 = 向下滚

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        float d = w.deltaY;
        if (std::abs (d) < 1.0e-4f) d = w.deltaX;          // 触控板横滑也算
        if (std::abs (d) < 1.0e-4f)
        {
            BaseT::mouseWheelMove (e, w);
            return;
        }

        if (onWheelStep)
            onWheelStep (d > 0.0f ? 1 : -1);
        else
            BaseT::mouseWheelMove (e, w);
    }
};

//==============================================================================
class BounceAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer,
                                   private juce::KeyListener
{
public:
    explicit BounceAudioProcessorEditor (BounceAudioProcessor&);
    ~BounceAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // 从 processor 同步全部控件（反序列化后调用）
    void refreshFromProcessor();

    // 捕获 Del / Backspace 删除选中的球
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    // 鼠标滚轮：在任意下拉框 / 数值控件上方滚动即可调节（+/- 按钮之外的快捷方式）
    //   向上滚 = 增加 / 向右（更大值），向下滚 = 减少 / 更小值
    //   实现见下方 WheelCatcher —— 在控件本身接事件，不走父组件冒泡

private:
    BounceAudioProcessor& audioProcessor;

    BounceLookAndFeel lnf;   // 统一黑底低饱和外观
    BallPanel ballPanel;
    BallStrip ballStrip;

    WheelCatcher<juce::ComboBox> progBox, scaleBox, keyBox, midiBox;
    juce::ToggleButton previewToggle { "Preview" };
    juce::ToggleButton dawToggle { "Sync DAW" };
    WheelCatcher<juce::Slider> bpmSlider;
    juce::Label bpmLabel;
    juce::TextButton runButton { "Play" };

    // 选中球属性
    WheelCatcher<juce::ComboBox> pitchBox, toneBox, rateBox, chanBox, ballTimbreBox;
    juce::ToggleButton followToggle { "Follow Chord" };
    WheelCatcher<juce::Slider> octaveSlider;   // 跟随和弦时的八度偏移
    // Solo / Mute（与 DAW 语义一致；Toggle 样式，开启后按钮底色点亮）
    juce::TextButton soloButton { "S" }, muteButton { "M" };

    // 触发概率（0..100%）：滚轮一格 5%；0 = 永不触发，100 = 每次碰撞都触发
    //  用 Slider(IncDecButtons) 而不是 ComboBox：这是连续量，
    //  直接用滚轮微调最顺手（和 oct / Tune 那几个旋钮一致的操作方式）
    WheelCatcher<juce::Slider> probSlider;
    juce::Label  probLabel { {}, "Prob" };

    //=== 第三排：琶音器（每球独立；开启后发声由 ARP 接管，碰撞不再重复触发）===
    juce::ToggleButton arpToggle { "Arp" };
    WheelCatcher<juce::ComboBox> arpRateBox;    // 步进时长 1/2 1/4 1/8 1/16
    WheelCatcher<juce::ComboBox> arpModeBox;    // Up / Down / Up-Down / Random / Chord
    WheelCatcher<juce::Slider>   arpOctSlider;  // 八度跨度 1..3
    WheelCatcher<juce::Slider>   arpStepSlider; // 一个循环的步数 2..8

    // 小标签：控件左侧的浅灰说明字
    juce::Label pitchLabel { {}, "Note" }, chordLabel { {}, "Chord" },
               octLabel { {}, "oct" },   rateLabel { {}, "Rate" },
               chLabel { {}, "Ch" },     toneLabel { {}, "Tone" },
               soloLabel { {}, "Solo" }, muteLabel { {}, "Mute" },
               arpRateLabel { {}, "Rate" }, arpModeLabel { {}, "Mode" },
               arpOctLabel { {}, "Oct" },    arpStepLabel { {}, "Step" };
    juce::TextButton deleteButton { "Delete" }, addCenterButton { "+ Add" };
    juce::TextButton clearButton { "Clear All" };
    juce::TextButton ensembleButton { "Ensemble" };   // 按档位分派互相配合的音色

    // 界面状态追踪（物理已搬到音频线程，这里不再需要 lastTime）
    int lastSelected = -2;
    int lastSelCount = -1;
    int lastBallCount = -1;

    // 画布是否需要重绘：静止且水纹散尽时不必 60fps 空转
    // （选中/球数变化、暂停后的第一次都要置 true，见 timerCallback）
    bool panelDirty = true;

    // 属性区随球变色：选中球时整块属性区铺球色淡背景
    juce::Rectangle<int> propArea;
    juce::Colour accentCol { 0xffe4e0d8 };
    bool accentOn = false;

    void refreshBallControls();
    void refreshMidiList();

    // 滚轮步进：下拉框按 id 走一格（自动跳过八度/音色分组标题）
    static bool stepComboBox (juce::ComboBox& box, int dir);
    // 数值控件走一个 interval
    void stepSlider (juce::Slider& s, int dir);

    // Solo / Mute 应用到当前选中（含多选）；isSolo=true 时改 solo 字段，否则改 mute 字段
    void applySoloMuteToSelection (bool state, bool isSolo);

    // 把每颗球的可听状态全量推给音频线程（Solo / Mute / 增删球后都要调）
    void pushGatesToProcessor();
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BounceAudioProcessorEditor)
};
