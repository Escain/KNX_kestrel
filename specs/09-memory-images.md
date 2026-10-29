# 09 — Application configuration: evaluation and memory images

Status: implemented in `kestrel::appconfig` (2026-10-29). Checked byte for byte against a real ETS6 download
to a MEAN WELL KAA-8R (System B, application M-0162 A-0001 v2.x). The opt-in test is
`KESTREL_PRIVATE_CORPUS=/path/to/private-corpus ctest -R unit.appconfig`.

## Parameter visibility (DynamicEvaluator)
- **Values belong to the Parameter.** All of a parameter's ParameterRefs share its value. A ref's own `Value`
  is only the default it shows.
- `choose ParamRefId=R`: the choose is evaluated **only while R itself is active** (reachable). A choose on a
  hidden parameter selects nothing, even if the parameter's value matches. Real files rely on this: the
  KAA-8R's "Blinds" blocks are top-level chooses on a parameter that is only shown in blinds mode.
  The active set is computed to a fixpoint. The first pass evaluates every choose, and each later pass
  restricts to the refs that were active in the pass before.
  Exception: a ParameterRef that no `ParameterRefRef` shows anywhere is internal, and its chooses always
  count. Converted pre-ETS4 programs (the Merten MTN670804) branch on such hidden "Dummy" parameters, often
  with a single `when default="true"`; with the strict rule nothing of them would be active.
- `when test="a b"` matches any listed value. Numbers compare numerically ("1.0E+001" = "10"), and
  `<`, `<=`, `>`, `>=`, `!=` prefixes are supported. `when default="true"` applies when no other when of
  the choose matched.
- Not yet supported: `Assign` and `Rename` (reported as warnings), and ModuleDefs (the catalog warns).

## Parameter segment
1. Start from the code segment's `<Data>`, zero-filled to its `Size`.
2. Write **every** parameter that has memory at its own default `Value`, active or not. For a union, only its
   `DefaultUnionParameter` (its members share the memory). A program converted from pre-ETS4 data
   (`ConvertedFromPreEts4Data` / `PreEts4Style`) has its default image in the `<Data>`: there only parameters with
   `LegacyPatchAlways="true"` are written while inactive. Found on a Merten MTN670804: with the defaults of hidden
   parameters written it sent nothing (locking), then 0x3f instead of 1 (an 8-bit union member's default over the
   1-bit value).
3. Write every **active** ParameterRef at its value (the user's value, else the ref default, else the
   parameter default).

Encoding:
- Bit offset 0 is the most significant bit of the octet at `Offset`. Multi-octet fields are big endian.
- TypeNumber and TypeRestriction are the integer value, two's complement for signedInt.
- TypeFloat `DPT 9` uses the smallest exponent with the mantissa **rounded** (60000 → `65 B9`). IEEE-754
  single and double are also supported.
- TypeText is Latin-1, zero padded.
- Other types are reported and left untouched.

## System B tables
| Object | Content |
|---|---|
| 1 address table | u16 count, then the group addresses in ascending order |
| 2 association table | u16 count, then pairs (address index from 1, object number). Sending links come first, ordered by object number, then the other links ordered by object number and index |
| 3 group object table | u16 count (the highest object number), then 2 octets per object 1..count. Config: U 0x80, T 0x40, I 0x20, W 0x10, R 0x08, C 0x04, priority (low 3, "High" 1, "Alert" 2). Then the size code: 1–7 bit → 0–6, 1 octet 7, 2 → 8, 3 → 9, 4 → 10, 6 → 11, 8 → 12, 10 → 13, 14 → 14. Inactive objects are `00 00`. **C is only set when the object has a group address** (ETS6 behaviour) |
| 4 parameters | the parameter segment of the RelativeSegment with LoadStateMachine 4 |

Program version (PID 13 of object 4): manufacturer (2 octets), application number (2), application version (1).

Unverified on hardware: the U and I bit positions, and priorities other than low.

## Reading a device back
`decodeParameterSegments()` is the inverse. It reads the active parameters' values from segment images,
keeping only those that differ from the default (comparing encoded bits, because decoding is lossy for
floats). With the tables decoded as well, a device's configuration can be read back into a project. This is
how the golden test rebuilds the ETS6 images.
