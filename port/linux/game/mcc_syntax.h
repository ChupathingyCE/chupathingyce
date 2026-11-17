#ifndef MCC_SYNTAX_H
#define MCC_SYNTAX_H

/* MCC Halo 1's cache syntax array uses signed 16-bit counts. Its final
 * addressable slot is 32766; this does not change the Xbox/CE 19001 limit. */
#define MCC_SYNTAX_CAPACITY 32767
/* Serialized 56-byte data-array header followed by 20-byte syntax nodes. */
#define MCC_SYNTAX_MAXIMUM_DATA_BYTES (56L + 20L * MCC_SYNTAX_CAPACITY)

struct data_array;
struct scenario;
struct hs_syntax_node;

/* Pre-publication validation: the caller owns and bounds the complete blob.
 * This also rejects cycles before the shared compiler follows sibling links. */
boolean mcc_syntax_graph_valid(struct data_array const *syntax, long bytes);

/* -1 for other formats; otherwise whether this active MCC scenario is sound. */
int mcc_syntax_scenario_valid(struct scenario const *scenario);

/* Return TRUE only when the active MCC scenario was handled. The callback
 * applies the engine's existing function allowlist and argument checks. All
 * traversal state belongs to MCC, including when a node is above slot 19000. */
boolean mcc_syntax_functions_check(struct scenario *scenario,
    struct data_array *syntax,
    short (*node_refusal)(struct hs_syntax_node const *, char const **),
    unsigned long *disabled_scripts, unsigned long *disabled_globals);

#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_syntax_functions_check(a0, a1, a2, a3, a4) ((void)sizeof(a0), (void)sizeof(a1), (void)sizeof(a2), (void)sizeof(a3), (void)sizeof(a4), (boolean)0)
#define mcc_syntax_scenario_valid(a0) ((void)sizeof(a0), (int)-1)
#endif

#endif
