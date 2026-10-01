# Cortex-A53 has NEON but no SVE. Avoid compiling unused SVE variants,
# including SVE2_128, which triggers a GCC ICE in simplify_gen_subreg_concatn
# while compiling enc_transforms-inl.h with the current UNO Q toolchain.
# libjxl 0.12 uses per-Highway-target options; the older
# JPEGXL_ENABLE_SIZELESS_VECTORS option no longer controls these variants.
EXTRA_OECMAKE:append:uno-q = " \
    -DJPEGXL_ENABLE_HWY_SVE=OFF \
    -DJPEGXL_ENABLE_HWY_SVE_256=OFF \
    -DJPEGXL_ENABLE_HWY_SVE2=OFF \
    -DJPEGXL_ENABLE_HWY_SVE2_128=OFF \
"
