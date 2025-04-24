//
// Created by Deamon on 4/23/2025.
//

#ifndef WOWROOTFLAGS_H
#define WOWROOTFLAGS_H

namespace RootWoW {
    enum class LoadMode : uint32_t {
        Normal,
        Full
    };

    enum class LocaleFlags : uint32_t {
        All       = 0xFFFFFFFF,
        None      = 0,
        Unk_1     = 0x1,
        enUS      = 0x2,
        koKR      = 0x4,
        Unk_8     = 0x8,
        frFR      = 0x10,
        deDE      = 0x20,
        zhCN      = 0x40,
        esES      = 0x80,
        zhTW      = 0x100,
        enGB      = 0x200,
        enCN      = 0x400,
        enTW      = 0x800,
        esMX      = 0x1000,
        ruRU      = 0x2000,
        ptBR      = 0x4000,
        itIT      = 0x8000,
        ptPT      = 0x10000,
        enSG      = 0x20000000,
        plPL      = 0x40000000,
        All_WoW   = enUS | koKR | frFR | deDE | zhCN | esES | zhTW | enGB | esMX | ruRU | ptBR | itIT | ptPT
    };

    enum class ContentFlags : uint32_t {
        None             = 0,
        F00000001        = 0x1,
        F00000002        = 0x2,
        F00000004        = 0x4,
        LoadOnWindows    = 0x8,
        LoadOnMacOS      = 0x10,
        LowViolence      = 0x80,
        DoNotLoad        = 0x100,
        UpdatePlugin     = 0x800,
        Encrypted        = 0x8000000,
        NoNames          = 0x10000000,
        UncommonRes      = 0x20000000,
        Bundle           = 0x40000000,
        NoCompression    = 0x80000000
    };
}

#endif //WOWROOTFLAGS_H
