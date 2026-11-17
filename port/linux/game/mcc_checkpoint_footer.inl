/* The campaign snapshot's place in an MCC checkpoint (mcc_checkpoint.c).
 *
 * The payload keeps the snapshot's first MCC_CAMPAIGN_SNAPSHOT_V5_BYTES, the
 * size the slot was reserved for. Every byte the snapshot grew by since then
 * lives in the 96-byte footer, so no arena offset moves (the saved thread
 * pointers address fixed places):
 *
 *   +0   "MCCS"           +4   footer version (1 to 4)
 *   +8   payload bytes    +12  payload CRC32
 *   +16  cache checksum   +20  campaign bytes in the payload
 *   +24  map name: 64 bytes up to version 3, 60 bytes from version 4
 *   +84  skulls (version 4: snapshot version 7, mcc_skulls.c)
 *   +88  model ambient (versions 3 and 4: snapshot version 6)
 *   +92  footer CRC32 (versions 3 and 4)
 *
 * The footer is full. A snapshot that grows again needs a new version here,
 * and this check stops the build until it gets one. */
#define MCC_CHECKPOINT_FOOTER_VERSION 4u
#define MCC_CHECKPOINT_FOOTER_NAME_V3 64u
#define MCC_CHECKPOINT_FOOTER_NAME 60u
typedef char mcc_checkpoint_footer_holds_the_snapshot[
    MCC_CAMPAIGN_SNAPSHOT_BYTES == MCC_CAMPAIGN_SNAPSHOT_V5_BYTES + 8u ? 1 : -1];

/* the footer's room for the map name, by footer version */
static unsigned long mcc_checkpoint_footer_name_bytes(uint32_t version)
{
    return version >= 4u ? MCC_CHECKPOINT_FOOTER_NAME : MCC_CHECKPOINT_FOOTER_NAME_V3;
}

/* writes the name (its first 59 characters when it is longer) at +24 */
static void mcc_checkpoint_footer_name_write(unsigned char *footer, char const *name)
{
    unsigned long length = strlen(name);
    if (length >= MCC_CHECKPOINT_FOOTER_NAME) length = MCC_CHECKPOINT_FOOTER_NAME - 1;
    memset(footer + 24, 0, MCC_CHECKPOINT_FOOTER_NAME);
    memcpy(footer + 24, name, length);
}

/* whether the footer names this map: whole up to version 3, the first 59
 * characters from version 4 */
static int mcc_checkpoint_footer_name_matches(uint32_t version, unsigned char const *footer, char const *name)
{
    unsigned long room = mcc_checkpoint_footer_name_bytes(version);
    if (!memchr(footer + 24, 0, room)) return 0;
    return version >= 4u ? !strncmp((char const *)footer + 24, name, room - 1) :
        !strcmp((char const *)footer + 24, name);
}

/* a whole current snapshot into the payload (MCC_CAMPAIGN_SNAPSHOT_V5_BYTES)
 * and the footer's two words; 0 for a snapshot of another size */
static int mcc_checkpoint_campaign_pack(unsigned char const *snapshot, unsigned long bytes,
    unsigned char *payload, unsigned char *footer)
{
    if (bytes != MCC_CAMPAIGN_SNAPSHOT_BYTES) return 0;
    memcpy(payload, snapshot, MCC_CAMPAIGN_SNAPSHOT_V5_BYTES);
    memcpy(footer + 88, snapshot + MCC_CAMPAIGN_SNAPSHOT_V5_BYTES, 4);
    memcpy(footer + 84, snapshot + MCC_CAMPAIGN_SNAPSHOT_V6_BYTES, 4);
    return 1;
}

/* the snapshot a footer version stored, rebuilt into out (room for
 * MCC_CAMPAIGN_SNAPSHOT_BYTES): its size, or 0 when the version and the
 * payload's size don't go together */
static unsigned long mcc_checkpoint_campaign_unpack(uint32_t version, unsigned char const *payload,
    unsigned long bytes, unsigned char const *footer, unsigned char *out)
{
    if (version == 3u || version == 4u) {
        if (bytes != MCC_CAMPAIGN_SNAPSHOT_V5_BYTES) return 0;
        memcpy(out, payload, MCC_CAMPAIGN_SNAPSHOT_V5_BYTES);
        memcpy(out + MCC_CAMPAIGN_SNAPSHOT_V5_BYTES, footer + 88, 4);
        if (version == 3u) return MCC_CAMPAIGN_SNAPSHOT_V6_BYTES;
        memcpy(out + MCC_CAMPAIGN_SNAPSHOT_V6_BYTES, footer + 84, 4);
        return MCC_CAMPAIGN_SNAPSHOT_BYTES;
    }
    if (version == 2u && (bytes == MCC_CAMPAIGN_SNAPSHOT_V1_BYTES ||
        bytes == MCC_CAMPAIGN_SNAPSHOT_V4_BYTES || bytes == MCC_CAMPAIGN_SNAPSHOT_V5_BYTES)) {
        memcpy(out, payload, bytes);
        return bytes;
    }
    return 0;
}
