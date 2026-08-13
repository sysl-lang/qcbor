/* What `qcbor.sysl` has to be able to name, rather than only to call.
 *
 * `shim.c` is otherwise a file of definitions with no header, because sysl
 * reaches a C function through `extern` and needs no declaration for it. This
 * file exists for the one thing that is not a function: a `c const` block asks
 * the C compiler for a *value*, and the expressions it asks about are compiled
 * on their own, outside `shim.c`. So anything a constant is measured from has
 * to be declared somewhere both can include -- which is what a header is for.
 *
 * `SyslDecoder` is the whole of it. The rest of what `qcbor.sysl` measures --
 * the tag numbers, `QCBOR_HEAD_BUFFER_SIZE`, `sizeof(QCBOREncodeContext)` --
 * comes from QCBOR's own headers, which are included below and therefore come
 * with it.
 */
#ifndef SYSL_QCBOR_SHIM_H
#define SYSL_QCBOR_SHIM_H

#include "qcbor.h"

/* A caller's own string allocator, in the shape sysl can express.
 *
 * QCBOR's `QCBORStringAllocate` returns a `UsefulBuf` **by value**, and sysl has
 * no spelling for that -- a `*extern` can be called with C's convention, but the
 * type it returns has to be one sysl can name, and a two-word aggregate whose
 * layout is C's is exactly what `15 § 7` says not to transcribe.
 *
 * So the sysl side's allocator answers through out-parameters instead, and
 * `allocator_thunk` in `shim.c` is the C function QCBOR actually gets. Non-zero
 * from the sysl side means the allocation failed, which becomes the NULL buffer
 * QCBOR reads as failure.
 */
typedef int (*SyslAllocate)(void *ctx, uint8_t *old_mem, size_t new_size,
                            uint8_t **out_ptr, size_t *out_len);

/* The decoder's state: QCBOR's context and the slot the last decoded item sits
 * in, in one allocation the caller supplies.
 *
 * `QCBORItem` is a struct with two unions and a tag array, and it comes back *by
 * value* from every function that produces one. sysl can neither lay it out nor
 * receive it, and passing its two dozen fields back through out-parameters would
 * be a call with two dozen arguments that grew every time QCBOR added a type. So
 * the item lives here, beside the context, and the accessors in `shim.c` read
 * the field the caller asks for.
 *
 * **It holds pointers into itself** -- `pCurrent` and `pCurrentBounded` in the
 * context -- so a byte-for-byte copy of one leaves those pointers aimed at the
 * original. That is why the sysl side holds a pointer to the caller's storage
 * rather than the structure itself, and why the storage is `[]u64`: alignment is
 * 8 and a `[]u8` does not promise it.
 */
typedef struct {
    QCBORDecodeContext ctx;
    QCBORItem          item;
    /* The caller's allocator, if one was installed. It lives here rather than in
     * a static because a program may be decoding two things at once, and because
     * QCBOR takes one `void *` of context and this is what has to be behind it.
     */
    SyslAllocate       allocate;
    void              *allocate_ctx;
} SyslDecoder;

#endif
