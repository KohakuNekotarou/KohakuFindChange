//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The values an object row's transform asks first - see KFCSpecialDialogs.h.
//  LAID OUT AS INDESIGN'S OWN (the author's call: "photograph the official Move, Rotate and Scale dialogs and make
//  KFC's look like them" - work/note-scripts/2026-10-17-kfc-object-special/official-*.png): the fields on the left,
//  each label before its box; OK and Cancel stacked on the right; a value shown with its unit ("0 mm", "100%", "0" and
//  the degree sign); Move's Horizontal and Vertical in a Position panel, with Distance and Angle under a line, each
//  pair worked out from the other; Scale's chain between its two fields; Shear's Axis. What InDesign's has and these
//  have not: Copy (a transformed copy - a row would have nothing to follow) and Preview. KFC's own ways (a number alone
//  in ruler units, each object about its own centre) are said in the fields' tips, where InDesign's dialogs say
//  nothing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IExtendScriptUtils.h"		// RunScriptInEngine - the dialog, and its answer read back

// General includes:
#include "PMString.h"
#include "ScriptData.h"				// ScriptData / ScriptRecordData - the script handed over, its answer
#include "Utils.h"
#include "WideString.h"				// a text read as characters - JsLiteral

#include <cmath>					// std::fabs, std::isfinite
#include <cstdio>					// std::snprintf
#include <cstdlib>					// std::strtod
#include <string>
#include <vector>

// Project includes:
#include "KFCBoundaryID.h"			// kKFCDisplayName - the dialogs' titles are built from it
#include "KFCSpecialDialogs.h"

namespace
{
// WHAT WAS TYPED LAST, offered again for the session (the special operations spec's E12) - a distance as typed, so it
// keeps its unit; a percentage or an angle as the number it was read as. Fixed buffers: nothing to tear down when the
// plug-in goes (an ASCII text the script handed back, or the default).
const size_t kTyped = 40;
char gMoveH[kTyped] = "0";
char gMoveV[kTyped] = "0";
char gScaleX[kTyped] = "100";
char gScaleY[kTyped] = "100";
bool gScaleConstrain = true;
char gRotate[kTyped] = "0";
char gShear[kTyped] = "0";
bool gShearVertical = false;
// Narrow Down's Pages...: the range as typed, UTF-8 (a section's name can be any text) - longer ones are not kept.
const size_t kTypedPages = 256;
char gPages[kTypedPages] = "";

void Keep(char (&slot)[kTyped], const std::string& typed)
{
	if (!typed.empty())
		std::snprintf(slot, kTyped, "%s", typed.c_str());
}

// A JavaScript string literal's body for any text (UTF-8 in): printable ASCII as it is, the quote and the backslash
// escaped, every other character as \uXXXX (a surrogate pair past U+FFFF) - the script stays pure ASCII (KFCHowTo's
// rule). What the transforms keep is printable ASCII already (the script's ascii() echo, fmt()'s numbers); a page range
// can be any text but one holding a lone surrogate (RunDialog refuses it before any UTF-8 is made), so the UTF-8 kept
// reads back here.
std::string JsLiteral(const char* utf8)
{
	PMString text;
	text.SetUTF8String(utf8);
	const WideString wide(text);
	std::string out;
	char buf[16];
	for (WideString::const_iterator c = wide.begin(); c != wide.end(); ++c)
	{
		const uint32 cp = static_cast<uint32>(*c);
		if (cp == '"' || cp == '\\')
		{
			out += '\\';
			out += static_cast<char>(cp);
		}
		else if (cp >= 0x20 && cp <= 0x7E)
			out += static_cast<char>(cp);
		else if (cp > 0xFFFF)
		{
			const uint32 v = cp - 0x10000;
			std::snprintf(buf, sizeof(buf), "\\u%04X\\u%04X", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF));
			out += buf;
		}
		else
		{
			std::snprintf(buf, sizeof(buf), "\\u%04X", cp);
			out += buf;
		}
	}
	return out;
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to)
{
	for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
		s.replace(at, from.size(), to);
}

// THE DIALOG'S SCRIPT, one per operation, ASCII (the degree sign is the escape \u00B0). The shared head: the units, the
// readers and the writers of a value, ASCII-only echoes of what was typed, a field and the button column as InDesign's
// dialogs lay them out, the display name (NAME - Head writes kKFCDisplayName in); then the operation's own window. Each
// answers "ok;<x>;<y>;<typed>..." or "cancel".
// ! ExtendScript is ES3: no nested ?: (memory extendscript-nested-ternary-is-left-associative) - none is written here.
const char* const kHead = R"JS(
(function () {
  var U = { pt: 1, mm: 72 / 25.4, cm: 72 / 2.54, "in": 72, pc: 12, px: 1, q: 72 / 25.4 / 4, h: 72 / 25.4 / 4 };
  var SHOWN = { pt: "pt", mm: "mm", cm: "cm", "in": "in", pc: "pc", px: "px", q: "Q", h: "H" };
  var DEG = "\u00B0";
  var NAME = "@@NAME@@";
  function unitOf(mu) {
    try {
      if (mu == MeasurementUnits.MILLIMETERS) return "mm";
      if (mu == MeasurementUnits.CENTIMETERS) return "cm";
      if (mu == MeasurementUnits.INCHES || mu == MeasurementUnits.INCHES_DECIMAL) return "in";
      if (mu == MeasurementUnits.PICAS) return "pc";
      if (mu == MeasurementUnits.PIXELS) return "px";
      if (mu == MeasurementUnits.Q) return "q";
      if (mu == MeasurementUnits.HA) return "h";
    } catch (e) {}
    return "pt";
  }
  var hu = "pt", vu = "pt";
  try { var vp = app.activeDocument.viewPreferences; hu = unitOf(vp.horizontalMeasurementUnits); vu = unitOf(vp.verticalMeasurementUnits); } catch (e) {}
  function trim(s) { return String(s).replace(/^\s+|\s+$/g, ""); }
  // A distance in points: a number alone in the ruler's unit, or a number and its unit.
  function len(text, unit) {
    var m = trim(text).toLowerCase().match(/^([-+]?(?:\d+\.?\d*|\.\d+))\s*(pt|mm|cm|in|pc|px|q|h)?$/);
    if (!m) return null;
    return parseFloat(m[1]) * U[m[2] || unit];
  }
  // A number, with the suffix allowed after it (a regular expression's text).
  function num(text, suffix) {
    var m = trim(text).toLowerCase().match(new RegExp("^([-+]?(?:\\d+\\.?\\d*|\\.\\d+))\\s*" + suffix + "$"));
    if (!m) return null;
    return parseFloat(m[1]);
  }
  function angle(text) { return num(text, "(?:" + DEG + "|deg)?"); }
  // An angle left is written again with the degree sign ("30" becomes "30" and the sign), as InDesign's fields write it.
  function degrees(e) { var a = angle(e.text); if (a !== null) e.text = fmt(a) + DEG; }
  // A number written as InDesign's fields write one: three places at most, no trailing zeros.
  function fmt(n) { var r = Math.round(n * 1000) / 1000; if (Math.abs(r) < 0.0005) r = 0; return String(r); }
  function lenText(pts, unit) { return fmt(pts / U[unit]) + " " + SHOWN[unit]; }
  // What was typed last, shown as InDesign shows a value: a number alone with the ruler's unit after it.
  function shownLen(typed, unit) {
    var t = trim(typed);
    if (/^[-+]?(?:\d+\.?\d*|\.\d+)$/.test(t)) return t + " " + SHOWN[unit];
    return t;
  }
  function ascii(s) { s = trim(s); return /^[\x20-\x3a\x3c-\x7e]*$/.test(s) ? s : ""; }
  // A field row as InDesign's dialogs have it: the label, then the box - rows right-aligned, so the boxes line up.
  function field(parent, label, text, tip) {
    var g = parent.add("group");
    g.add("statictext", undefined, label);
    var e = g.add("edittext", undefined, text);
    e.characters = 9;
    if (tip) { e.helpTip = tip; }
    return e;
  }
  // The window: fields on the left, OK and Cancel stacked on the right. Answers the OK button.
  function frame(title) {
    var w = new Window("dialog", title);
    w.orientation = "row";
    w.alignChildren = "top";
    w.spacing = 18;
    w.margins = [18, 16, 18, 16];
    return w;
  }
  function buttons(w) {
    var b = w.add("group");
    b.orientation = "column";
    b.alignChildren = "fill";
    b.spacing = 8;
    var ok = b.add("button", undefined, "OK", { name: "ok" });
    b.add("button", undefined, "Cancel", { name: "cancel" });
    return ok;
  }
  var answer = "cancel";
)JS";

const char* const kMove = R"JS(
  var w = frame(NAME + " - Move");
  var pos = w.add("panel", undefined, "Position");
  pos.orientation = "column";
  pos.alignChildren = "right";
  pos.margins = [14, 18, 14, 12];
  var tip = "A number alone is in the document's ruler units (" + SHOWN[hu] + "), or type pt, mm, cm, in, pc, px, Q or H. Right and down are positive; every object moves by the same distance.";
  var eh = field(pos, "Horizontal:", shownLen("@@H@@", hu), tip);
  var ev = field(pos, "Vertical:", shownLen("@@V@@", vu), tip);
  var line = pos.add("panel");
  line.alignment = "fill";
  line.minimumSize.height = 1;
  line.maximumSize.height = 1;
  var ed = field(pos, "Distance:", "", "How far each object moves - Horizontal and Vertical worked out from it and the Angle.");
  var ea = field(pos, "Angle:", "", "The direction, counter-clockwise from the right - as InDesign's own Move.");
  // Each pair worked out from the other when it is left, as InDesign's Move does - and what was typed written again with
  // its unit ("10" becomes "10 mm"), as InDesign's fields write it.
  function fromXY() {
    var x = len(eh.text, hu), y = len(ev.text, vu);
    if (x === null || y === null) return;
    eh.text = lenText(x, hu);
    ev.text = lenText(y, vu);
    ed.text = lenText(Math.sqrt(x * x + y * y), hu);
    var a = 0;
    if (x !== 0 || y !== 0) a = Math.atan2(-y, x) * 180 / Math.PI;
    ea.text = fmt(a) + DEG;
  }
  function fromDA() {
    var d = len(ed.text, hu), a = angle(ea.text);
    if (d === null || a === null) return;
    var r = a * Math.PI / 180;
    ed.text = lenText(d, hu);
    ea.text = fmt(a) + DEG;
    eh.text = lenText(d * Math.cos(r), hu);
    ev.text = lenText(-d * Math.sin(r), vu);
  }
  eh.onChange = fromXY; ev.onChange = fromXY; ed.onChange = fromDA; ea.onChange = fromDA;
  fromXY();
  var ok = buttons(w);
  ok.onClick = function () {
    var x = len(eh.text, hu), y = len(ev.text, vu);
    if (x === null || y === null || !isFinite(x) || !isFinite(y)) {
      alert("Type a distance - a number, or a number and one of pt, mm, cm, in, pc, px, Q, H.");
      return;
    }
    answer = "ok;" + x + ";" + y + ";" + ascii(eh.text) + ";" + ascii(ev.text);
    w.close(1);
  };
  w.show();
  return answer;
})();
)JS";

const char* const kScale = R"JS(
  var w = frame(NAME + " - Scale");
  var g = w.add("group");
  g.orientation = "row";
  g.alignChildren = "center";
  g.spacing = 6;
  var col = g.add("group");
  col.orientation = "column";
  col.alignChildren = "right";
  var tip = "In percent. Each object is scaled about its own centre.";
  var ex = field(col, "Scale X:", "@@X@@%", tip);
  var ey = field(col, "Scale Y:", "@@Y@@%", tip);
  // THE CHAIN, as InDesign's own (the author: "the icon and what it does change as it is pressed" - and "the same as the
  // official"): linked, the chain whole on its square and the two values one; unlinked, the chain broken and each value
  // its own; a square under the pointer and a blue ring with the keyboard focus, as InDesign's button draws them - drawn
  // from that button, photographed in each colour theme and state (kChain).
@@CHAIN@@
  var first = null;
  try { first = chainImage(@@C@@, false, false); } catch (e1) {}
  var chain = g.add("iconbutton", undefined, first, { style: "toolbutton", toggle: true });
  chain.preferredSize = [20, 20];
  chain.value = @@C@@;
  chain.helpTip = "Constrain proportions for scaling";
  chain.onDraw = function (state) {
    try {
      var hover = false, focus = false;
      if (state) { hover = state.mouseOver; focus = state.hasFocus; }
      var img = chainImage(this.value, hover, focus);
      if (img) this.graphics.drawImage(img, (this.size[0] - 20) / 2, (this.size[1] - 20) / 2);
    } catch (e2) {}
  };
  ex.onChanging = function () { if (chain.value) ey.text = ex.text; };
  ey.onChanging = function () { if (chain.value) ex.text = ey.text; };
  chain.onClick = function () { if (chain.value) ey.text = ex.text; };
  // A value left is written again with its sign ("50" becomes "50%"), as InDesign's fields write it.
  function percent(e) { var v = num(e.text, "%?"); if (v !== null) e.text = fmt(v) + "%"; }
  ex.onChange = function () { percent(ex); if (chain.value) ey.text = ex.text; };
  ey.onChange = function () { percent(ey); if (chain.value) ex.text = ey.text; };
  var ok = buttons(w);
  ok.onClick = function () {
    var x = num(ex.text, "%?"), y = num(ey.text, "%?");
    if (chain.value) y = x;
    if (x === null || y === null || x === 0 || y === 0 || !isFinite(x) || !isFinite(y)) {
      alert("Type a percentage other than 0.");
      return;
    }
    answer = "ok;" + x + ";" + y + ";" + fmt(x) + ";" + fmt(y) + ";" + (chain.value ? "1" : "0");
    w.close(1);
  };
  w.show();
  return answer;
})();
)JS";

// THE CHAIN, for Scale's script (@@CHAIN@@): InDesign's own chain button, drawn as InDesign draws it. Not the SDK's link
// icons (source/open/icons) nor the app's own PNG resources 296 / 297 (Required/(Application UI Resources)/idrc_PNGA and
// idrc_PNGD - on screen InDesign draws the button from the SVG ones, idrc_SVGA, which come out a little differently): the
// button was photographed in InDesign's Scale dialog at the four colour-theme presets in every state it draws - at rest,
// under the pointer, with the keyboard focus, linked and unlinked - and ONE shape (the square's ring and inside, the two
// chains: coverage masks) with each theme's colours was fitted to those photographs; drawn back through this script's own
// arithmetic, every one of them comes out within 2 greys. The script builds the state's PNG from them when it is first
// drawn. ! Two of InDesign's drawings were left out: its first drawing after a theme change, still in the old theme's chain
// colour until the button is pressed, and the button held down (not photographed - drawn as under the pointer).
// Made by work/note-scripts/2026-10-17-kfc-object-special/chain_js2.py (photographs: theme_states.ps1; fit: chain_model2.py).
const char* const kChain = R"JS(
  // The button's shape, 20 x 20, one hex pair (0..255) a pixel: the square's ring and its inside (one shape for every
  // state), the chain whole and the chain broken.
  var CH_RING = "20a0ffffffffffffffffffffffffffffffffa820b07f01ffffffffffffffffffffffffffff016dd0ff01ffffffffffffffffffffffffffffffff01ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff01ffffffffffffffffffffffffffffffff06ffd04702ffffffffffffffffffffffffffff0832e740e0ffffffffffffffffffffffffffffffffe749";
  var CH_FILL = "0001000000000000000000000000000000000200000ea2ffffffffffffffffffffffffffffa6200100afffffffffffffffffffffffffffffffffcf0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000ffffffffffffffffffffffffffffffffffff0000cfffffffffffffffffffffffffffffffffe4000133e2fffffffffffffffffffffffffffff159000202000000000000000000000000000000000000";
  var CH_LINK = "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000004fb8b85f00000000000000000000000000000058ffd0d0ff760000000000000000000000000000c1c70000a7df0000000000000000000000000000ff7f00007fff0000000000000000000000000000b80f40207fff000000000000000000000000000018d0ff7f7fff0000000000000000000000000000a7f75f207fff0000000000000000000000000000f0980018d0e70000000000000000000000000000ff7f7fffff5f0000000000000000000000000000ff7f407f40470000000000000000000000000000ff7f000067ff0000000000000000000000000000d8a0000089f0000000000000000000000000000089ff9089f7a0000000000000000000000000000008a0ffffa708000000000000000000000000000000000f0f00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
  var CH_BREAK = "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000050b6c060000000000000000000000039e72f0050ffd0d0ff780000000000000000000008b6ef2f60c60000a7d6000000000000000000000008b6ef2f1000007fff00000000000000000000000008b6ef2f00007fff0000000000000000000000000008b6ef2f007fff000000000000000000000000000008b6ef2f50ff0000000000000000000000000000a710b6ef2f500000000000000000000000000000ff7808b6ef2f0000000000000000000000000000ff7f0008b6ef2f00000000000000000000000000ff7f000008b6ef2f000000000000000000000000d69f00004910b6ef2f000000000000000000000078ff8f87f78f08b6ef0800000000000000000000009fffffc01000084900000000000000000000000000002000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";
  // InDesign's colours for it at the four colour-theme presets (uiBrightnessPreference): the chain; the linked square's ring
  // and inside; the unlinked one's under the pointer; the inside with the keyboard focus; the focus ring.
  var CH_THEMES = [
    { at: 0, glyph: 0xb6, lring: 0x3e, lin: 0x1f, hring: 0x47, hin: 0x29, fin: 0x2a, blue: [0x46, 0xa0, 0xf5] },
    { at: 0.5, glyph: 0xc2, lring: 0x56, lin: 0x30, hring: 0x59, hin: 0x3d, fin: 0x45, blue: [0x46, 0xa0, 0xf5] },
    { at: 0.51, glyph: 0x40, lring: 0x83, lin: 0x96, hring: 0xb2, hin: 0xdc, fin: 0xdf, blue: [0x0f, 0x67, 0xd2] },
    { at: 1, glyph: 0x53, lring: 0xa7, lin: 0xbd, hring: 0xbd, hin: 0xf9, fin: 0xfb, blue: [0x14, 0x76, 0xe6] }
  ];
  // The colours for the theme now. Between two presets on one side (dark 0..0.50, light 0.51..1) they are taken in
  // proportion - a guess: InDesign's own between its presets were not measured.
  function chainColours() {
    var b = 0.5;
    try { b = app.generalPreferences.uiBrightnessPreference; } catch (e0) {}
    var lo = CH_THEMES[0], hi = CH_THEMES[1];
    if (b > 0.5) { lo = CH_THEMES[2]; hi = CH_THEMES[3]; }
    var t = (b - lo.at) / (hi.at - lo.at);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    var out = {}, k;
    for (k in lo) { if (k != "at" && k != "blue") out[k] = lo[k] + (hi[k] - lo[k]) * t; }
    out.blue = [lo.blue[0] + (hi.blue[0] - lo.blue[0]) * t, lo.blue[1] + (hi.blue[1] - lo.blue[1]) * t, lo.blue[2] + (hi.blue[2] - lo.blue[2]) * t];
    return out;
  }
  // A PNG of 20 x 20 straight RGBA (px: 1600 numbers), for ScriptUI.newImage: zlib's stored block - no compression - and
  // each chunk's CRC-32.
  var CH_CRC = [];
  (function () {
    for (var n = 0; n < 256; n++) {
      var c = n;
      for (var k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
      CH_CRC[n] = c;
    }
  })();
  function chU32(n) { return String.fromCharCode((n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255); }
  function chChunk(type, data) {
    var s = type + data, c = -1;
    for (var i = 0; i < s.length; i++) c = CH_CRC[(c ^ s.charCodeAt(i)) & 255] ^ (c >>> 8);
    return chU32(data.length) + s + chU32((c ^ -1) >>> 0);
  }
  function chPng(px) {
    var raw = "", a = 1, b = 0, i;
    for (var y = 0; y < 20; y++) {
      raw += "\x00";
      for (var x = 0; x < 80; x++) raw += String.fromCharCode(px[y * 80 + x]);
    }
    for (i = 0; i < raw.length; i++) { a = (a + raw.charCodeAt(i)) % 65521; b = (b + a) % 65521; }
    var n = raw.length;
    var z = "\x78\x01\x01" + String.fromCharCode(n & 255, (n >>> 8) & 255, ~n & 255, (~n >>> 8) & 255) + raw + chU32(((b << 16) | a) >>> 0);
    return "\x89PNG\r\n\x1A\n" + chChunk("IHDR", chU32(20) + chU32(20) + "\x08\x06\x00\x00\x00") + chChunk("IDAT", z) + chChunk("IEND", "");
  }
  // The button as InDesign draws it in this state, made once: linked - the square (grey ring) and the chain whole;
  // unlinked - the chain broken, a square only under the pointer or with the focus; the keyboard focus - a blue ring.
  var CH_IMG = {};
  function chainImage(linked, hover, focus) {
    var key = (linked ? "L" : "U") + (hover ? "H" : "") + (focus ? "F" : "");
    if (CH_IMG[key]) return CH_IMG[key];
    var c = chainColours(), ring = null, inside = 0;
    if (focus) ring = c.blue;
    else if (linked) ring = [c.lring, c.lring, c.lring];
    else if (hover) ring = [c.hring, c.hring, c.hring];
    if (linked) inside = c.lin;
    else if (hover) inside = c.hin;
    else if (focus) inside = c.fin;
    var glyph = CH_BREAK;
    if (linked) glyph = CH_LINK;
    var px = [];
    for (var i = 0; i < 400; i++) {
      var r = 0, g = 0, bl = 0, al = 0, o;
      if (ring) {
        o = parseInt(CH_RING.substr(i * 2, 2), 16) / 255;
        r = ring[0] * o + r * (1 - o); g = ring[1] * o + g * (1 - o); bl = ring[2] * o + bl * (1 - o); al = o + al * (1 - o);
        o = parseInt(CH_FILL.substr(i * 2, 2), 16) / 255;
        r = inside * o + r * (1 - o); g = inside * o + g * (1 - o); bl = inside * o + bl * (1 - o); al = o + al * (1 - o);
      }
      o = parseInt(glyph.substr(i * 2, 2), 16) / 255;
      r = c.glyph * o + r * (1 - o); g = c.glyph * o + g * (1 - o); bl = c.glyph * o + bl * (1 - o); al = o + al * (1 - o);
      if (al > 0) px.push(Math.floor(r / al + 0.5), Math.floor(g / al + 0.5), Math.floor(bl / al + 0.5), Math.floor(al * 255 + 0.5));
      else px.push(0, 0, 0, 0);
    }
    CH_IMG[key] = ScriptUI.newImage(chPng(px));
    return CH_IMG[key];
  }
)JS";

const char* const kRotate = R"JS(
  var w = frame(NAME + " - Rotate");
  var col = w.add("group");
  col.orientation = "column";
  col.alignChildren = "right";
  var ea = field(col, "Angle:", "@@A@@" + DEG, "Counter-clockwise is positive, as in InDesign's own Rotate. Each object turns about its own centre.");
  ea.onChange = function () { degrees(ea); };
  var ok = buttons(w);
  ok.onClick = function () {
    var a = angle(ea.text);
    if (a === null || !isFinite(a)) { alert("Type an angle in degrees."); return; }
    answer = "ok;" + a + ";0;" + fmt(a);
    w.close(1);
  };
  w.show();
  return answer;
})();
)JS";

const char* const kShear = R"JS(
  var w = frame(NAME + " - Shear");
  var col = w.add("group");
  col.orientation = "column";
  col.alignChildren = "left";
  col.spacing = 10;
  var ea = field(col, "Shear Angle:", "@@A@@" + DEG, "Between -90 and 90 degrees. Each object is sheared about its own centre.");
  ea.onChange = function () { degrees(ea); };
  var axis = col.add("panel", undefined, "Axis");
  axis.orientation = "column";
  axis.alignChildren = "left";
  axis.alignment = "fill";
  axis.margins = [14, 18, 14, 10];
  var rh = axis.add("radiobutton", undefined, "Horizontal");
  var rv = axis.add("radiobutton", undefined, "Vertical");
  rv.value = @@V@@;
  rh.value = !rv.value;
  var ok = buttons(w);
  ok.onClick = function () {
    var a = angle(ea.text);
    if (a === null || a <= -90 || a >= 90) { alert("Type an angle between -90 and 90 degrees."); return; }
    answer = "ok;" + a + ";" + (rv.value ? "1" : "0") + ";" + fmt(a);
    w.close(1);
  };
  w.show();
  return answer;
})();
)JS";

const char* const kPages = R"JS(
  var w = frame(NAME + " - Narrow Down");
  var col = w.add("group");
  col.orientation = "column";
  col.alignChildren = "left";
  col.spacing = 6;
  var e = field(col, "Pages:", "@@P@@", "As the Print dialog's Range: 1-5, 8 - a section's pages by its name (Sec1:3), absolute numbers with + (+3). Each document reads it by its own page numbers.");
  e.characters = 18;
  col.add("statictext", undefined, "e.g. 1-5, 8");
  var ok = buttons(w);
  e.active = true;
  // A lone surrogate (a pasted text can carry one) does not reach the plug-in whole - its UTF-8 throws or turns into
  // another character - so a text holding one is refused as an empty one is.
  function lone(s) {
    for (var i = 0; i < s.length; i++) {
      var c = s.charCodeAt(i);
      if (c >= 0xD800 && c <= 0xDBFF) {
        var d = s.charCodeAt(i + 1);
        if (d >= 0xDC00 && d <= 0xDFFF) { i++; continue; }
        return true;
      }
      if (c >= 0xDC00 && c <= 0xDFFF) return true;
    }
    return false;
  }
  ok.onClick = function () {
    var t = trim(e.text);
    if (t === "" || lone(t)) { alert("Type the pages - for example 1-5, 8."); return; }
    answer = "ok;" + t;
    w.close(1);
  };
  w.show();
  return answer;
})();
)JS";

// The answer's fields, split at ';'.
std::vector<std::string> Fields(const std::string& answer)
{
	std::vector<std::string> out;
	size_t from = 0;
	for (size_t at = answer.find(';'); ; at = answer.find(';', from))
	{
		out.push_back(answer.substr(from, at == std::string::npos ? std::string::npos : at - from));
		if (at == std::string::npos)
			break;
		from = at + 1;
	}
	return out;
}

// The scripts' shared head (kHead), the display name written in (kKFCDisplayName - the one definition of the name).
std::string Head()
{
	std::string head(kHead);
	ReplaceAll(head, "@@NAME@@", JsLiteral(kKFCDisplayName));
	return head;
}

// A DIALOG'S SCRIPT RUN THROUGH THE PLUG-IN'S OWN ENGINE (KFCHowTo's - "KFC", out of the user's
// #targetengine sessions), its answer read back: the script handed as ScriptData with no arguments, its value the
// result, an error its string (Adobe's own code calls RunScriptInEngine nowhere -
// docs/ai-notes/api-official-examples.md); no alert of the runner's own - the caller says what went wrong, in its own
// words.
enum DialogRun
{
	kDialogAnswered = 0,
	kDialogNoEngine,		// no script engine to run it in
	kDialogFailed,			// it ran into an error, or answered nothing that reads as text
	kDialogBroken			// it answered a text holding a lone surrogate (HasLoneSurrogate)
};

// A LONE SURROGATE in a text, its UTF-16 read as it stands: GetUTF8String takes a lone high one together with the
// character after it (or, at the end, makes something else of it), and SetUTF8String throws on a lone low one's UTF-8
// (measured - KT's u8trip probe, branch exp/2026-10-23-u8trip) - so no UTF-8 is made of a text holding one.
bool HasLoneSurrogate(const PMString& s)
{
	int32 n = 0;
	const UTF16TextChar* u = s.GrabUTF16Buffer(&n);
	for (int32 i = 0; u != nil && i < n; ++i)
	{
		if (u[i] >= 0xD800 && u[i] <= 0xDBFF)
		{
			if (i + 1 < n && u[i + 1] >= 0xDC00 && u[i + 1] <= 0xDFFF)
			{
				++i;
				continue;
			}
			return true;
		}
		if (u[i] >= 0xDC00 && u[i] <= 0xDFFF)
			return true;
	}
	return false;
}

DialogRun RunDialog(const std::string& js, std::string& outAnswer)
{
	Utils<IExtendScriptUtils> esUtils;
	if (!esUtils.Exists())
		return kDialogNoEngine;
	PMString scriptText;
	scriptText.SetTranslatable(kFalse);
	scriptText.SetUTF8String(js);
	ScriptData script;
	script.SetPMString(scriptText);
	ScriptRecordData arguments;
	ScriptData result;
	PMString errorString;
	const ErrorCode ran = esUtils->RunScriptInEngine("KFC", script, arguments, result, errorString,
		kFalse /*showErrorAlert*/, kFalse /*invokeDebugger*/);
	PMString answerText;
	if (ran != kSuccess || !errorString.IsEmpty() || result.GetType() != ScriptData::s_string
		|| result.GetPMString(answerText) != kSuccess)
		return kDialogFailed;
	if (HasLoneSurrogate(answerText))
		return kDialogBroken;
	outAnswer = answerText.GetUTF8String();
	return kDialogAnswered;
}
}	// anonymous namespace

const char* KFCSpecialDialogs::OpName(int32 op)
{
	switch (op)
	{
		case kKFCSpecialMove:	return "Move";
		case kKFCSpecialScale:	return "Scale";
		case kKFCSpecialRotate:	return "Rotate";
		case kKFCSpecialShear:	return "Shear";
		default:				return "";
	}
}

bool KFCSpecialDialogs::NothingToDo(int32 op, const KFCSpecialParams& params)
{
	const double kTiny = 1e-9;
	switch (op)
	{
		case kKFCSpecialMove:	return std::fabs(params.x) < kTiny && std::fabs(params.y) < kTiny;
		case kKFCSpecialScale:	return std::fabs(params.x - 100.0) < kTiny && std::fabs(params.y - 100.0) < kTiny;
		case kKFCSpecialRotate:
		case kKFCSpecialShear:	return std::fabs(params.x) < kTiny;		// a shear of 0 is nothing on either axis
		default:				return false;
	}
}

bool KFCSpecialDialogs::AskPageRange(PMString& io, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	std::string js(Head());
	std::string body(kPages);
	ReplaceAll(body, "@@P@@", JsLiteral(gPages));
	js += body;
	std::string answer;
	const DialogRun run = RunDialog(js, answer);
	if (run != kDialogAnswered)
	{
		if (run == kDialogNoEngine)
			outWhy.Append("Narrow Down: the dialog could not be shown (no script engine) - nothing was searched.");
		else if (run == kDialogBroken)
			outWhy.Append("Narrow Down: the pages typed hold a broken character - nothing was searched.");
		else
			outWhy.Append("Narrow Down: the dialog could not be shown - nothing was searched.");
		return false;
	}
	// "ok;<the range>" - the range is everything after the first ';' (it holds no ';' of its own that matters: a range is
	// separated by commas).
	if (answer.compare(0, 3, "ok;") != 0)
		return false;		// Cancel: nothing searched, nothing said
	const std::string typed(answer.substr(3));
	io.SetUTF8String(typed);
	io.SetTranslatable(kFalse);
	if (typed.size() < kTypedPages)
		std::snprintf(gPages, kTypedPages, "%s", typed.c_str());
	return !io.IsEmpty();
}

bool KFCSpecialDialogs::AskTransformValues(int32 op, KFCSpecialParams& io, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	std::string js(Head());
	switch (op)
	{
		case kKFCSpecialMove:
		{
			std::string body(kMove);
			ReplaceAll(body, "@@H@@", JsLiteral(gMoveH));
			ReplaceAll(body, "@@V@@", JsLiteral(gMoveV));
			js += body;
			break;
		}
		case kKFCSpecialScale:
		{
			std::string body(kScale);
			ReplaceAll(body, "@@X@@", JsLiteral(gScaleX));
			ReplaceAll(body, "@@Y@@", JsLiteral(gScaleY));
			ReplaceAll(body, "@@C@@", gScaleConstrain ? "true" : "false");
			ReplaceAll(body, "@@CHAIN@@", kChain);
			js += body;
			break;
		}
		case kKFCSpecialRotate:
		{
			std::string body(kRotate);
			ReplaceAll(body, "@@A@@", JsLiteral(gRotate));
			js += body;
			break;
		}
		case kKFCSpecialShear:
		{
			std::string body(kShear);
			ReplaceAll(body, "@@A@@", JsLiteral(gShear));
			ReplaceAll(body, "@@V@@", gShearVertical ? "true" : "false");
			js += body;
			break;
		}
		default:
			return false;
	}

	std::string answer;
	const DialogRun run = RunDialog(js, answer);
	if (run != kDialogAnswered)
	{
		outWhy = OpName(op);
		outWhy.Append(run == kDialogNoEngine
			? ": the dialog could not be shown (no script engine) - nothing was changed."
			: ": the dialog could not be shown - nothing was changed.");
		outWhy.SetTranslatable(kFalse);
		return false;
	}
	const std::vector<std::string> f = Fields(answer);
	if (f.size() < 3 || f[0] != "ok")
		return false;		// Cancel: nothing written, nothing said
	io.x = std::strtod(f[1].c_str(), nil);
	io.y = std::strtod(f[2].c_str(), nil);
	// NOT FINITE - refused (the script's own OK asks isFinite; an answer of "Infinity" reads back as inf, and no
	// transform can be made of it - KFCObjectSpecial's doors measure what a finite one would do).
	if (!std::isfinite(io.x) || !std::isfinite(io.y))
	{
		outWhy = OpName(op);
		outWhy.Append(": the values are too large - nothing was changed.");
		outWhy.SetTranslatable(kFalse);
		return false;
	}
	// What was typed, kept for the next one (an empty echo - not ASCII - keeps the last).
	switch (op)
	{
		case kKFCSpecialMove:
			if (f.size() >= 5) { Keep(gMoveH, f[3]); Keep(gMoveV, f[4]); }
			break;
		case kKFCSpecialScale:
			if (f.size() >= 6) { Keep(gScaleX, f[3]); Keep(gScaleY, f[4]); gScaleConstrain = (f[5] == "1"); }
			break;
		case kKFCSpecialRotate:
			if (f.size() >= 4) Keep(gRotate, f[3]);
			break;
		case kKFCSpecialShear:
			if (f.size() >= 4) Keep(gShear, f[3]);
			gShearVertical = (io.y > 0.5);
			break;
		default:
			break;
	}
	return true;
}

// End, KFCSpecialDialogs.cpp.
