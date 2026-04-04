/**
 * @file SimpleLooper.cpp
 * @brief Simple Looper — LVH Processor SDK sample plugin.
 *
 * Demonstrates the Processor SDK with a self-contained Win32 UI.
 * Supports REC / PLAY / OVERDUB / CLEAR via buttons and configurable
 * global hotkeys (F1-F12, A-Z, 0-9) suitable for USB foot switch assignment.
 *
 * Default hotkeys are F1-F4 to avoid conflicts with LVH's virtual keyboard
 * (which uses Z/S/X/D/C/V/G/B/H/N/J/M for MIDI note input).
 *
 * No JUCE dependency — pure Win32 + standard C++17.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../ProcessorSDK/IProcessorPlugin.h"
#include <atomic>
#include <vector>
#include <algorithm>

// =============================================================================
// DLL module handle
// =============================================================================

static HINSTANCE g_hInst = nullptr;

BOOL WINAPI DllMain (HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        g_hInst = hInst;
    return TRUE;
}

// =============================================================================
// Constants
// =============================================================================

enum class LooperState : int { Idle, Recording, Playing, Overdubbing };

static constexpr int  ID_BTN_REC      = 101;
static constexpr int  ID_BTN_PLAY     = 102;
static constexpr int  ID_BTN_OVERDUB  = 103;
static constexpr int  ID_BTN_CLEAR    = 104;
static constexpr int  ID_EDIT_REC     = 111;
static constexpr int  ID_EDIT_PLAY    = 112;
static constexpr int  ID_EDIT_OVERDUB = 113;
static constexpr int  ID_EDIT_CLEAR   = 114;
static constexpr int  HK_REC          = 1;
static constexpr int  HK_PLAY         = 2;
static constexpr int  HK_OVERDUB      = 3;
static constexpr int  HK_CLEAR        = 4;
static constexpr UINT TIMER_UI        = 1;   ///< 100 ms UI refresh timer.

// =============================================================================
// SimpleLooper
// =============================================================================

/**
 * @class SimpleLooper
 * @brief LVH Processor SDK sample: a 5-minute stereo looper with Win32 UI.
 *
 * **State machine:**
 * - IDLE      : audio passes through unchanged.
 * - RECORDING : input is written to the circular buffer; audio passes through.
 *               Press REC again to stop and auto-start playback.
 * - PLAYING   : buffer plays back in a loop; input is ignored.
 *               Press PLAY to stop.
 * - OVERDUBBING: buffer plays back and input is mixed (clamped to ±1) into it.
 *               Press OVERDUB again to return to PLAYING.
 *
 * **Hotkey defaults: F1=REC, F2=PLAY, F3=OVERDUB, F4=CLEAR**
 * Function keys are used by default to avoid conflicts with LVH's virtual
 * keyboard (Z/S/X/D/C/V/G/B/H/N/J/M) and general text input.
 * Users may reassign to A-Z or 0-9, but those keys will be captured globally.
 *
 * **Thread safety:**
 * - processBlock() runs on the audio thread; all shared state uses std::atomic.
 * - Action methods (doRec, doPlay, doOverdub, doClear) run on the message thread.
 * - UI label is refreshed by a 100 ms WM_TIMER on the message thread.
 */
class SimpleLooper : public IProcessorPlugin
{
public:
    SimpleLooper()  = default;
    ~SimpleLooper() = default;
    SimpleLooper (const SimpleLooper&)            = delete;
    SimpleLooper& operator= (const SimpleLooper&) = delete;

    // =========================================================================
    // IProcessorPlugin
    // =========================================================================

    void initialise (double sampleRate, int maxBufferSize) override
    {
        // Stop audio before reallocating
        state_.store (LooperState::Idle, std::memory_order_release);

        sampleRate_    = sampleRate;
        maxBufferSize_ = maxBufferSize;

        const size_t len = static_cast<size_t> (sampleRate * kMaxSeconds);
        for (int ch = 0; ch < 2; ++ch)
            buf_[ch].assign (len, 0.0f);
        bufLen_ = len;

        writePos_.store (0);
        readPos_ .store (0);
        loopLen_ .store (0);

        if (hwnd_ == nullptr)
            createWindow();
    }

    void shutdown() override
    {
        state_.store (LooperState::Idle, std::memory_order_release);

        if (hwnd_ != nullptr)
        {
            KillTimer (hwnd_, TIMER_UI);
            for (int i = 1; i <= 4; ++i)
                UnregisterHotKey (hwnd_, i);
            DestroyWindow (hwnd_);
            hwnd_ = nullptr;
        }
        UnregisterClassA (kWndClass, g_hInst);
    }

    void processBlock (const float* const* in,
                       float**             out,
                       int                 numChannels,
                       int                 numSamples) override
    {
        const LooperState st      = state_  .load (std::memory_order_acquire);
        const size_t      loopLen = loopLen_.load (std::memory_order_relaxed);
        const int         ch2     = (numChannels >= 2) ? 2 : 1;

        for (int s = 0; s < numSamples; ++s)
        {
            switch (st)
            {
            case LooperState::Idle:
                for (int c = 0; c < ch2; ++c) out[c][s] = in[c][s];
                break;

            case LooperState::Recording:
            {
                size_t wp = writePos_.load (std::memory_order_relaxed);
                if (wp < bufLen_)
                {
                    for (int c = 0; c < ch2; ++c)
                    { buf_[c][wp] = in[c][s]; out[c][s] = in[c][s]; }
                    writePos_.store (wp + 1, std::memory_order_relaxed);
                }
                else
                {
                    // Buffer full — auto-switch to playing
                    loopLen_.store (bufLen_, std::memory_order_release);
                    readPos_.store (0, std::memory_order_relaxed);
                    state_  .store (LooperState::Playing, std::memory_order_release);
                    for (int c = 0; c < ch2; ++c) out[c][s] = in[c][s];
                }
                break;
            }

            case LooperState::Playing:
            {
                if (loopLen == 0)
                { for (int c = 0; c < ch2; ++c) out[c][s] = in[c][s]; break; }
                size_t rp = readPos_.load (std::memory_order_relaxed);
                for (int c = 0; c < ch2; ++c) out[c][s] = buf_[c][rp];
                readPos_.store ((rp + 1) % loopLen, std::memory_order_relaxed);
                break;
            }

            case LooperState::Overdubbing:
            {
                if (loopLen == 0)
                { for (int c = 0; c < ch2; ++c) out[c][s] = in[c][s]; break; }
                size_t rp = readPos_.load (std::memory_order_relaxed);
                for (int c = 0; c < ch2; ++c)
                {
                    float v = buf_[c][rp] + in[c][s];
                    if (v >  1.0f) v =  1.0f;
                    if (v < -1.0f) v = -1.0f;
                    buf_[c][rp] = v;
                    out[c][s]   = v;
                }
                readPos_.store ((rp + 1) % loopLen, std::memory_order_relaxed);
                break;
            }
            }
        }
    }

    const char*  getName()         const override { return "Simple Looper"; }
    unsigned int getAccentColour() const override { return 0xff884466; }
    bool hasUserRequestedClose()   const noexcept override { return closeRequested_.load (std::memory_order_relaxed); }

    // =========================================================================
    // Actions  (message thread)
    // =========================================================================

    void doRec()
    {
        const LooperState st = state_.load();
        if (st == LooperState::Idle)
        {
            for (int c = 0; c < 2; ++c)
                std::fill (buf_[c].begin(), buf_[c].end(), 0.0f);
            loopLen_ .store (0); writePos_.store (0); readPos_.store (0);
            state_.store (LooperState::Recording, std::memory_order_release);
        }
        else if (st == LooperState::Recording)
        {
            const size_t recorded = writePos_.load();
            if (recorded > 0)
            {
                loopLen_.store (recorded, std::memory_order_release);
                readPos_.store (0);
                state_  .store (LooperState::Playing, std::memory_order_release);
            }
            else
            {
                state_.store (LooperState::Idle, std::memory_order_release);
            }
        }
    }

    void doPlay()
    {
        const LooperState st = state_.load();
        if (st == LooperState::Playing || st == LooperState::Overdubbing)
            state_.store (LooperState::Idle, std::memory_order_release);
        else if (loopLen_.load() > 0)
        {
            readPos_.store (0);
            state_  .store (LooperState::Playing, std::memory_order_release);
        }
    }

    void doOverdub()
    {
        const LooperState st = state_.load();
        if (st == LooperState::Playing)
            state_.store (LooperState::Overdubbing, std::memory_order_release);
        else if (st == LooperState::Overdubbing)
            state_.store (LooperState::Playing, std::memory_order_release);
    }

    void doClear()
    {
        state_.store (LooperState::Idle, std::memory_order_release);
        for (int c = 0; c < 2; ++c)
            std::fill (buf_[c].begin(), buf_[c].end(), 0.0f);
        loopLen_.store (0); writePos_.store (0); readPos_.store (0);
    }

private:
    // =========================================================================
    // Constants
    // =========================================================================
    static constexpr double kMaxSeconds = 300.0;
    static constexpr LPCSTR kWndClass   = "LVH_SimpleLooper_v1";

    // =========================================================================
    // Audio state
    // =========================================================================
    std::vector<float> buf_[2];
    size_t             bufLen_        = 0;
    double             sampleRate_    = 44100.0;
    int                maxBufferSize_ = 512;

    std::atomic<LooperState> state_          { LooperState::Idle };
    std::atomic<size_t>      writePos_        { 0 };
    std::atomic<size_t>      readPos_         { 0 };
    std::atomic<size_t>      loopLen_         { 0 };
    std::atomic<bool>        closeRequested_  { false };

    // =========================================================================
    // UI state
    // =========================================================================
    HWND hwnd_        = nullptr;
    HWND lblState_    = nullptr;
    HWND editRec_     = nullptr;
    HWND editPlay_    = nullptr;
    HWND editOverdub_ = nullptr;
    HWND editClear_   = nullptr;

    // Original EDIT class WndProc, stored once and reused for all 4 edit boxes.
    static WNDPROC s_origEditProc;

    char hotkeys_[4][4] = { "F1", "F2", "F3", "F4" };

    // =========================================================================
    // UI helpers
    // =========================================================================

    /** @brief Format elapsed seconds as MM:SS into buf (must be >= 6 bytes). */
    static void fmtTime (char* buf, int totalSec)
    {
        buf[0] = '0' + (totalSec / 60 / 10) % 10;
        buf[1] = '0' + (totalSec / 60) % 10;
        buf[2] = ':';
        buf[3] = '0' + (totalSec % 60 / 10);
        buf[4] = '0' + (totalSec % 60 % 10);
        buf[5] = '\0';
    }

    /**
     * @brief Refresh the state label (called every 100 ms from WM_TIMER).
     *
     * - RECORDING   : "State: RECORDING   00:12"
     * - PLAYING     : "State: PLAYING   [00:12]"
     * - OVERDUBBING : "State: OVERDUBBING   [00:12]"
     * - IDLE        : "State: IDLE"
     */
    void updateStateLabel()
    {
        if (!lblState_) return;

        const LooperState st      = state_  .load (std::memory_order_acquire);
        const size_t      loopLen = loopLen_.load (std::memory_order_relaxed);
        char buf[64]{};
        char timeBuf[8]{};

        switch (st)
        {
        case LooperState::Recording:
        {
            const size_t wp  = writePos_.load (std::memory_order_relaxed);
            const int    sec = (sampleRate_ > 0) ? static_cast<int> (wp / sampleRate_) : 0;
            fmtTime (timeBuf, sec);
            wsprintfA (buf, "State: RECORDING   %s", timeBuf);
            break;
        }
        case LooperState::Playing:
        {
            const int sec = (sampleRate_ > 0 && loopLen > 0)
                                ? static_cast<int> (loopLen / sampleRate_) : 0;
            fmtTime (timeBuf, sec);
            wsprintfA (buf, "State: PLAYING   [%s]", timeBuf);
            break;
        }
        case LooperState::Overdubbing:
        {
            const int sec = (sampleRate_ > 0 && loopLen > 0)
                                ? static_cast<int> (loopLen / sampleRate_) : 0;
            fmtTime (timeBuf, sec);
            wsprintfA (buf, "State: OVERDUBBING   [%s]", timeBuf);
            break;
        }
        default:
            lstrcpyA (buf, "State: IDLE");
            break;
        }

        SetWindowTextA (lblState_, buf);
    }

    // =========================================================================
    // Win32 helpers
    // =========================================================================

    static UINT strToVK (const char* s) noexcept
    {
        if (!s || !s[0]) return 0;

        if (s[0] == 'F' || s[0] == 'f')
        {
            int n = 0;
            for (int i = 1; s[i] >= '0' && s[i] <= '9'; ++i)
                n = n * 10 + (s[i] - '0');
            if (n >= 1 && n <= 12)
                return static_cast<UINT> (VK_F1 + (n - 1));
            return 0;
        }

        char c = s[0];
        if (c >= 'a' && c <= 'z') c = static_cast<char> (c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            return static_cast<UINT> (static_cast<unsigned char> (c));

        return 0;
    }

    void registerHotkeys()
    {
        if (!hwnd_) return;
        for (int i = 1; i <= 4; ++i) UnregisterHotKey (hwnd_, i);
        auto reg = [&] (int id, int idx)
        {
            UINT vk = strToVK (hotkeys_[idx]);
            if (vk) RegisterHotKey (hwnd_, id, MOD_NOREPEAT, vk);
        };
        reg (HK_REC, 0); reg (HK_PLAY, 1); reg (HK_OVERDUB, 2); reg (HK_CLEAR, 3);
    }

    void createWindow()
    {
        WNDCLASSEXA wc{};
        wc.cbSize        = sizeof (wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = g_hInst;
        wc.hCursor       = LoadCursor (nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH> (COLOR_BTNFACE + 1);
        wc.lpszClassName = kWndClass;
        RegisterClassExA (&wc);

        hwnd_ = CreateWindowExA (0, kWndClass, "Simple Looper",
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 400, 200,
                                 nullptr, nullptr, g_hInst, this);

        if (hwnd_) { ShowWindow (hwnd_, SW_SHOW); UpdateWindow (hwnd_); }
    }

    void buildControls (HWND hwnd)
    {
        const int pad = 12, btnW = 78, btnH = 30, gap = 8;

        lblState_ = CreateWindowExA (0, "STATIC", "State: IDLE",
                                     WS_CHILD | WS_VISIBLE | SS_CENTER,
                                     pad, 10, 400 - pad * 2 - 16, 18,
                                     hwnd, nullptr, g_hInst, nullptr);

        int bx = pad;
        auto makeBtn = [&] (LPCSTR lbl, int id)
        {
            CreateWindowExA (0, "BUTTON", lbl,
                             WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                             bx, 38, btnW, btnH,
                             hwnd,
                             reinterpret_cast<HMENU> (static_cast<intptr_t> (id)),
                             g_hInst, nullptr);
            bx += btnW + gap;
        };
        makeBtn ("REC",     ID_BTN_REC);
        makeBtn ("PLAY",    ID_BTN_PLAY);
        makeBtn ("OVERDUB", ID_BTN_OVERDUB);
        makeBtn ("CLEAR",   ID_BTN_CLEAR);

        CreateWindowExA (0, "STATIC", "Hotkeys (F1-F12 / A-Z / 0-9):",
                         WS_CHILD | WS_VISIBLE,
                         pad, 78, 200, 16,
                         hwnd, nullptr, g_hInst, nullptr);

        const int editY = 98, editW = 44, editH = 22;
        int ex = pad;
        auto makeEdit = [&] (int id, const char* def, HWND& out)
        {
            out = CreateWindowExA (WS_EX_CLIENTEDGE, "EDIT", def,
                                   WS_CHILD | WS_VISIBLE | ES_CENTER | ES_UPPERCASE,
                                   ex + (btnW - editW) / 2, editY, editW, editH,
                                   hwnd,
                                   reinterpret_cast<HMENU> (static_cast<intptr_t> (id)),
                                   g_hInst, nullptr);
            SendMessageA (out, EM_LIMITTEXT, 3, 0);

            // Subclass to intercept Enter key → move focus to parent window.
            WNDPROC prev = reinterpret_cast<WNDPROC> (
                SetWindowLongPtrA (out, GWLP_WNDPROC,
                                   reinterpret_cast<LONG_PTR> (EditSubclassProc)));
            if (!s_origEditProc) s_origEditProc = prev;  // store once; same for all EDIT controls

            ex += btnW + gap;
        };
        makeEdit (ID_EDIT_REC,     "F1", editRec_);
        makeEdit (ID_EDIT_PLAY,    "F2", editPlay_);
        makeEdit (ID_EDIT_OVERDUB, "F3", editOverdub_);
        makeEdit (ID_EDIT_CLEAR,   "F4", editClear_);

        CreateWindowExA (0, "STATIC",
                         "REC twice to stop & play.  X hides window (looper keeps running).",
                         WS_CHILD | WS_VISIBLE | SS_CENTER,
                         pad, 140, 400 - pad * 2 - 16, 28,
                         hwnd, nullptr, g_hInst, nullptr);
    }

    void onEditChange (int id)
    {
        HWND  edit; int idx;
        switch (id)
        {
        case ID_EDIT_REC:     edit = editRec_;     idx = 0; break;
        case ID_EDIT_PLAY:    edit = editPlay_;    idx = 1; break;
        case ID_EDIT_OVERDUB: edit = editOverdub_; idx = 2; break;
        case ID_EDIT_CLEAR:   edit = editClear_;   idx = 3; break;
        default: return;
        }
        char buf[4]{};
        GetWindowTextA (edit, buf, sizeof (buf));
        if (buf[0])
        {
            buf[3] = '\0';
            for (int i = 0; i < 3 && buf[i]; ++i)
                if (buf[i] >= 'a' && buf[i] <= 'z')
                    buf[i] = static_cast<char> (buf[i] - 'a' + 'A');
            if (strToVK (buf))
            {
                for (int i = 0; i < 4; ++i) hotkeys_[idx][i] = buf[i];
                registerHotkeys();
            }
        }
    }

    // =========================================================================
    // Edit subclass procedure — intercepts Enter to defocus
    // =========================================================================

    static LRESULT CALLBACK EditSubclassProc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_KEYDOWN && wp == VK_RETURN)
        {
            SetFocus (GetParent (hwnd));
            return 0;
        }
        return CallWindowProcA (s_origEditProc, hwnd, msg, wp, lp);
    }

    // =========================================================================
    // Window procedure
    // =========================================================================

    static LRESULT CALLBACK WndProc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        SimpleLooper* self = nullptr;

        if (msg == WM_NCCREATE)
        {
            self = reinterpret_cast<SimpleLooper*> (
                reinterpret_cast<CREATESTRUCTA*> (lp)->lpCreateParams);
            SetWindowLongPtrA (hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (self));
            self->hwnd_ = hwnd;
        }
        else
        {
            self = reinterpret_cast<SimpleLooper*> (GetWindowLongPtrA (hwnd, GWLP_USERDATA));
        }

        if (!self) return DefWindowProcA (hwnd, msg, wp, lp);

        switch (msg)
        {
        case WM_CREATE:
            self->buildControls (hwnd);
            self->registerHotkeys();
            SetTimer (hwnd, TIMER_UI, 100, nullptr);
            return 0;

        case WM_TIMER:
            if (wp == TIMER_UI) self->updateStateLabel();
            return 0;

        case WM_COMMAND:
            if (HIWORD (wp) == BN_CLICKED)
                switch (LOWORD (wp))
                {
                case ID_BTN_REC:     self->doRec();     break;
                case ID_BTN_PLAY:    self->doPlay();    break;
                case ID_BTN_OVERDUB: self->doOverdub(); break;
                case ID_BTN_CLEAR:   self->doClear();   break;
                }
            else if (HIWORD (wp) == EN_CHANGE)
                self->onEditChange (LOWORD (wp));
            return 0;

        case WM_HOTKEY:
            switch (static_cast<int> (wp))
            {
            case HK_REC:     self->doRec();     break;
            case HK_PLAY:    self->doPlay();    break;
            case HK_OVERDUB: self->doOverdub(); break;
            case HK_CLEAR:   self->doClear();   break;
            }
            return 0;

        case WM_CLOSE:
            ShowWindow (hwnd, SW_HIDE);
            self->closeRequested_.store (true, std::memory_order_relaxed);
            return 0;

        case WM_DESTROY:
            KillTimer (hwnd, TIMER_UI);
            return 0;
        }

        return DefWindowProcA (hwnd, msg, wp, lp);
    }
};

WNDPROC SimpleLooper::s_origEditProc = nullptr;

// =============================================================================
// DLL entry point
// =============================================================================

extern "C" __declspec(dllexport)
IProcessorPlugin* createProcessor()
{
    return new SimpleLooper();
}
