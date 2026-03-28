#pragma once
#include <juce_core/juce_core.h>
#include <cstring>

// =====================================================================
// LanguageManager — centralised UI string localisation (i18n)
//
// Usage:
//   LvhStr ("STR_LOAD")   → "LOAD" (en) / "ロード" (ja)
//
// Initialise once at startup (before any windows open):
//   LanguageManager::getInstance().init (appProperties_.getUserSettings());
//
// Change language at runtime:
//   LanguageManager::getInstance().setLanguage (lang, prefs);
//
// Adding a new string:
//   1. Add a { "STR_MY_KEY", "English text", "日本語テキスト" } entry
//      to the table inside get().
//   2. Call LvhStr ("STR_MY_KEY") wherever you need it.
//
// jassertfalse fires in Debug if a string ID is not found — helps catch typos.
// =====================================================================
class LanguageManager
{
public:
    enum class Language { English, Japanese };

    static LanguageManager& getInstance() noexcept
    {
        static LanguageManager instance;
        return instance;
    }

    // Call once during app initialise to restore the saved language.
    void init (juce::PropertiesFile* prefs)
    {
        if (prefs != nullptr)
            current_ = (prefs->getValue ("language", "en") == "ja")
                       ? Language::Japanese : Language::English;
    }

    // Change language and optionally persist the choice.
    void setLanguage (Language lang, juce::PropertiesFile* prefs = nullptr)
    {
        current_ = lang;
        if (prefs != nullptr)
            prefs->setValue ("language",
                             lang == Language::Japanese ? "ja" : "en");
    }

    Language getLanguage() const noexcept { return current_; }
    bool     isJapanese()  const noexcept { return current_ == Language::Japanese; }

    // Look up a localised string by ID. Falls back to returning the ID
    // itself so missing strings are always visible in the UI.
    juce::String get (const char* id) const noexcept
    {
        struct Entry { const char* id; const char* en; const char* ja; };
        static const Entry table[] =
        {
            // ── Stage Window ────────────────────────────────────────────
            { "STR_NEW",                 "NEW",                                "新規"                        },
            { "STR_OPEN_SET",            "OPEN SET",                           "開く"                        },
            { "STR_SAVE_SET",            "SAVE SET",                           "保存"                        },
            { "STR_ADD",                 "+ ADD",                              "+ 追加"                      },
            { "STR_LOAD",                "LOAD",                               "ロード"                      },
            { "STR_RENAME_ALIAS",        "Rename Alias",                       "エイリアス変更"              },
            { "STR_DELETE",              "Delete",                             "削除"                        },
            { "STR_RENAME_ALIAS_TITLE",  "Rename Alias",                       "エイリアス変更"              },
            { "STR_RENAME_ALIAS_MSG",    "Enter new alias:",                   "新しいエイリアスを入力："    },
            { "STR_DELETE_TITLE",        "Delete Item",                        "アイテムの削除"              },
            { "STR_DELETE_CONFIRM_PRE",  "Remove \"",                          "「"                          },
            { "STR_DELETE_CONFIRM_POST", "\" from the set?",                   "」をセットから削除しますか？"},
            { "STR_OK",                  "OK",                                 "OK"                          },
            { "STR_CANCEL",              "Cancel",                             "キャンセル"                  },
            // ── Mixer Window ────────────────────────────────────────────
            { "STR_MIXER_CONSOLE",       "MIXER CONSOLE",                      "ミキサー"                    },
            { "STR_MASTER",              "MASTER",                             "マスター"                    },
            { "STR_BYPASS_FX",           "Bypass FX",                          "FXバイパス"                  },
            { "STR_METER_TOOLTIP",       "Toggle LED Meters",                  "LEDメーターの表示切替"        },
            { "STR_NO_INSTRUMENTS",      "No instruments loaded.\nLaunch a Bridge to add channels.",
                                                                               "楽器が読み込まれていません。\nBridgeを起動してチャンネルを追加してください。" },
            // ── Window controls ─────────────────────────────────────────
            { "STR_PIN_TOOLTIP",         "Pin window on top",                  "最前面に固定"                },
            // ── Settings — Nav labels ───────────────────────────────────
            { "STR_NAV_GENERAL",         "General",                            "一般"                        },
            { "STR_NAV_AUDIO_MIDI",      "Audio / MIDI",                       "オーディオ / MIDI"           },
            { "STR_NAV_PLUGIN_PATHS",    "Plugin Paths",                       "プラグインパス"              },
            { "STR_NAV_MIDI_SETTINGS",   "MIDI Settings",                      "MIDI設定"                    },
            { "STR_NAV_VISUALIZER",      "Visualizer",                         "ビジュアライザー"            },
            // ── Settings — Visualizer page ──────────────────────────────
            { "STR_VIS_VU_HEADER",       "VU Meter",                           "VUメーター"                  },
            { "STR_VIS_VU_THEME",        "Backlight Theme:",                   "バックライトテーマ："        },
            { "STR_VIS_WARM",            "Vintage Warm",                       "ヴィンテージウォーム"        },
            { "STR_VIS_NEON",            "Oxygen Neon",                        "オキシジェンネオン"          },
            // ── Settings — General page ─────────────────────────────────
            { "STR_LANGUAGE",            "Language",                           "言語"                        },
            { "STR_SHOW_LEVEL_METER",    "Level Meter (toolbar)",              "レベルメーター（ツールバー）" },
            { "STR_SHOW_MIDI_MONITOR",   "MIDI Monitor (toolbar)",             "MIDIモニター（ツールバー）"  },
            { "STR_SHOW_INFO_MONITOR",   "Info Monitor Panel (main area)",     "情報モニター（メインエリア）"},
            { "STR_REMEMBER_FOLDER",     "Remember last opened Bridge folder", "前回のBridgeフォルダを記憶"  },
            { "STR_RECENT_COUNT",        "Recent Bridges shown in menu:",      "メニューに表示する最近のBridge：" },
            // ── Settings — MIDI page ────────────────────────────────────
            { "STR_TRANSPOSE",           "Transpose (semitones):",             "トランスポーズ（半音）："    },
            { "STR_CHANNEL_FILTER",      "MIDI Channel Filter:",               "MIDIチャンネルフィルター："  },
            { "STR_ALL_CHANNELS",        "All Channels",                       "全チャンネル"                },
            { "STR_CHANNEL",             "Channel ",                           "チャンネル "                 },
            // ── Settings — Plugin Paths page ────────────────────────────
            { "STR_ADD_PATH",            "Add Path...",                        "パスを追加..."               },
            { "STR_REMOVE",              "Remove",                             "削除"                        },
            { "STR_RESCAN_PLUGINS",      "Rescan Plugins",                     "プラグインを再スキャン"       },
            { "STR_SELECT_SCAN_FOLDER",  "Select VST3 scan folder",            "VST3 スキャンフォルダを選択" },
            { "STR_SETTINGS_TITLE",      "Settings",                           "設定"                        },
            // ── Mixer MIDI Learn ────────────────────────────────────────
            { "STR_MIDI_LEARN",      "MIDI Learn",             "MIDI ラーン"              },
            { "STR_MIDI_CLEAR_MAP",  "Clear Mapping",          "マッピングを解除"          },
            // ── MIDI Settings — Stage Remote Control ────────────────────
            { "STR_STAGE_REMOTE",    "Stage Remote Control",   "ステージリモート制御"     },
            { "STR_REMOTE_METHOD",   "Control Method:",        "操作方式："               },
            { "STR_REMOTE_NONE",     "None",                   "無効"                     },
            { "STR_REMOTE_PC",       "Program Change",         "プログラムチェンジ"       },
            { "STR_REMOTE_CC",       "Control Change (CC)",    "コントロールチェンジ (CC)"},
            { "STR_REMOTE_CH",       "Remote Channel:",        "受信チャンネル："         },
            { "STR_REMOTE_CC_PREV",  "Prev CC#:",              "前へ CC#："               },
            { "STR_REMOTE_CC_NEXT",  "Next CC#:",              "次へ CC#："               },
            { "STR_REMOTE_CC_LOAD",  "Load CC#:",              "ロード CC#："             },
            // ── Dialogs — unsaved changes / file choosers ────────────────
            { "STR_UNSAVED_TITLE",       "Unsaved Changes",                    "未保存の変更"                },
            { "STR_UNSAVED_MSG",         "The current set has unsaved changes.\nDo you want to save before continuing?",
                                                                               "現在のセットに未保存の変更があります。\n続行前に保存しますか？" },
            { "STR_SAVE_SET_DIALOG",     "Save Stage Set...",                  "ステージセットを保存..."     },
            { "STR_OPEN_SET_DIALOG",     "Open Stage Set...",                  "ステージセットを開く..."     },
            { "STR_ADD_BRIDGE_DIALOG",   "Add project to set...",              "セットにプロジェクトを追加..."},
            // sentinel
            { nullptr, nullptr, nullptr }
        };

        bool ja = (current_ == Language::Japanese);
        for (const auto* e = table; e->id != nullptr; ++e)
            if (std::strcmp (e->id, id) == 0)
                return juce::String::fromUTF8 (ja ? e->ja : e->en);

        jassertfalse;   // unknown string ID — add it to the table above
        return juce::String::fromUTF8 (id);
    }

private:
    Language current_ = Language::English;

    LanguageManager()  = default;
    ~LanguageManager() = default;

    JUCE_DECLARE_NON_COPYABLE (LanguageManager)
};

// ── Convenience free function — use this everywhere in UI code ─────────────
inline juce::String LvhStr (const char* id)
{
    return LanguageManager::getInstance().get (id);
}
