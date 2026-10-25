# qcbor

CBOR — [RFC 8949](https://www.rfc-editor.org/rfc/rfc8949.html) — for
[sysl](https://github.com/sysl-lang/sysl), bound to
[QCBOR](https://github.com/laurencelundblade/QCBOR) 1.6.1.

*An independent sysl package for QCBOR. It is not affiliated with or endorsed by QCBOR's authors or trademark holders.*

**Nothing has to be installed to use this.** QCBOR is about 11,000 lines of C and this package
carries them: sysl compiles a library's C as part of the build, so there is no `pkg-config`, no build
script, and nothing to apt-get. The only `@link` in the binding is `m`, the mathematics, and only the
decoder's numeric coercions reach it.

**And nothing allocates.** QCBOR calls no `malloc` anywhere — checked against the vendored source
rather than taken from its README — and this binding keeps that property: a context is storage the
caller supplies, an encoder writes into a buffer the caller supplies, and a decoded string is a view
into the input the caller supplied. CBOR exists for constrained devices, and a binding that quietly
put a heap under it would have thrown that away.

```
sh/sysl/qcbor/
    qcbor.sysl          errors, the RFC 8949 tag numbers, the shared helpers
    encode.sysl         `Encoder`
    decode.sysl         `Decoder` — traversal and the bounded getters
    item.sysl           `Item`, and the values read out of one
    tests.sysl          94 tests, mostly against RFC 8949's own table
    c/
        c.sysl          every declaration that crosses into C, and nothing else
        shim.c          the C that only C can reach
        shim.h          what a `c const` block has to be able to name
        qcbor_encode.c  vendored from laurencelundblade/QCBOR v1.6.1
        qcbor_decode.c
        UsefulBuf.c  ieee754.c  qcbor_err_to_str.c  and their headers
sysl.hocon              who this package is, and what it needs of the machine
```

**The four `.sysl` files at the top are one module**, all declaring `module sh.sysl.qcbor`, so the
split is invisible to anything importing it. What decided the boundaries is that a struct's methods
live in its declaration: `Encoder`, `Decoder` and `Item` are each too large to share a file with
another.

**Everything that is C is in `c/`** — the sub-module `sh.sysl.qcbor.c`, and the C itself. That
directory holds the `@link`, the `@include`, the three opaque contexts, every `c const` block and all
155 `extern` declarations, beside `shim.c` and QCBOR's own translation units. A call reading
`c.add_int64(h, 1)` says it is crossing into C, and nothing in the other four files reads as though
it were sysl when it is not.

The two layers have different jobs. `c` has to be **faithful** — a signature that disagrees with the
header links perfectly and corrupts the call at run time, and nothing checks one against the other.
The rest has to be **pleasant**, which is a different question and would otherwise be answered in the
same breath. A program may import `c` and use it; the split is there to make the boundary legible,
not to forbid crossing it.

**Nothing here has an `impl Drop`, and that is the point rather than an omission.** The other C
bindings in this org wrap a handle the library allocated and free it when the last reference goes.
QCBOR allocates nothing: a context is storage the caller supplied and a decoded string is a view into
the caller's input, so there is no ownership for the language to track and no `destroy` to hide.

The module is **`sh.sysl.qcbor`**, and the three directories are that name: a dotted module name
mirrors its path from the library root. The prefix is the reverse-DNS of `sysl.sh`, so that a package
claims a name nobody else will mint rather than the top-level word `qcbor`.

## Using it

Name it in your project's `sysl.hocon` and `sysl build` fetches it:

```hocon
dependencies {
  qcbor { git = "github.com/sysl-lang/qcbor", version = "0.4.3" }
}
```

The coordinate is an identity rather than a URL, so it carries no `https://`, and `version` is the
tag `v0.4.3` here. Resolution clones it, selects versions by MVS, and records what arrived in
`sysl.sum`.

Or build it into an artifact and compile against that, which needs no fetching and is what this
repository's own tests do:

```
sysl build-lib . -o /tmp/qcbor.syslib
sysl run yourprogram.sysl --lib /tmp/qcbor.syslib
```

## Encoding

```sysl
import sh.sysl.qcbor.*

var out: []u8 = [0; 256]
var e = encoder(out)

e.open_map()
e.label("name")
e.add_text("ada")
e.label("born")
e.add_int(1815)
e.label("tags")
e.open_array()
e.add_text("mathematician")
e.add_text("writer")
e.close_array()
e.close_map()

e.finish() match
    Err(why) -> print(f"could not encode: ${why}")
    Ok(n) -> send(out[0..<n])
```

**No `add` reports an error.** QCBOR remembers the first failure and turns every later call into a
no-op, so a long encoding sequence is checked once at `finish` rather than forty times on the way.
`error()` is there for a caller who wants to stop early anyway.

**A map is a flat sequence and its entries are pairs**, which is why `label` composes with every
value rather than there being an `add_int_to_map` for each. QCBOR declares three spellings of nearly
every add — the bare one, one for an integer label, one for a text label — and its two labelled ones
are inline and identical to writing the label and then the value. So this binding has `label` and
`label_at`, which reach every combination including the ones QCBOR never wrote a labelled form for.

**Give `encoder` an empty buffer and it counts instead of writing**, which is how you size a buffer
before making one: `finish_get_size()` then reports what the same calls would have taken.

## With no allocator at all

`encoder` makes the context's storage for you, which means a heap. A program that has none supplies
it instead, and **the size is a constant rather than something to call**, so it can be the length of
an ordinary array:

```sysl
var storage: [encode_words]u64 = [0; encode_words]
var out: [32]u8 = [0; 32]

encoder_with_storage(storage[0..<encode_words], out[0..<32]) match
    Err(why) -> ...
    Ok(e) -> ...
```

`decoder_with_storage` and `decoder_words` are the same on the other side. Both numbers are `sizeof`
divided by eight, asked of the C compiler at build time in a `c const` block — so they are the
target's answer and not the machine the binding was written on, and unlike a shim function reporting
one they are constants, which is what an array's length has to be.

## QCBOR's own numbering, asked of the C compiler

`ErrorKind`, `ItemKind`, `DecodeMode` and `TagRequirement` name QCBOR's constants directly. Until
0.3.0 each of them crossed as a dense ordinal that had to run down the enum's declaration order,
with a hand-written list in `shim.c` saying which QCBOR constant a position meant — two
transcriptions in two languages, and nothing comparing them. The binding's own comment admitted it:
*"the declaration order above IS the agreement with `shim.c`"*.

They are `match`es against `sh.sysl.qcbor.c` now, so a QCBOR name that does not exist is a compile
error and a variant with no arm is one too, and about 250 lines of C went with the change. What the
compiler still cannot check is that the right variant is on the right side of an arrow, so nineteen
item kinds are read back off real CBOR in the tests and the six tag-requirement numbers are pinned.

All four also print as words — `f"${item.kind()}"` says `decimal fraction with a negative bignum
mantissa` rather than `14`.

`TagRequirement.code()` answers a `u8`, which is the type QCBOR's own parameter has. It was an `int`
for one release: a `u8` answered by a six-arm match reached the C as a different value, so the shim
was widened to take an `int` and cast. That was a compiler defect — a narrow scalar crossing to C was
not extended to fill a register, as every convention but AArch64-away-from-Darwin and Windows
requires — and sysl 0.0.48 fixed it, so 0.4.0 takes the widening out again. **The binding needs
sysl 0.0.48 or newer**; on an older one the tag-requirement tests fail, which is the defect showing
itself rather than a regression here.

## Decoding, two ways

**Item by item**, when you do not know the shape:

```sysl
var d = decoder(message, Normal)

d.next() match
    Err(why) -> print(f"not CBOR: ${why}")
    Ok(item) ->
        item.kind() match
            Int -> ...
            TextString -> ...
            Map -> ...          // item.count() members follow
            _ -> ...
```

**By label**, when you do:

```sysl
var d = decoder(message, Normal)

d.enter_map()

val born = d.get_int_in_map("born")
val name = d.get_text_bytes_in_map("name")

d.exit_map()
d.finish()
```

Entering is what makes the by-label reads work: it bounds the traversal to one map, so a search has
somewhere to search. Order does not matter, and a label that is not there is `LabelNotFound` — which
is recoverable, so `take_error()` and carry on.

**A decode is not correct until `finish` says so.** Bytes left over mean the input was not one
message, and that is how a protocol notices something appended. For a CBOR *sequence*, where leftover
bytes are the next message, use `partial_finish` and `remaining` instead.

## What it binds

Everything QCBOR 1.6.1 has, with one exception noted at the end.

| | |
|---|---|
| **Numbers** | signed and unsigned integers, `f32` and `f64`, with and without RFC 8949's preferred (shortest-exact) serialization |
| **Strings** | text and byte strings; `open_bytes` writes content straight into the output buffer with no second copy |
| **Simple values** | `true`, `false`, `null`, `undefined`, and simple values by number |
| **Structure** | arrays and maps, definite and indefinite length; byte strings wrapping CBOR |
| **Tags** | any tag number, plus the registered ones by name, and QCBOR's interpreted types: epoch dates and days, date and full-date strings, positive and negative bignums, decimal fractions, big floats, URI, base64, base64url, regex, MIME, binary UUID |
| **Decoding** | item-by-item traversal with peek and subtree-skip; bounded mode with get-by-label; converting getters; several labels in one pass, with or without a callback for the unknown ones; three decode modes; CBOR sequences; a string pool or an allocator of your own |
| **Odds** | encoding a data item's head alone, `tell`/`substring`, size-calculation mode, every error QCBOR names |

**Not bound: `UsefulBuf`'s own utility functions.** `UsefulBuf_Compare`, `UsefulInputBuf` and the
rest are QCBOR's internal buffer library rather than anything to do with CBOR, and sysl's slices
already do what they do.

The deprecated 1.x tag interfaces — `SetCallerConfiguredTagList`, `GetNextWithTags`, `IsTagged` —
are also absent. They are superseded by `tag`, `tags` and `tagged_with` on a decoded item, which read
through `QCBORDecode_GetNthTag` and are correct for tag numbers wider than 16 bits where the item's
own array is not.

## Four things that will bite you, and are not this binding's doing

Each of these is pinned by a test, because each was measured rather than assumed.

**A map label with no value is not caught.** QCBOR writes half the item count, rounded down, as the
entry count — so a map holding one item encodes as `a0`, an *empty* map, with the orphan left after
it as a second top-level data item, and `finish` reports success. Nothing here defends against it,
because a binding that counted items would be a second implementation of the nesting bookkeeping and
would disagree with QCBOR's own somewhere. Every `label` gets exactly one value.

**A decoded string is a view into the input, not a copy.** That is what makes decoding
allocator-free, and it means the slice dies with the input buffer. `text()` is the one accessor that
copies, and it is where CBOR's promise that text is UTF-8 actually gets checked.

**A `substring` span does not survive the close of an enclosing array or map.** A definite-length
head is written at the close and *inserted* at the position the open happened, moving every later
byte along. QCBOR's own source says it cannot detect the misuse.

**The item callback fires on the entries you did *not* ask for.** It is not a validator for the
values being fetched — those are checked against the type each `want` named. It is where a protocol
decides what to do about a field its schema does not know.

## Upstream

Vendored from [laurencelundblade/QCBOR](https://github.com/laurencelundblade/QCBOR) at tag `v1.6.1`.
It is BSD-3-Clause; see `LICENSE`, which carries the copyright notices redistribution requires and
says which files are the binding's own rather than QCBOR's.

**One modification, and it is mechanical.** sysl compiles a tree's C with no include path of its own,
so a quoted `#include` resolves relative to the file that wrote it and to nothing else — while
QCBOR's headers say `#include "qcbor/qcbor_common.h"` from inside the very directory that holds
`qcbor_common.h`, which upstream resolves with `-Iinc` alongside `-Iinc/qcbor`. So the tree is
flattened into one directory and eleven quoted includes lose their `qcbor/` prefix. Nothing else in
the vendored source is touched.

**1.6.1 rather than 2.0.** QCBOR 2.0 is in alpha at the time of writing, and a package whose selling
point is that nothing has to be installed should not be shipping somebody's alpha.

Vendoring rather than linking is a deliberate choice with a cost worth stating: **upstream fixes do
not arrive on their own.** QCBOR is stable, widely reviewed, and moves slowly, which is what makes
that acceptable here.
