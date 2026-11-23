#pragma once
#include "../core/Geometry.h"

namespace wd {

class Gfx;

enum IconId {
    IcExplorer,
    IcNotepad,
    IcCalculator,
    IcSettings,
    IcTerminal,
    IcPaint,
    IcMines,
    IcFolder,
    IcFile,
    IcImage,
    IcPC,
    IcBin,
    IcDrive,
    IcLogo,
    IcUser,
    IcDialog,
    IcClock,
    IcGlyphSearch,
    IcGlyphPower,
    IcGlyphWifi,
    IcGlyphVolume,
    IcGlyphBattery,
    IcGlyphKeyboard,
    IcGlyphBack,
    IcGlyphForward,
    IcGlyphUp,
    IcGlyphDown,
    IcGlyphChevronRight,
    IcGlyphBluetooth,
    IcGlyphPlane,
    IcGlyphMoon,
    IcGlyphSun,
    IcGlyphLeaf,
    IcGlyphGear,
    IcGlyphPlus,
    IcGlyphTrash,
    IcGlyphRename,
    IcGlyphNewFolder,
    IcGlyphRefresh,
    IcGlyphBackspace,
    IcGlyphShift,
    IcGlyphEnter,
    IcGlyphClose,
    IcGlyphBrush,
    IcGlyphEraser,
    IcGlyphSave,
    IcGlyphOpen,
    IcGlyphNew,
    IcGlyphLock,
    IcGlyphRestart,
    IcGlyphSleep,
    IcGlyphFlag,
    IcGlyphGrid,
    IcGlyphHome,
};

void drawIcon(Gfx& g, IconId id, const Rect& r, Color glyph, float battery = 1.0f);

}
