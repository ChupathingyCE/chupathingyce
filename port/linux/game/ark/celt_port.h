/*
CELT_PORT.H

(port) Included before every file of libcelt 0.11 (PORT.md) and before its
celt.h wherever the game uses it: the library's external names given a
celt011_ prefix, so that they do not meet Opus's CELT of the same names
(port/third_party/opus: ec_dec_init, quant_all_bands and the rest are in
both, and differ), and its allocations made from the port's own (FMOD's
mode for MCC's banks is made once, fsb_celt.c: nothing is freed).

Generated from the names the library's objects define (nm); a name
defined by a configuration this build does not use is harmless here.
*/

#ifndef __CELT_PORT_H
#define __CELT_PORT_H

/* the library's configuration (its config.h, which it is not given) */
#ifndef CELT_BUILD
#define CELT_BUILD 1
#endif
#ifndef VAR_ARRAYS
#define VAR_ARRAYS 1
#endif
#ifndef CUSTOM_MODES
#define CUSTOM_MODES 1
#endif
/* the configuration FMOD's frames are decoded with (the one Opus's
integration of CELT 0.11 used, and the only one that decodes every frame of
MCC's banks): the pitch postfilter on, and the decoder's signalling a
setting (fsb_celt.c switches it off: FMOD's frames carry no
table-of-contents byte). Built without it, the postfilter is off and the
signalling always on, and two frames of five in a dialogue stream do not
decode */
#ifndef OPUS_BUILD
#define OPUS_BUILD 1
#endif
#ifndef ENABLE_POSTFILTER
#define ENABLE_POSTFILTER 1
#endif

/* allocation: celt011_alloc (zeroed memory) and celt011_free, the port's */
#define OVERRIDE_CELT_ALLOC 1
#define OVERRIDE_CELT_FREE 1
#define celt_alloc celt011_alloc
#define celt_free celt011_free
void *celt011_alloc(int size);
void celt011_free(void *pointer);

#define alg_quant celt011_alg_quant
#define alg_unquant celt011_alg_unquant
#define amp2Log2 celt011_amp2Log2
#define anti_collapse celt011_anti_collapse
#define _celt_autocorr celt011__celt_autocorr
#define celt_decode celt011_celt_decode
#define celt_decode_float celt011_celt_decode_float
#define celt_decoder_create celt011_celt_decoder_create
#define celt_decoder_create_custom celt011_celt_decoder_create_custom
#define celt_decoder_ctl celt011_celt_decoder_ctl
#define celt_decoder_destroy celt011_celt_decoder_destroy
#define celt_decoder_get_size celt011_celt_decoder_get_size
#define celt_decoder_get_size_custom celt011_celt_decoder_get_size_custom
#define celt_decoder_init celt011_celt_decoder_init
#define celt_decoder_init_custom celt011_celt_decoder_init_custom
#define celt_encode celt011_celt_encode
#define celt_encode_float celt011_celt_encode_float
#define celt_encoder_create celt011_celt_encoder_create
#define celt_encoder_create_custom celt011_celt_encoder_create_custom
#define celt_encoder_ctl celt011_celt_encoder_ctl
#define celt_encoder_destroy celt011_celt_encoder_destroy
#define celt_encoder_get_size celt011_celt_encoder_get_size
#define celt_encoder_get_size_custom celt011_celt_encoder_get_size_custom
#define celt_encoder_init celt011_celt_encoder_init
#define celt_encoder_init_custom celt011_celt_encoder_init_custom
#define _celt_lpc celt011__celt_lpc
#define celt_mode_create celt011_celt_mode_create
#define celt_mode_destroy celt011_celt_mode_destroy
#define celt_strerror celt011_celt_strerror
#define clt_mdct_backward celt011_clt_mdct_backward
#define clt_mdct_clear celt011_clt_mdct_clear
#define clt_mdct_forward celt011_clt_mdct_forward
#define clt_mdct_init celt011_clt_mdct_init
#define compute_allocation celt011_compute_allocation
#define compute_band_energies celt011_compute_band_energies
#define compute_pulse_cache celt011_compute_pulse_cache
#define decode_pulses celt011_decode_pulses
#define denormalise_bands celt011_denormalise_bands
#define ec_dec_bit_logp celt011_ec_dec_bit_logp
#define ec_dec_bits celt011_ec_dec_bits
#define ec_dec_icdf celt011_ec_dec_icdf
#define ec_dec_init celt011_ec_dec_init
#define ec_decode celt011_ec_decode
#define ec_decode_bin celt011_ec_decode_bin
#define ec_dec_uint celt011_ec_dec_uint
#define ec_dec_update celt011_ec_dec_update
#define ec_enc_bit_logp celt011_ec_enc_bit_logp
#define ec_enc_bits celt011_ec_enc_bits
#define ec_enc_done celt011_ec_enc_done
#define ec_enc_icdf celt011_ec_enc_icdf
#define ec_enc_init celt011_ec_enc_init
#define ec_encode celt011_ec_encode
#define ec_encode_bin celt011_ec_encode_bin
#define ec_enc_patch_initial_bits celt011_ec_enc_patch_initial_bits
#define ec_enc_shrink celt011_ec_enc_shrink
#define ec_enc_uint celt011_ec_enc_uint
#define ec_laplace_decode celt011_ec_laplace_decode
#define ec_laplace_encode celt011_ec_laplace_encode
#define ec_tell_frac celt011_ec_tell_frac
#define encode_pulses celt011_encode_pulses
#define fir celt011_fir
#define get_required_bits celt011_get_required_bits
#define haar1 celt011_haar1
#define icwrs celt011_icwrs
#define iir celt011_iir
#define isqrt32 celt011_isqrt32
#define kiss_fft celt011_kiss_fft
#define kiss_fft_alloc celt011_kiss_fft_alloc
#define kiss_fft_alloc_twiddles celt011_kiss_fft_alloc_twiddles
#define kiss_fft_free celt011_kiss_fft_free
#define kiss_ifft celt011_kiss_ifft
#define lcg_rand celt011_lcg_rand
#define log2Amp celt011_log2Amp
#define log2_frac celt011_log2_frac
#define normalise_bands celt011_normalise_bands
#define pitch_downsample celt011_pitch_downsample
#define pitch_search celt011_pitch_search
#define quant_all_bands celt011_quant_all_bands
#define quant_coarse_energy celt011_quant_coarse_energy
#define quant_energy_finalise celt011_quant_energy_finalise
#define quant_fine_energy celt011_quant_fine_energy
#define renormalise_vector celt011_renormalise_vector
#define spreading_decision celt011_spreading_decision
#define stereo_itheta celt011_stereo_itheta
#define unquant_coarse_energy celt011_unquant_coarse_energy
#define unquant_energy_finalise celt011_unquant_energy_finalise
#define unquant_fine_energy celt011_unquant_fine_energy

/* (and those OPUS_BUILD makes external) */
#define celt_decode_with_ec celt011_celt_decode_with_ec
#define celt_decode_with_ec_float celt011_celt_decode_with_ec_float
#define celt_encode_with_ec celt011_celt_encode_with_ec
#define celt_encode_with_ec_float celt011_celt_encode_with_ec_float
#define remove_doubling celt011_remove_doubling

#endif
