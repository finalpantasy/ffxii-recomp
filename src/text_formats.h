#pragma once
// Game text containers as raw byte strings (host code, no guest state).
// Layouts: analysis/formats/text.md; tools/ff12text.py is the reference.
//
// Each format splits into "units": one string of an EBP2 script or st2e
// table, one entry of a word table. A unit is addressed by a key:
//   s <i>              string i (EBP2, st2e)
//   w <t> <e>          entry e of word sub-table t
//   p <k> s <i>        string i of pack section k
//   p <k> w <t> <e>    entry e of sub-table t of pack section k

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ffxii::text {

using Bytes = std::vector<uint8_t>;

enum class Kind { kNone, kEbp, kSt2e, kWords, kPack };

Kind Detect(const Bytes& d);
const char* KindName(Kind k);

// Text form (ff12text.py) to game bytes, without a terminator: UTF-8 glyphs
// from the character table, "\n" as 0x02, "{XX XX}" as raw bytes. A string
// starting with "{ex00}" becomes "ex00" + UTF-16LE (st2e menu names); the
// st2e terminator rules are applied by Rebuild. `allow_nul` permits 00 bytes
// (word entries). Returns false with `err` set on a character with no byte.
bool Encode(const std::string& text, bool allow_nul, Bytes& out, std::string& err);

// Text form per key (ff12text.py), and encoded unit bytes per key. Unit
// bytes are as stored: an EBP2 string without its terminator, an st2e string
// with it, a word entry with its zero padding (a multiple of 4 bytes).
using Replacements = std::map<std::string, std::string>;
using RawUnits = std::map<std::string, Bytes>;

// The units of d.
bool Units(const Bytes& d, RawUnits& out, std::string& err);

// Encodes text replacements for d (a word entry keeps the original's
// trailing zero count). Fails on a key d does not have or a character with
// no game byte. Keys are normalized to single spaces in `out`.
bool EncodeUnits(const Bytes& d, const Replacements& rep, RawUnits& out, std::string& err);

// d with the given units replaced, laid out as ff12text.py does: no
// replacements give back d unchanged.
bool RebuildRaw(const Bytes& d, const RawUnits& rep, Bytes& out, std::string& err);

// EncodeUnits, then RebuildRaw.
bool Rebuild(const Bytes& d, const Replacements& rep, Bytes& out, std::string& err);

}  // namespace ffxii::text
