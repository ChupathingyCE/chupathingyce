#ifndef MCC_SCRIPT_PARAMETERS_H
#define MCC_SCRIPT_PARAMETERS_H
struct mcc_runtime;
struct scenario;
struct data_array;
struct hs_syntax_node;
struct mcc_script_signature { short count; short types[16]; };
int mcc_parameters_prepare(struct mcc_runtime *, struct scenario *, struct data_array *);
void mcc_parameters_dispose(void);
struct mcc_script_signature const *mcc_parameters_signature(short script);
/* -1 means an ordinary Xbox/CE node; -2 is a damaged MCC parameter. */
int mcc_parameter_type(struct hs_syntax_node const *node, short *owner);
int mcc_parameter_compile(struct hs_syntax_node const *node);
int mcc_parameter_set_valid(struct hs_syntax_node const *node, struct hs_syntax_node const *value);
void mcc_parameter_compile_skipped(struct hs_syntax_node const *node, long expression_index);
#if defined(HALO_MCC) && !HALO_MCC
/* no MCC maps in this build (HALO_MCC, halo_port_features.h): the hooks
the shared code calls do nothing, their arguments unevaluated */
#define mcc_parameter_compile(a0) ((void)sizeof(a0), (int)-1)
#define mcc_parameter_compile_skipped(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (void)0)
#define mcc_parameter_set_valid(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (int)-1)
#define mcc_parameter_type(a0, a1) ((void)sizeof(a0), (void)sizeof(a1), (int)-1)
#define mcc_parameters_signature(a0) ((void)sizeof(a0), (struct mcc_script_signature const *)0)
#endif

#endif
