/* The C that a QCBOR binding cannot be written without.
 *
 * Everything here is reachable from C and from nothing else, which is why this
 * file sits beside `qcbor.sysl` rather than being transcribed into it. QCBOR
 * puts three such things in the way, and between them they account for nearly
 * every function below:
 *
 *   - **Most of QCBOR's API is `static inline`.** Of the roughly three hundred
 *     entry points its headers declare, fewer than eighty have a symbol; the
 *     rest are inline wrappers that compile into the caller. There is nothing
 *     for a linker to resolve and nothing for `extern` to name, so a shim is
 *     the only way to reach them.
 *   - **`UsefulBufC` crosses by value.** It is `{ const void *ptr; size_t len; }`
 *     and it is how QCBOR says "a string" in both directions. A struct passed
 *     in registers by the C ABI has no sysl spelling, so every one of them is
 *     split into a pointer and a length here.
 *   - **The constants are `#define`s.** `QCBOR_TYPE_TEXT_STRING`,
 *     `CBOR_TAG_DATE_EPOCH`, `QCBOR_ERR_NO_MORE_ITEMS` and the rest have no
 *     symbols. They are read from the headers here rather than copied into
 *     sysl as the numbers one machine happened to use.
 *
 * ==The two contexts are the caller's storage, and they may not be moved==
 *
 * QCBOR allocates nothing: a context is storage the caller supplies, which is
 * what lets it run on a device with no heap. This binding keeps that property
 * rather than quietly calling `malloc` here -- so `sysl_qcbor_encode_init` and
 * `sysl_qcbor_decode_init` are handed the storage and return a pointer into it.
 *
 * **Both contexts hold pointers into themselves** -- `pCurrentNesting` in the
 * encoder, `pCurrent` and `pCurrentBounded` in the decoder -- so a byte-for-byte
 * copy of one leaves those pointers aimed at the original. That is why the sysl
 * side holds a pointer to the caller's storage rather than the context itself,
 * and why the storage is `[]u64`: alignment is 8 and a `[]u8` does not promise
 * it.
 *
 * The size comes from `sizeof` here rather than from a number written in sysl,
 * and the check that the caller's storage is big enough is made here too, for
 * the same reason: `15 § 7` is explicit that a transcribed size compiles
 * everywhere and is correct on one machine.
 */
#include "qcbor.h"
#include "qcbor_spiffy_decode.h"

#include <string.h>

/* ===================================================================== *
 * Storage
 * ===================================================================== */

/* How many 64-bit words a context needs. The caller sizes an array with this
 * rather than being told a number that was right when somebody wrote it down.
 */
size_t sysl_qcbor_encode_words(void) {
    return (sizeof(QCBOREncodeContext) + sizeof(uint64_t) - 1) / sizeof(uint64_t);
}

/* The decoder's own storage is `sysl_qcbor_decoder_words`, further down, because
 * what it sizes is a wrapper holding the context *and* the item slot rather than
 * the context alone. There is deliberately no second entry point for a bare
 * QCBORDecodeContext: an item has to live somewhere, and one place to put it is
 * better than two.
 */

/* Initialize an encoding context in the caller's storage, over the caller's
 * output buffer. Answers NULL when the storage is too small, which is the one
 * failure that cannot be reported through QCBOR's own error state -- there is
 * no context yet to hold it.
 *
 * A NULL `out` with a length is QCBOR's size-calculation mode: nothing is
 * written and `Finish` reports how many bytes would have been. It is reached
 * from sysl by passing an empty output slice.
 */
QCBOREncodeContext *sysl_qcbor_encode_init(uint64_t *storage, size_t words,
                                           uint8_t *out, size_t out_len) {
    UsefulBuf buf;

    if (words * sizeof(uint64_t) < sizeof(QCBOREncodeContext)) {
        return NULL;
    }

    if (out == NULL) {
        /* Size-calculation mode is `SizeCalculateUsefulBuf`, which is not merely a
         * null pointer -- it is `{NULL, SIZE_MAX}`. A null pointer with a length
         * of zero is an ordinary buffer with no room, and every write into one
         * fails with QCBOR_ERR_BUFFER_TOO_SMALL. The constant is spelled here
         * rather than in sysl because SIZE_MAX is a macro whose value is the
         * target's, which is the whole of `15 § 7`'s argument.
         */
        buf = (UsefulBuf)SizeCalculateUsefulBuf;
    } else {
        buf.ptr = out;
        buf.len = out_len;
    }

    QCBOREncode_Init((QCBOREncodeContext *)storage, buf);

    return (QCBOREncodeContext *)storage;
}

/* ===================================================================== *
 * Errors
 *
 * QCBOR's error numbers are sparse and run to 255, with several spellings
 * sharing one value. The sysl side has an enum instead, so the translation is
 * a switch here -- which keeps every QCBOR constant in C, where the header
 * defines it, and leaves sysl holding only its own dense ordinals.
 *
 * `sysl_qcbor_error_ordinal` is total: anything unlisted becomes ORDINAL_OTHER
 * and carries its original number alongside, so a QCBOR that grows an error
 * this file has not heard of still reports something a caller can print.
 * ===================================================================== */

#define ORDINAL_SUCCESS                     0
#define ORDINAL_BUFFER_TOO_SMALL            1
#define ORDINAL_ENCODE_UNSUPPORTED          2
#define ORDINAL_BUFFER_TOO_LARGE            3
#define ORDINAL_ARRAY_NESTING_TOO_DEEP      4
#define ORDINAL_CLOSE_MISMATCH              5
#define ORDINAL_ARRAY_TOO_LONG              6
#define ORDINAL_TOO_MANY_CLOSES             7
#define ORDINAL_ARRAY_OR_MAP_STILL_OPEN     8
#define ORDINAL_OPEN_BYTE_STRING            9
#define ORDINAL_CANNOT_CANCEL              10
#define ORDINAL_EXTRA_BYTES                11
#define ORDINAL_UNSUPPORTED                12
#define ORDINAL_ARRAY_OR_MAP_UNCONSUMED    13
#define ORDINAL_BAD_INT                    14
#define ORDINAL_INDEFINITE_STRING_CHUNK    15
#define ORDINAL_HIT_END                    16
#define ORDINAL_BAD_BREAK                  17
#define ORDINAL_INPUT_TOO_LARGE            18
#define ORDINAL_ARRAY_DECODE_NESTING_DEEP  19
#define ORDINAL_ARRAY_DECODE_TOO_LONG      20
#define ORDINAL_STRING_TOO_LONG            21
#define ORDINAL_BAD_EXP_AND_MANTISSA       22
#define ORDINAL_NO_STRING_ALLOCATOR        23
#define ORDINAL_STRING_ALLOCATE            24
#define ORDINAL_MAP_LABEL_TYPE             25
#define ORDINAL_BAD_TAG_CONTENT            26
#define ORDINAL_INDEF_LEN_STRINGS_DISABLED 27
#define ORDINAL_INDEF_LEN_ARRAYS_DISABLED  28
#define ORDINAL_TAGS_DISABLED              29
#define ORDINAL_TOO_MANY_TAGS              30
#define ORDINAL_UNEXPECTED_TYPE            31
#define ORDINAL_DUPLICATE_LABEL            32
#define ORDINAL_MEM_POOL_SIZE              33
#define ORDINAL_INT_OVERFLOW               34
#define ORDINAL_DATE_OVERFLOW              35
#define ORDINAL_EXIT_MISMATCH              36
#define ORDINAL_NO_MORE_ITEMS              37
#define ORDINAL_LABEL_NOT_FOUND            38
#define ORDINAL_NUMBER_SIGN_CONVERSION     39
#define ORDINAL_CONVERSION_UNDER_OVER_FLOW 40
#define ORDINAL_MAP_NOT_ENTERED            41
#define ORDINAL_CALLBACK_FAIL              42
#define ORDINAL_FLOAT_DATE_DISABLED        43
#define ORDINAL_HALF_PRECISION_DISABLED    44
#define ORDINAL_HW_FLOAT_DISABLED          45
#define ORDINAL_FLOAT_EXCEPTION            46
#define ORDINAL_ALL_FLOAT_DISABLED         47
#define ORDINAL_RECOVERABLE_BAD_TAG        48
#define ORDINAL_CANNOT_ENTER_ALLOCATED     49
#define ORDINAL_OTHER                      50

int sysl_qcbor_error_ordinal(int err) {
    switch ((QCBORError)err) {
        case QCBOR_SUCCESS:                       return ORDINAL_SUCCESS;
        case QCBOR_ERR_BUFFER_TOO_SMALL:          return ORDINAL_BUFFER_TOO_SMALL;
        case QCBOR_ERR_ENCODE_UNSUPPORTED:        return ORDINAL_ENCODE_UNSUPPORTED;
        case QCBOR_ERR_BUFFER_TOO_LARGE:          return ORDINAL_BUFFER_TOO_LARGE;
        case QCBOR_ERR_ARRAY_NESTING_TOO_DEEP:    return ORDINAL_ARRAY_NESTING_TOO_DEEP;
        case QCBOR_ERR_CLOSE_MISMATCH:            return ORDINAL_CLOSE_MISMATCH;
        case QCBOR_ERR_ARRAY_TOO_LONG:            return ORDINAL_ARRAY_TOO_LONG;
        case QCBOR_ERR_TOO_MANY_CLOSES:           return ORDINAL_TOO_MANY_CLOSES;
        case QCBOR_ERR_ARRAY_OR_MAP_STILL_OPEN:   return ORDINAL_ARRAY_OR_MAP_STILL_OPEN;
        case QCBOR_ERR_OPEN_BYTE_STRING:          return ORDINAL_OPEN_BYTE_STRING;
        case QCBOR_ERR_CANNOT_CANCEL:             return ORDINAL_CANNOT_CANCEL;
        case QCBOR_ERR_EXTRA_BYTES:               return ORDINAL_EXTRA_BYTES;
        case QCBOR_ERR_UNSUPPORTED:               return ORDINAL_UNSUPPORTED;
        case QCBOR_ERR_ARRAY_OR_MAP_UNCONSUMED:   return ORDINAL_ARRAY_OR_MAP_UNCONSUMED;
        case QCBOR_ERR_BAD_INT:                   return ORDINAL_BAD_INT;
        case QCBOR_ERR_INDEFINITE_STRING_CHUNK:   return ORDINAL_INDEFINITE_STRING_CHUNK;
        case QCBOR_ERR_HIT_END:                   return ORDINAL_HIT_END;
        case QCBOR_ERR_BAD_BREAK:                 return ORDINAL_BAD_BREAK;
        case QCBOR_ERR_INPUT_TOO_LARGE:           return ORDINAL_INPUT_TOO_LARGE;
        case QCBOR_ERR_ARRAY_DECODE_NESTING_TOO_DEEP:
                                                  return ORDINAL_ARRAY_DECODE_NESTING_DEEP;
        case QCBOR_ERR_ARRAY_DECODE_TOO_LONG:     return ORDINAL_ARRAY_DECODE_TOO_LONG;
        case QCBOR_ERR_STRING_TOO_LONG:           return ORDINAL_STRING_TOO_LONG;
        case QCBOR_ERR_BAD_EXP_AND_MANTISSA:      return ORDINAL_BAD_EXP_AND_MANTISSA;
        case QCBOR_ERR_NO_STRING_ALLOCATOR:       return ORDINAL_NO_STRING_ALLOCATOR;
        case QCBOR_ERR_STRING_ALLOCATE:           return ORDINAL_STRING_ALLOCATE;
        case QCBOR_ERR_MAP_LABEL_TYPE:            return ORDINAL_MAP_LABEL_TYPE;
        /* QCBOR_ERR_UNRECOVERABLE_TAG_CONTENT and QCBOR_ERR_BAD_OPT_TAG are
         * two more spellings of this same value, so they cannot be separate
         * cases -- the compiler would refuse the duplicate.
         */
        case QCBOR_ERR_BAD_TAG_CONTENT:           return ORDINAL_BAD_TAG_CONTENT;
        case QCBOR_ERR_INDEF_LEN_STRINGS_DISABLED:
                                                  return ORDINAL_INDEF_LEN_STRINGS_DISABLED;
        case QCBOR_ERR_INDEF_LEN_ARRAYS_DISABLED: return ORDINAL_INDEF_LEN_ARRAYS_DISABLED;
        case QCBOR_ERR_TAGS_DISABLED:             return ORDINAL_TAGS_DISABLED;
        case QCBOR_ERR_TOO_MANY_TAGS:             return ORDINAL_TOO_MANY_TAGS;
        case QCBOR_ERR_UNEXPECTED_TYPE:           return ORDINAL_UNEXPECTED_TYPE;
        case QCBOR_ERR_DUPLICATE_LABEL:           return ORDINAL_DUPLICATE_LABEL;
        case QCBOR_ERR_MEM_POOL_SIZE:             return ORDINAL_MEM_POOL_SIZE;
        case QCBOR_ERR_INT_OVERFLOW:              return ORDINAL_INT_OVERFLOW;
        case QCBOR_ERR_DATE_OVERFLOW:             return ORDINAL_DATE_OVERFLOW;
        case QCBOR_ERR_EXIT_MISMATCH:             return ORDINAL_EXIT_MISMATCH;
        case QCBOR_ERR_NO_MORE_ITEMS:             return ORDINAL_NO_MORE_ITEMS;
        case QCBOR_ERR_LABEL_NOT_FOUND:           return ORDINAL_LABEL_NOT_FOUND;
        case QCBOR_ERR_NUMBER_SIGN_CONVERSION:    return ORDINAL_NUMBER_SIGN_CONVERSION;
        case QCBOR_ERR_CONVERSION_UNDER_OVER_FLOW:
                                                  return ORDINAL_CONVERSION_UNDER_OVER_FLOW;
        case QCBOR_ERR_MAP_NOT_ENTERED:           return ORDINAL_MAP_NOT_ENTERED;
        case QCBOR_ERR_CALLBACK_FAIL:             return ORDINAL_CALLBACK_FAIL;
        case QCBOR_ERR_FLOAT_DATE_DISABLED:       return ORDINAL_FLOAT_DATE_DISABLED;
        case QCBOR_ERR_HALF_PRECISION_DISABLED:   return ORDINAL_HALF_PRECISION_DISABLED;
        case QCBOR_ERR_HW_FLOAT_DISABLED:         return ORDINAL_HW_FLOAT_DISABLED;
        case QCBOR_ERR_FLOAT_EXCEPTION:           return ORDINAL_FLOAT_EXCEPTION;
        case QCBOR_ERR_ALL_FLOAT_DISABLED:        return ORDINAL_ALL_FLOAT_DISABLED;
        case QCBOR_ERR_RECOVERABLE_BAD_TAG_CONTENT:
                                                  return ORDINAL_RECOVERABLE_BAD_TAG;
        case QCBOR_ERR_CANNOT_ENTER_ALLOCATED_STRING:
                                                  return ORDINAL_CANNOT_ENTER_ALLOCATED;
        default:                                  return ORDINAL_OTHER;
    }
}

/* QCBOR's own text for an error number, as a NUL-terminated string in static
 * storage. Never NULL: `qcbor_err_to_str` answers "Unidentified error" for a
 * number it does not know.
 */
const char *sysl_qcbor_error_message(int err) {
    return qcbor_err_to_str((QCBORError)err);
}

int sysl_qcbor_error_is_not_well_formed(int err) {
    return QCBORDecode_IsNotWellFormedError((QCBORError)err) ? 1 : 0;
}

int sysl_qcbor_error_is_unrecoverable(int err) {
    return QCBORDecode_IsUnrecoverableError((QCBORError)err) ? 1 : 0;
}

/* ===================================================================== *
 * Encoding
 *
 * Every function here is one QCBOR call with the `UsefulBufC` arguments split
 * into a pointer and a length. Nothing else happens, deliberately: a shim that
 * made decisions would be a second implementation to keep correct.
 *
 * **The label forms are absent on purpose, and that is not a gap in coverage.**
 * QCBOR declares three spellings of nearly every operation -- the bare one,
 * `...ToMapN` for an integer label and `...ToMapSZ` for a text one -- and the
 * two labelled ones are inline and identical: they add the label as an ordinary
 * item, then add the value. So sysl gets `label` and `label_at`, and composes
 * the pairs itself, which reaches every combination rather than the ones
 * somebody thought to write out. See `qcbor.sysl`.
 *
 * `uTagRequirement` is a `bool` here rather than QCBOR's `QCBOR_ENCODE_AS_TAG`
 * / `QCBOR_ENCODE_AS_BORROWED`, since it has exactly two values and both are
 * macros. `true` writes the tag number; `false` writes the content alone, for a
 * protocol whose schema already says what the bytes mean.
 * ===================================================================== */

static UsefulBufC bufc(const uint8_t *p, size_t n) {
    UsefulBufC b;

    b.ptr = p;
    b.len = n;

    return b;
}

static uint8_t tag_req(int tagged) {
    return (uint8_t)(tagged ? QCBOR_ENCODE_AS_TAG : QCBOR_ENCODE_AS_BORROWED);
}

/* -- Numbers -------------------------------------------------------------- */

void sysl_qcbor_encode_add_int64(QCBOREncodeContext *ctx, int64_t v) {
    QCBOREncode_AddInt64(ctx, v);
}

void sysl_qcbor_encode_add_uint64(QCBOREncodeContext *ctx, uint64_t v) {
    QCBOREncode_AddUInt64(ctx, v);
}

/* Preferred serialization: a double is written in the shortest form that holds
 * its exact value, so 1.0 becomes a half-precision two-byte item. The
 * `_no_preferred` pair below writes the full width instead.
 */
void sysl_qcbor_encode_add_double(QCBOREncodeContext *ctx, double v) {
    QCBOREncode_AddDouble(ctx, v);
}

void sysl_qcbor_encode_add_float(QCBOREncodeContext *ctx, float v) {
    QCBOREncode_AddFloat(ctx, v);
}

void sysl_qcbor_encode_add_double_no_preferred(QCBOREncodeContext *ctx, double v) {
    QCBOREncode_AddDoubleNoPreferred(ctx, v);
}

void sysl_qcbor_encode_add_float_no_preferred(QCBOREncodeContext *ctx, float v) {
    QCBOREncode_AddFloatNoPreferred(ctx, v);
}

/* -- Strings -------------------------------------------------------------- */

void sysl_qcbor_encode_add_text(QCBOREncodeContext *ctx, const uint8_t *p, size_t n) {
    QCBOREncode_AddText(ctx, bufc(p, n));
}

void sysl_qcbor_encode_add_bytes(QCBOREncodeContext *ctx, const uint8_t *p, size_t n) {
    QCBOREncode_AddBytes(ctx, bufc(p, n));
}

/* The head of a byte string whose content is written straight into the output
 * buffer rather than copied through QCBOR. `place` and `place_len` come back as
 * the region to write into, and `close_bytes` then says how much was used.
 */
void sysl_qcbor_encode_open_bytes(QCBOREncodeContext *ctx, uint8_t **place, size_t *place_len) {
    UsefulBuf p;

    QCBOREncode_OpenBytes(ctx, &p);

    *place     = (uint8_t *)p.ptr;
    *place_len = p.len;
}

void sysl_qcbor_encode_close_bytes(QCBOREncodeContext *ctx, size_t amount) {
    QCBOREncode_CloseBytes(ctx, amount);
}

/* The length of a byte string with no content, for computing a size without
 * having the bytes to hand.
 */
void sysl_qcbor_encode_add_bytes_len_only(QCBOREncodeContext *ctx, const uint8_t *p, size_t n) {
    QCBOREncode_AddBytesLenOnly(ctx, bufc(p, n));
}

/* -- Simple values -------------------------------------------------------- */

void sysl_qcbor_encode_add_bool(QCBOREncodeContext *ctx, int b) {
    QCBOREncode_AddBool(ctx, b ? true : false);
}

void sysl_qcbor_encode_add_null(QCBOREncodeContext *ctx) { QCBOREncode_AddNULL(ctx); }
void sysl_qcbor_encode_add_undef(QCBOREncodeContext *ctx) { QCBOREncode_AddUndef(ctx); }

void sysl_qcbor_encode_add_simple(QCBOREncodeContext *ctx, uint8_t v) {
    QCBOREncode_AddSimple(ctx, v);
}

/* -- Tags ----------------------------------------------------------------- */

void sysl_qcbor_encode_add_tag(QCBOREncodeContext *ctx, uint64_t tag) {
    QCBOREncode_AddTag(ctx, tag);
}

void sysl_qcbor_encode_add_date_epoch(QCBOREncodeContext *ctx, int tagged, int64_t seconds) {
    QCBOREncode_AddTDateEpoch(ctx, tag_req(tagged), seconds);
}

void sysl_qcbor_encode_add_days_epoch(QCBOREncodeContext *ctx, int tagged, int64_t days) {
    QCBOREncode_AddTDaysEpoch(ctx, tag_req(tagged), days);
}

/* QCBOR takes these two as NUL-terminated strings rather than as a UsefulBufC,
 * which sysl cannot supply from a `string` without copying. Splitting the
 * pointer and length here and calling the underlying tagged-string path keeps
 * the copy out of it: `AddTDateString` is `AddTag` plus `AddSZString`, and
 * `AddSZString` is `AddText` of `strlen` bytes.
 */
void sysl_qcbor_encode_add_date_string(QCBOREncodeContext *ctx, int tagged,
                                       const uint8_t *p, size_t n) {
    if (tagged) {
        QCBOREncode_AddTag(ctx, CBOR_TAG_DATE_STRING);
    }

    QCBOREncode_AddText(ctx, bufc(p, n));
}

void sysl_qcbor_encode_add_days_string(QCBOREncodeContext *ctx, int tagged,
                                       const uint8_t *p, size_t n) {
    if (tagged) {
        QCBOREncode_AddTag(ctx, CBOR_TAG_DAYS_STRING);
    }

    QCBOREncode_AddText(ctx, bufc(p, n));
}

void sysl_qcbor_encode_add_binary_uuid(QCBOREncodeContext *ctx, int tagged,
                                       const uint8_t *p, size_t n) {
    QCBOREncode_AddTBinaryUUID(ctx, tag_req(tagged), bufc(p, n));
}

void sysl_qcbor_encode_add_positive_bignum(QCBOREncodeContext *ctx, int tagged,
                                           const uint8_t *p, size_t n) {
    QCBOREncode_AddTPositiveBignum(ctx, tag_req(tagged), bufc(p, n));
}

void sysl_qcbor_encode_add_negative_bignum(QCBOREncodeContext *ctx, int tagged,
                                           const uint8_t *p, size_t n) {
    QCBOREncode_AddTNegativeBignum(ctx, tag_req(tagged), bufc(p, n));
}

void sysl_qcbor_encode_add_decimal_fraction(QCBOREncodeContext *ctx, int tagged,
                                            int64_t mantissa, int64_t exponent) {
    QCBOREncode_AddTDecimalFraction(ctx, tag_req(tagged), mantissa, exponent);
}

void sysl_qcbor_encode_add_decimal_fraction_bignum(QCBOREncodeContext *ctx, int tagged,
                                                   const uint8_t *p, size_t n, int negative,
                                                   int64_t exponent) {
    QCBOREncode_AddTDecimalFractionBigNum(ctx, tag_req(tagged), bufc(p, n),
                                          negative ? true : false, exponent);
}

void sysl_qcbor_encode_add_big_float(QCBOREncodeContext *ctx, int tagged,
                                     int64_t mantissa, int64_t exponent) {
    QCBOREncode_AddTBigFloat(ctx, tag_req(tagged), mantissa, exponent);
}

void sysl_qcbor_encode_add_big_float_bignum(QCBOREncodeContext *ctx, int tagged,
                                            const uint8_t *p, size_t n, int negative,
                                            int64_t exponent) {
    QCBOREncode_AddTBigFloatBigNum(ctx, tag_req(tagged), bufc(p, n),
                                   negative ? true : false, exponent);
}

void sysl_qcbor_encode_add_uri(QCBOREncodeContext *ctx, int tagged,
                               const uint8_t *p, size_t n) {
    QCBOREncode_AddTURI(ctx, tag_req(tagged), bufc(p, n));
}

void sysl_qcbor_encode_add_b64_text(QCBOREncodeContext *ctx, int tagged,
                                    const uint8_t *p, size_t n) {
    QCBOREncode_AddTB64Text(ctx, tag_req(tagged), bufc(p, n));
}

void sysl_qcbor_encode_add_b64url_text(QCBOREncodeContext *ctx, int tagged,
                                       const uint8_t *p, size_t n) {
    QCBOREncode_AddTB64URLText(ctx, tag_req(tagged), bufc(p, n));
}

void sysl_qcbor_encode_add_regex(QCBOREncodeContext *ctx, int tagged,
                                 const uint8_t *p, size_t n) {
    QCBOREncode_AddTRegex(ctx, tag_req(tagged), bufc(p, n));
}

void sysl_qcbor_encode_add_mime_data(QCBOREncodeContext *ctx, int tagged,
                                     const uint8_t *p, size_t n) {
    QCBOREncode_AddTMIMEData(ctx, tag_req(tagged), bufc(p, n));
}

/* -- Structure ------------------------------------------------------------ */

void sysl_qcbor_encode_open_array(QCBOREncodeContext *ctx) { QCBOREncode_OpenArray(ctx); }
void sysl_qcbor_encode_close_array(QCBOREncodeContext *ctx) { QCBOREncode_CloseArray(ctx); }
void sysl_qcbor_encode_open_map(QCBOREncodeContext *ctx) { QCBOREncode_OpenMap(ctx); }
void sysl_qcbor_encode_close_map(QCBOREncodeContext *ctx) { QCBOREncode_CloseMap(ctx); }

void sysl_qcbor_encode_open_array_indefinite(QCBOREncodeContext *ctx) {
    QCBOREncode_OpenArrayIndefiniteLength(ctx);
}

void sysl_qcbor_encode_close_array_indefinite(QCBOREncodeContext *ctx) {
    QCBOREncode_CloseArrayIndefiniteLength(ctx);
}

void sysl_qcbor_encode_open_map_indefinite(QCBOREncodeContext *ctx) {
    QCBOREncode_OpenMapIndefiniteLength(ctx);
}

void sysl_qcbor_encode_close_map_indefinite(QCBOREncodeContext *ctx) {
    QCBOREncode_CloseMapIndefiniteLength(ctx);
}

/* A byte string whose content is itself CBOR -- how COSE wraps a payload it
 * must sign the exact bytes of.
 */
void sysl_qcbor_encode_bstr_wrap(QCBOREncodeContext *ctx) { QCBOREncode_BstrWrap(ctx); }

/* The wrapped bytes come back as an offset into the output buffer rather than
 * as a pointer, because the sysl side has the buffer and an offset is what it
 * can slice with. QCBOR hands back a pointer into that same buffer, so the
 * subtraction is exact.
 */
void sysl_qcbor_encode_close_bstr_wrap(QCBOREncodeContext *ctx, int include_head,
                                       size_t *offset, size_t *len) {
    UsefulBufC wrapped;
    const uint8_t *base = (const uint8_t *)QCBOREncode_RetrieveOutputStorage(ctx).ptr;

    QCBOREncode_CloseBstrWrap2(ctx, include_head ? true : false, &wrapped);

    if (wrapped.ptr == NULL || base == NULL) {
        *offset = 0;
        *len    = 0;
    } else {
        *offset = (size_t)((const uint8_t *)wrapped.ptr - base);
        *len    = wrapped.len;
    }
}

void sysl_qcbor_encode_cancel_bstr_wrap(QCBOREncodeContext *ctx) {
    QCBOREncode_CancelBstrWrap(ctx);
}

void sysl_qcbor_encode_add_encoded(QCBOREncodeContext *ctx, const uint8_t *p, size_t n) {
    QCBOREncode_AddEncoded(ctx, bufc(p, n));
}

/* -- Finishing and inspection --------------------------------------------- */

/* Finish, reporting the encoded length through an out-parameter and the error
 * as the result. QCBOR's own Finish hands back a UsefulBufC by value, which has
 * no sysl spelling; the pointer is the caller's buffer and is already known on
 * that side, so only the length crosses back.
 */
int sysl_qcbor_encode_finish(QCBOREncodeContext *ctx, size_t *len) {
    UsefulBufC out;
    QCBORError err = QCBOREncode_Finish(ctx, &out);

    *len = (err == QCBOR_SUCCESS) ? out.len : 0;

    return (int)err;
}

int sysl_qcbor_encode_finish_get_size(QCBOREncodeContext *ctx, size_t *len) {
    return (int)QCBOREncode_FinishGetSize(ctx, len);
}

int sysl_qcbor_encode_get_error(QCBOREncodeContext *ctx) {
    return (int)QCBOREncode_GetErrorState(ctx);
}

/* True when the encoder was given no output buffer and is only counting bytes. */
int sysl_qcbor_encode_is_buffer_null(QCBOREncodeContext *ctx) {
    return QCBOREncode_IsBufferNULL(ctx) ? 1 : 0;
}

size_t sysl_qcbor_encode_tell(QCBOREncodeContext *ctx) {
    return QCBOREncode_Tell(ctx);
}

/* The bytes written since `start`, as an offset and a length into the caller's
 * own buffer, for the same reason `close_bstr_wrap` reports one.
 */
void sysl_qcbor_encode_substring(QCBOREncodeContext *ctx, size_t start,
                                 size_t *offset, size_t *len) {
    UsefulBufC sub = QCBOREncode_SubString(ctx, start);
    const uint8_t *base = (const uint8_t *)QCBOREncode_RetrieveOutputStorage(ctx).ptr;

    if (sub.ptr == NULL || base == NULL) {
        *offset = 0;
        *len    = 0;
    } else {
        *offset = (size_t)((const uint8_t *)sub.ptr - base);
        *len    = sub.len;
    }
}

/* The head of a data item on its own -- the major type, the argument, and
 * nothing else -- for a caller assembling CBOR by hand around this binding.
 * Writes into the caller's buffer and answers how many bytes it used, or zero
 * when the buffer is smaller than QCBOR_HEAD_BUFFER_SIZE.
 */
size_t sysl_qcbor_encode_head(uint8_t *out, size_t out_len, uint8_t major_type,
                              uint8_t min_len, uint64_t number) {
    UsefulBuf buffer;
    UsefulBufC head;

    if (out_len < QCBOR_HEAD_BUFFER_SIZE) {
        return 0;
    }

    buffer.ptr = out;
    buffer.len = out_len;

    head = QCBOREncode_EncodeHead(buffer, major_type, min_len, number);

    if (head.ptr == NULL) {
        return 0;
    }

    /* QCBOR writes the head at the *end* of the buffer and returns a pointer to
     * where it started, so it is moved to the front here -- a caller who was
     * handed an offset into their own buffer would have to be told this rule,
     * and it is QCBOR's implementation detail rather than CBOR's.
     */
    memmove(out, head.ptr, head.len);

    return head.len;
}

/* The number of bytes a head can take, so a caller can size a buffer for the
 * function above without transcribing QCBOR_HEAD_BUFFER_SIZE.
 */
size_t sysl_qcbor_encode_head_max(void) { return QCBOR_HEAD_BUFFER_SIZE; }

/* ===================================================================== *
 * Tag numbers
 *
 * These are protocol constants -- RFC 8949 and the CBOR tag registry fix them
 * for all time -- but they are still `#define`s, so they have no symbol and
 * `extern` has nothing to name. Read from the header here for the reason
 * `15 § 7` gives: a number transcribed into sysl compiles everywhere and is
 * correct wherever it happened to be copied from.
 * ===================================================================== */

uint64_t sysl_qcbor_tag_date_string(void)      { return CBOR_TAG_DATE_STRING; }
uint64_t sysl_qcbor_tag_date_epoch(void)       { return CBOR_TAG_DATE_EPOCH; }
uint64_t sysl_qcbor_tag_pos_bignum(void)       { return CBOR_TAG_POS_BIGNUM; }
uint64_t sysl_qcbor_tag_neg_bignum(void)       { return CBOR_TAG_NEG_BIGNUM; }
uint64_t sysl_qcbor_tag_decimal_fraction(void) { return CBOR_TAG_DECIMAL_FRACTION; }
uint64_t sysl_qcbor_tag_bigfloat(void)         { return CBOR_TAG_BIGFLOAT; }
uint64_t sysl_qcbor_tag_cose_encrypt0(void)    { return CBOR_TAG_COSE_ENCRYPT0; }
uint64_t sysl_qcbor_tag_cose_mac0(void)        { return CBOR_TAG_COSE_MAC0; }
uint64_t sysl_qcbor_tag_cose_sign1(void)       { return CBOR_TAG_COSE_SIGN1; }
uint64_t sysl_qcbor_tag_enc_as_b64url(void)    { return CBOR_TAG_ENC_AS_B64URL; }
uint64_t sysl_qcbor_tag_enc_as_b64(void)       { return CBOR_TAG_ENC_AS_B64; }
uint64_t sysl_qcbor_tag_enc_as_b16(void)       { return CBOR_TAG_ENC_AS_B16; }
uint64_t sysl_qcbor_tag_cbor(void)             { return CBOR_TAG_CBOR; }
uint64_t sysl_qcbor_tag_uri(void)              { return CBOR_TAG_URI; }
uint64_t sysl_qcbor_tag_b64url(void)           { return CBOR_TAG_B64URL; }
uint64_t sysl_qcbor_tag_b64(void)              { return CBOR_TAG_B64; }
uint64_t sysl_qcbor_tag_regex(void)            { return CBOR_TAG_REGEX; }
uint64_t sysl_qcbor_tag_mime(void)             { return CBOR_TAG_MIME; }
uint64_t sysl_qcbor_tag_bin_uuid(void)         { return CBOR_TAG_BIN_UUID; }
uint64_t sysl_qcbor_tag_cwt(void)              { return CBOR_TAG_CWT; }
uint64_t sysl_qcbor_tag_cbor_sequence(void)    { return CBOR_TAG_CBOR_SEQUENCE; }
uint64_t sysl_qcbor_tag_cose_encrypt(void)     { return CBOR_TAG_COSE_ENCRYPT; }
uint64_t sysl_qcbor_tag_cose_mac(void)         { return CBOR_TAG_COSE_MAC; }
uint64_t sysl_qcbor_tag_cose_sign(void)        { return CBOR_TAG_COSE_SIGN; }
uint64_t sysl_qcbor_tag_days_epoch(void)       { return CBOR_TAG_DAYS_EPOCH; }
uint64_t sysl_qcbor_tag_geo_coord(void)        { return CBOR_TAG_GEO_COORD; }
uint64_t sysl_qcbor_tag_binary_mime(void)      { return CBOR_TAG_BINARY_MIME; }
uint64_t sysl_qcbor_tag_days_string(void)      { return CBOR_TAG_DAYS_STRING; }
uint64_t sysl_qcbor_tag_cbor_magic(void)       { return CBOR_TAG_CBOR_MAGIC; }
uint64_t sysl_qcbor_tag_invalid64(void)        { return CBOR_TAG_INVALID64; }

/* ===================================================================== *
 * Decoding
 *
 * ==The decoder is a context and one item, and they travel together==
 *
 * `QCBORItem` is a struct with two unions and a tag array, and it comes back
 * *by value* from every function that produces one. sysl can neither lay it out
 * nor receive it, and passing its two dozen fields back through out-parameters
 * would be a call with two dozen arguments that grew every time QCBOR added a
 * type.
 *
 * So the item lives here, beside the context, in one wrapper the sysl side holds
 * a pointer to. A decode writes into `item`, and the accessors below read the
 * field the caller asks for. That also makes the storage question the same one
 * the encoder already answered: `sysl_qcbor_decode_words` reports the size of
 * the *wrapper*, and the caller supplies it.
 *
 * The consequence worth knowing is that **there is one item slot**, so the next
 * decode overwrites the last one -- which is what QCBOR's own model does anyway,
 * since the strings it hands back point into the input rather than into the
 * item.
 * ===================================================================== */

typedef struct {
    QCBORDecodeContext ctx;
    QCBORItem          item;
} SyslDecoder;

size_t sysl_qcbor_decoder_words(void) {
    return (sizeof(SyslDecoder) + sizeof(uint64_t) - 1) / sizeof(uint64_t);
}

/* The decode modes, whose values are an enum in the header rather than macros --
 * but crossing them as a small dense integer chosen here keeps sysl from
 * depending on QCBOR's numbering the way it would if it wrote the numbers.
 */
static QCBORDecodeMode decode_mode(int mode) {
    switch (mode) {
        case 1:  return QCBOR_DECODE_MODE_MAP_STRINGS_ONLY;
        case 2:  return QCBOR_DECODE_MODE_MAP_AS_ARRAY;
        default: return QCBOR_DECODE_MODE_NORMAL;
    }
}

SyslDecoder *sysl_qcbor_decoder_init(uint64_t *storage, size_t words,
                                     const uint8_t *in, size_t in_len, int mode) {
    SyslDecoder *d;
    UsefulBufC buf;

    if (words * sizeof(uint64_t) < sizeof(SyslDecoder)) {
        return NULL;
    }

    d = (SyslDecoder *)storage;

    buf.ptr = in;
    buf.len = in_len;

    memset(&d->item, 0, sizeof(d->item));
    QCBORDecode_Init(&d->ctx, buf, decode_mode(mode));

    return d;
}

/* -- Item kinds ----------------------------------------------------------- *
 *
 * QCBOR's QCBOR_TYPE_ numbers are sparse and run past 78 with gaps. The sysl
 * side has an enum, so the translation is a switch here for the same reason the
 * error one is: every QCBOR constant stays in C, and sysl holds only its own
 * dense ordinals, in the declaration order of `ItemKind`.
 */

#define KIND_NONE                       0
#define KIND_INT                        1
#define KIND_UINT                       2
#define KIND_ARRAY                      3
#define KIND_MAP                        4
#define KIND_BYTE_STRING                5
#define KIND_TEXT_STRING                6
#define KIND_POS_BIGNUM                 7
#define KIND_NEG_BIGNUM                 8
#define KIND_DATE_STRING                9
#define KIND_DATE_EPOCH                10
#define KIND_UNKNOWN_SIMPLE            11
#define KIND_DECIMAL_FRACTION          12
#define KIND_DECIMAL_FRACTION_POS_BIG  13
#define KIND_DECIMAL_FRACTION_NEG_BIG  14
#define KIND_BIG_FLOAT                 15
#define KIND_BIG_FLOAT_POS_BIG         16
#define KIND_BIG_FLOAT_NEG_BIG         17
#define KIND_FALSE                     18
#define KIND_TRUE                      19
#define KIND_NULL                      20
#define KIND_UNDEF                     21
#define KIND_FLOAT                     22
#define KIND_DOUBLE                    23
#define KIND_MAP_AS_ARRAY              24
#define KIND_URI                       25
#define KIND_BASE64URL                 26
#define KIND_BASE64                    27
#define KIND_REGEX                     28
#define KIND_MIME                      29
#define KIND_UUID                      30
#define KIND_BINARY_MIME               31
#define KIND_DAYS_STRING               32
#define KIND_DAYS_EPOCH                33
#define KIND_OTHER                     34

static int kind_of(uint8_t type) {
    switch (type) {
        case QCBOR_TYPE_NONE:          return KIND_NONE;
        case QCBOR_TYPE_INT64:         return KIND_INT;
        case QCBOR_TYPE_UINT64:        return KIND_UINT;
        case QCBOR_TYPE_ARRAY:         return KIND_ARRAY;
        case QCBOR_TYPE_MAP:           return KIND_MAP;
        case QCBOR_TYPE_BYTE_STRING:   return KIND_BYTE_STRING;
        case QCBOR_TYPE_TEXT_STRING:   return KIND_TEXT_STRING;
        case QCBOR_TYPE_POSBIGNUM:     return KIND_POS_BIGNUM;
        case QCBOR_TYPE_NEGBIGNUM:     return KIND_NEG_BIGNUM;
        case QCBOR_TYPE_DATE_STRING:   return KIND_DATE_STRING;
        case QCBOR_TYPE_DATE_EPOCH:    return KIND_DATE_EPOCH;
        case QCBOR_TYPE_UKNOWN_SIMPLE: return KIND_UNKNOWN_SIMPLE;
        case QCBOR_TYPE_DECIMAL_FRACTION:            return KIND_DECIMAL_FRACTION;
        case QCBOR_TYPE_DECIMAL_FRACTION_POS_BIGNUM: return KIND_DECIMAL_FRACTION_POS_BIG;
        case QCBOR_TYPE_DECIMAL_FRACTION_NEG_BIGNUM: return KIND_DECIMAL_FRACTION_NEG_BIG;
        case QCBOR_TYPE_BIGFLOAT:                    return KIND_BIG_FLOAT;
        case QCBOR_TYPE_BIGFLOAT_POS_BIGNUM:         return KIND_BIG_FLOAT_POS_BIG;
        case QCBOR_TYPE_BIGFLOAT_NEG_BIGNUM:         return KIND_BIG_FLOAT_NEG_BIG;
        case QCBOR_TYPE_FALSE:         return KIND_FALSE;
        case QCBOR_TYPE_TRUE:          return KIND_TRUE;
        case QCBOR_TYPE_NULL:          return KIND_NULL;
        case QCBOR_TYPE_UNDEF:         return KIND_UNDEF;
        case QCBOR_TYPE_FLOAT:         return KIND_FLOAT;
        case QCBOR_TYPE_DOUBLE:        return KIND_DOUBLE;
        case QCBOR_TYPE_MAP_AS_ARRAY:  return KIND_MAP_AS_ARRAY;
        case QCBOR_TYPE_URI:           return KIND_URI;
        case QCBOR_TYPE_BASE64URL:     return KIND_BASE64URL;
        case QCBOR_TYPE_BASE64:        return KIND_BASE64;
        case QCBOR_TYPE_REGEX:         return KIND_REGEX;
        case QCBOR_TYPE_MIME:          return KIND_MIME;
        case QCBOR_TYPE_UUID:          return KIND_UUID;
        case QCBOR_TYPE_BINARY_MIME:   return KIND_BINARY_MIME;
        case QCBOR_TYPE_DAYS_STRING:   return KIND_DAYS_STRING;
        case QCBOR_TYPE_DAYS_EPOCH:    return KIND_DAYS_EPOCH;
        default:                       return KIND_OTHER;
    }
}

/* -- Traversal ------------------------------------------------------------ */

/* The next item in pre-order, decoded into the wrapper's slot.
 *
 * `GetNext` rather than `VGetNext`: the two differ only in whether the error is
 * returned or stashed in the context, and a binding that answers a Result wants
 * it returned. `v_next` below is the stashing one, for the spiffy sequences
 * where checking once at the end is the point.
 */
int sysl_qcbor_decode_next(SyslDecoder *d) {
    return (int)QCBORDecode_GetNext(&d->ctx, &d->item);
}

int sysl_qcbor_decode_v_next(SyslDecoder *d) {
    QCBORDecode_VGetNext(&d->ctx, &d->item);

    return (int)QCBORDecode_GetError(&d->ctx);
}

/* The next item without consuming it, so the same item comes back next time. */
int sysl_qcbor_decode_peek(SyslDecoder *d) {
    return (int)QCBORDecode_PeekNext(&d->ctx, &d->item);
}

int sysl_qcbor_decode_v_peek(SyslDecoder *d) {
    QCBORDecode_VPeekNext(&d->ctx, &d->item);

    return (int)QCBORDecode_GetError(&d->ctx);
}

/* The next item, and everything inside it if it is a map or an array -- the way
 * to skip a subtree whose content does not matter.
 */
int sysl_qcbor_decode_next_consume(SyslDecoder *d) {
    QCBORDecode_VGetNextConsume(&d->ctx, &d->item);

    return (int)QCBORDecode_GetError(&d->ctx);
}

/* -- Reading the decoded item --------------------------------------------- */

int sysl_qcbor_item_kind(const SyslDecoder *d)       { return kind_of(d->item.uDataType); }
int sysl_qcbor_item_label_kind(const SyslDecoder *d) { return kind_of(d->item.uLabelType); }

uint8_t sysl_qcbor_item_nesting(const SyslDecoder *d)      { return d->item.uNestingLevel; }
uint8_t sysl_qcbor_item_next_nesting(const SyslDecoder *d) { return d->item.uNextNestLevel; }

int64_t  sysl_qcbor_item_int64(const SyslDecoder *d)  { return d->item.val.int64; }
uint64_t sysl_qcbor_item_uint64(const SyslDecoder *d) { return d->item.val.uint64; }
double   sysl_qcbor_item_double(const SyslDecoder *d) { return d->item.val.dfnum; }
float    sysl_qcbor_item_float(const SyslDecoder *d)  { return d->item.val.fnum; }
uint8_t  sysl_qcbor_item_simple(const SyslDecoder *d) { return d->item.val.uSimple; }

/* An array's or a map's member count. UINT16_MAX means indefinite length, which
 * the sysl side turns into `None` rather than a number nothing should read.
 */
uint16_t sysl_qcbor_item_count(const SyslDecoder *d) { return d->item.val.uCount; }

int sysl_qcbor_count_is_indefinite(uint16_t count) {
    return count == QCBOR_COUNT_INDICATES_INDEFINITE_LENGTH ? 1 : 0;
}

/* A string value, or a bignum, which share the `string`/`bigNum` union member.
 * The pointer is **into the input buffer**, not into the item, so it stays valid
 * exactly as long as the input does and nothing is copied here.
 */
const uint8_t *sysl_qcbor_item_string_ptr(const SyslDecoder *d) {
    return (const uint8_t *)d->item.val.string.ptr;
}

size_t sysl_qcbor_item_string_len(const SyslDecoder *d) {
    return d->item.val.string.len;
}

int64_t sysl_qcbor_item_epoch_seconds(const SyslDecoder *d) {
    return d->item.val.epochDate.nSeconds;
}

double sysl_qcbor_item_epoch_fraction(const SyslDecoder *d) {
    return d->item.val.epochDate.fSecondsFraction;
}

int64_t sysl_qcbor_item_epoch_days(const SyslDecoder *d) { return d->item.val.epochDays; }

/* A decimal fraction or a big float, whose value is a mantissa and an exponent.
 * When the mantissa is too large for an int64 the type says so -- the four
 * `_POS_BIG` / `_NEG_BIG` kinds -- and the mantissa is then the byte string the
 * accessors above report.
 */
int64_t sysl_qcbor_item_exponent(const SyslDecoder *d) {
    return d->item.val.expAndMantissa.nExponent;
}

int64_t sysl_qcbor_item_mantissa(const SyslDecoder *d) {
    return d->item.val.expAndMantissa.Mantissa.nInt;
}

const uint8_t *sysl_qcbor_item_mantissa_ptr(const SyslDecoder *d) {
    return (const uint8_t *)d->item.val.expAndMantissa.Mantissa.bigNum.ptr;
}

size_t sysl_qcbor_item_mantissa_len(const SyslDecoder *d) {
    return d->item.val.expAndMantissa.Mantissa.bigNum.len;
}

int64_t  sysl_qcbor_item_label_int64(const SyslDecoder *d)  { return d->item.label.int64; }
uint64_t sysl_qcbor_item_label_uint64(const SyslDecoder *d) { return d->item.label.uint64; }

const uint8_t *sysl_qcbor_item_label_string_ptr(const SyslDecoder *d) {
    return (const uint8_t *)d->item.label.string.ptr;
}

size_t sysl_qcbor_item_label_string_len(const SyslDecoder *d) {
    return d->item.label.string.len;
}

/* -- Tags on the decoded item --------------------------------------------- */

/* The nth tag number on the item, innermost first, or CBOR_TAG_INVALID64 when
 * there are no more. QCBOR maps tag numbers above 16 bits into a small table, so
 * this call rather than the item's own array is what a caller must use.
 */
uint64_t sysl_qcbor_item_tag(SyslDecoder *d, uint32_t index) {
    return QCBORDecode_GetNthTag(&d->ctx, &d->item, index);
}

uint64_t sysl_qcbor_last_tag(SyslDecoder *d, uint32_t index) {
    return QCBORDecode_GetNthTagOfLast(&d->ctx, index);
}

/* How many tags an item can carry, so a caller can bound a loop without
 * transcribing QCBOR_MAX_TAGS_PER_ITEM.
 */
uint32_t sysl_qcbor_max_tags_per_item(void) { return QCBOR_MAX_TAGS_PER_ITEM; }

/* -- Position, errors and finishing --------------------------------------- */

int sysl_qcbor_decode_finish(SyslDecoder *d) {
    return (int)QCBORDecode_Finish(&d->ctx);
}

int sysl_qcbor_decode_partial_finish(SyslDecoder *d, size_t *consumed) {
    return (int)QCBORDecode_PartialFinish(&d->ctx, consumed);
}

size_t sysl_qcbor_decode_tell(SyslDecoder *d) { return QCBORDecode_Tell(&d->ctx); }

/* Whether the traversal has reached the end of the input, without consuming
 * anything to find out.
 */
int sysl_qcbor_decode_at_end(SyslDecoder *d) {
    return QCBORDecode_EndCheck(&d->ctx) == QCBOR_ERR_NO_MORE_ITEMS ? 1 : 0;
}

void sysl_qcbor_decode_rewind(SyslDecoder *d) { QCBORDecode_Rewind(&d->ctx); }

int sysl_qcbor_decode_get_error(SyslDecoder *d) {
    return (int)QCBORDecode_GetError(&d->ctx);
}

int sysl_qcbor_decode_get_and_reset_error(SyslDecoder *d) {
    return (int)QCBORDecode_GetAndResetError(&d->ctx);
}

void sysl_qcbor_decode_set_error(SyslDecoder *d, int err) {
    QCBORDecode_SetError(&d->ctx, (QCBORError)err);
}

/* The bytes of the input that have not been consumed, as an offset and a length,
 * for a caller decoding a CBOR sequence one item at a time.
 */
void sysl_qcbor_decode_undecoded(SyslDecoder *d, size_t *offset, size_t *len) {
    UsefulBufC rest = QCBORDecode_RetrieveUndecodedInput(&d->ctx);

    *offset = QCBORDecode_Tell(&d->ctx);
    *len    = rest.ptr == NULL ? 0 : rest.len - *offset;
}

/* -- The string allocator for indefinite-length strings -------------------- *
 *
 * An indefinite-length string arrives in chunks, and joining them needs storage
 * somewhere. QCBOR does not allocate, so it asks the caller for a pool -- which
 * keeps this binding allocator-free too, since the pool is an ordinary slice.
 *
 * Without one, an indefinite-length string decodes as
 * QCBOR_ERR_NO_STRING_ALLOCATOR rather than silently as something else.
 */
int sysl_qcbor_decode_set_mem_pool(SyslDecoder *d, uint8_t *pool, size_t pool_len,
                                   int all_strings) {
    UsefulBuf p;

    p.ptr = pool;
    p.len = pool_len;

    return (int)QCBORDecode_SetMemPool(&d->ctx, p, all_strings ? true : false);
}

/* The smallest pool QCBOR will accept, which is its own bookkeeping overhead. */
size_t sysl_qcbor_mem_pool_overhead(void) { return QCBOR_DECODE_MIN_MEM_POOL_SIZE; }
