#pragma once
#include <juce_core/juce_core.h>

// 触发事件：音频线程 → 音频线程（无锁 FIFO）
//  ⚠️ 2026-10-15 起生产者在音频线程（world.audioThreadUpdate 里的碰撞 / 琶音），
//     消费者也在音频线程（renderVoices）；UI 线程只读不写。
struct Trigger
{
    int   note     = 0;
    int   channel  = 1;
    float velocity = 0.9f;
    int   timbre   = 1;   // 该次发声使用的音色（每个球独立）
    int   ballIndex = -1; // 发出这个触发的球在 balls 里的下标（查 Mute/Solo 闸门用）
};

class TriggerQueue
{
public:
    void push (const Trigger& t)
    {
        const auto w = fifo.write (1);
        if (w.blockSize1 > 0)
        {
            data[w.startIndex1] = t;
            fifo.finishedWrite ((int) w.blockSize1);
        }
    }

    bool pop (Trigger& t)
    {
        const auto r = fifo.read (1);
        if (r.blockSize1 > 0)
        {
            t = data[r.startIndex1];
            fifo.finishedRead ((int) r.blockSize1);
            return true;
        }
        return false;
    }

    // 队列里是否还有待处理的触发（processBlock 的静默快速通道用）
    bool isEmpty() const { return fifo.getNumReady() == 0; }

    //==== 只读遍历（不移动读游标）=========================================
    //  用于"先看一遍待处理的触发、再决定每个 voice 的目标增益"（见 updateVoiceGates）。
    //  ⚠️ 纯只读：只用 prepareToRead 取当前可读区段，**绝不调 finishedRead**，
    //     否则会把还没发声的音吃掉。
    //  JUCE 的 AbstractFifo 是环形缓冲，一次可读段最多跨一次回绕 ——
    //  所以用 (startIndex + 线性偏移) % size 换算成物理下标即可。
    class ConstIterator
    {
    public:
        ConstIterator (const Trigger* d, int start, int count)
            : data (d), startIdx (start), remaining (count) {}

        bool operator!= (const ConstIterator& other) const { return remaining != other.remaining; }
        const Trigger& operator*()  const { return data[(startIdx + pos) % size]; }
        const Trigger* operator->() const { return &operator*(); }
        ConstIterator& operator++() { ++pos; --remaining; return *this; }

    private:
        const Trigger* data;
        int startIdx, remaining, pos = 0;
    };

    ConstIterator begin() const
    {
        int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
        const int ready = fifo.getNumReady();
        // 只取"当前可读"的整段（n1+n2 == ready），不消费
        fifo.prepareToRead (ready, s1, n1, s2, n2);
        return { data, s1, n1 + n2 };
    }
    ConstIterator end() const { return { data, 0, 0 }; }

private:
    static constexpr int size = 256;
    juce::AbstractFifo fifo { size };
    Trigger data[size];
};
