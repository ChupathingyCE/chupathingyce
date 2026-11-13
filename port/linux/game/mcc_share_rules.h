/* Preliminary offer policy only; no files, network or download admission.
 * Hand-placed packages only for this phase. Eligibility is not a statement
 * about redistribution rights or the safety of map bytes. */
#ifndef MCC_SHARE_RULES_H
#define MCC_SHARE_RULES_H
#include <stddef.h>
#include <stdint.h>

enum mcc_share_sidecar {
    MCC_SHARE_SIDECAR_ABSENT,      /* the package has no workshop_id.txt */
    MCC_SHARE_SIDECAR_VALID,       /* it holds a valid Workshop item id */
    MCC_SHARE_SIDECAR_UNREADABLE   /* present, but unreadable or not a valid id */
};

enum mcc_share_content {
    MCC_SHARE_CONTENT_UNKNOWN,
    MCC_SHARE_CONTENT_CUSTOM,
    MCC_SHARE_CONTENT_STOCK
};

enum mcc_share_reason {
    MCC_SHARE_ELIGIBLE,            /* passes preliminary offer policy */
    MCC_SHARE_NOT_OPTED_IN,        /* the host has not turned sharing on */
    MCC_SHARE_BAD_NAME,            /* not a safe mcc_maps identity */
    MCC_SHARE_NOT_A_PACKAGE,       /* a loose map: never offered */
    MCC_SHARE_SHARED_RESOURCES,    /* the _shared resource folder: never offered */
    MCC_SHARE_STOCK,               /* identified stock content: never offered */
    MCC_SHARE_WORKSHOP,            /* a Workshop item: the joiner is pointed to Steam */
    MCC_SHARE_UNKNOWN_ORIGIN,      /* the origin cannot be read: treated as unknown, not offered */
    MCC_SHARE_UNKNOWN_CONTENT      /* no classification or fingerprint: not offered */
};

struct mcc_share_package {
    char const *name;              /* mcc_maps\<package>\<level>, either slash */
    size_t name_length;            /* readable bytes excluding NUL; no terminator needed */
    enum mcc_share_sidecar sidecar;
    /* Classification of the exact content, established locally by the caller.
     * Names, a missing sidecar and a newly computed hash do not establish CUSTOM.
     * Do not deserialize this struct or accept these facts from a remote host. */
    enum mcc_share_content content;
    int content_known;             /* nonzero when the SHA-256 content identity is known */
    int host_opt_in;               /* nonzero when the host allows sharing */
};

enum mcc_share_reason mcc_share_decide(struct mcc_share_package const *package);
int mcc_share_eligible(struct mcc_share_package const *package);
/* A short stable word for logs and tests, such as "workshop". */
char const *mcc_share_reason_word(enum mcc_share_reason reason);
/* Preliminary resource leaf allowlist, not file validation or path containment.
 * Buffers must contain the declared number of readable bytes; no terminator
 * is required. Only bitmaps.map and sounds.map match, excluding their NUL. */
int mcc_share_file_allowed(char const *leaf, size_t length);

/* OpenCE's MCC package sharing on the wire (their 43 on; chester's): a
 * client's request, message 103, 80 bytes after the distributed header, and
 * the host's answer, 104, 144 bytes and then a payload. This build shares no
 * MCC packages (its joiners get the Workshop), so a host of ours declines a
 * request: an answer of the request's op with result 1 (sharing off), no
 * payload, so that the asking player hears at once instead of waiting 30
 * seconds on nothing. A client of ours never asks, and drops any answer. */
#define MCC_SHARE_REQUEST_KIND 103
#define MCC_SHARE_RESPONSE_KIND 104
#define MCC_SHARE_REQUEST_SIZE 80
#define MCC_SHARE_RESPONSE_SIZE 144
#define MCC_SHARE_RESULT_OK 0
#define MCC_SHARE_RESULT_OFF 1
#define MCC_SHARE_LEVEL_SIZE 64
enum mcc_share_op {
    MCC_SHARE_OP_MANIFEST = 1,
    MCC_SHARE_OP_CHUNK = 2,
    MCC_SHARE_OP_DONE = 3,
    MCC_SHARE_OP_HAVE = 4
};
/* the decline of a request (its 80 bytes: op, file, flags, reserved,
 * session, offset, length, level name) into reply (MCC_SHARE_RESPONSE_SIZE
 * bytes): 1 when the request was one (a known op, a level name with its zero
 * within its field) and reply holds the answer; 0 for anything else, which
 * is dropped unanswered. Byte by byte: the same on every machine. */
int mcc_share_decline(unsigned char const *request, size_t size, unsigned char *reply);
/* ... and what an answer says: its result (MCC_SHARE_RESULT_*), -1 when the
 * bytes are no answer (shorter than one) */
int mcc_share_answer_result(unsigned char const *reply, size_t size);

/* OpenCE's admission hold, message 105: a client checking its files
 * against the MCC identity in the host's settings asks the host to keep it
 * meanwhile. Its 204 bytes: op, generation (4 bytes each, little-endian),
 * then the identity: the 128-byte map name field, the content and metadata
 * digests (32 each), the map checksum (4). The host compares the first 192
 * against its own (the checksum is the record's own map version). */
#define MCC_ADMISSION_KIND 105
#define MCC_ADMISSION_SIZE 204
#define MCC_ADMISSION_IDENTITY_SIZE 196
#define MCC_ADMISSION_COMPARED 192
enum mcc_admission_op {
    MCC_ADMISSION_BEGIN = 1,
    MCC_ADMISSION_PING = 2,
    MCC_ADMISSION_READY = 3,
    MCC_ADMISSION_CANCEL = 4,
    MCC_ADMISSION_ACK = 5,
    MCC_ADMISSION_REFUSED = 6
};
/* (milliseconds) a hold's longest, its longest quiet, a ready machine's
 * grace */
#define MCC_ADMISSION_LIMIT 120000UL
#define MCC_ADMISSION_QUIET 20000UL
#define MCC_ADMISSION_GRACE 15000UL
enum mcc_admission_state {
    MCC_ADMISSION_NONE,
    MCC_ADMISSION_PENDING,
    MCC_ADMISSION_HELD_READY,
    MCC_ADMISSION_CLOSED
};
/* one machine slot's hold, on the host */
struct mcc_admission {
    unsigned long address;
    int state;
    uint32_t generation;
    unsigned long started;
    unsigned long heard;
    /* the host's identity when the hold opened (MCC_ADMISSION_COMPARED) */
    unsigned char identity[MCC_ADMISSION_COMPARED];
};
/* a 105 body (size bytes) from the machine at address, now, against the
 * host's identity (MCC_ADMISSION_IDENTITY_SIZE bytes; NULL while it has
 * none): the op to answer with (MCC_ADMISSION_ACK or _REFUSED), 0 for no
 * answer, -1 for a body that is none (dropped unread). Only the host's own
 * identity opens or keeps a hold, so a machine naming others can't hold its
 * slot past the limits; another machine on the slot, or another identity of
 * the host's, starts it anew. A closed hold stays closed. */
int mcc_admission_take(struct mcc_admission *hold, unsigned long address, unsigned char const *body, size_t size,
    unsigned char const *host, unsigned long now);
/* whether the machine at address is held (pending, or in its grace after
 * "ready"): not removed for having no player */
int mcc_admission_holding(struct mcc_admission const *hold, unsigned long address, unsigned long now);
#endif
