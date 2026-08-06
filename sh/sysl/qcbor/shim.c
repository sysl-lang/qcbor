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

size_t sysl_qcbor_decode_words(void) {
    return (sizeof(QCBORDecodeContext) + sizeof(uint64_t) - 1) / sizeof(uint64_t);
}

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

    buf.ptr = out;
    buf.len = out_len;

    QCBOREncode_Init((QCBOREncodeContext *)storage, buf);

    return (QCBOREncodeContext *)storage;
}

QCBORDecodeContext *sysl_qcbor_decode_init(uint64_t *storage, size_t words,
                                           const uint8_t *in, size_t in_len,
                                           int mode) {
    UsefulBufC buf;

    if (words * sizeof(uint64_t) < sizeof(QCBORDecodeContext)) {
        return NULL;
    }

    buf.ptr = in;
    buf.len = in_len;

    QCBORDecode_Init((QCBORDecodeContext *)storage, buf, (QCBORDecodeMode)mode);

    return (QCBORDecodeContext *)storage;
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
 * Encoding -- a first slice, to prove the path
 * ===================================================================== */

void sysl_qcbor_encode_add_int64(QCBOREncodeContext *ctx, int64_t v) {
    QCBOREncode_AddInt64(ctx, v);
}

void sysl_qcbor_encode_add_text(QCBOREncodeContext *ctx, const uint8_t *p, size_t n) {
    UsefulBufC s;

    s.ptr = p;
    s.len = n;

    QCBOREncode_AddText(ctx, s);
}

void sysl_qcbor_encode_open_map(QCBOREncodeContext *ctx) { QCBOREncode_OpenMap(ctx); }
void sysl_qcbor_encode_close_map(QCBOREncodeContext *ctx) { QCBOREncode_CloseMap(ctx); }

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
