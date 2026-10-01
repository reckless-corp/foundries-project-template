# USE_GBM needs Mesa's GBM headers and library in addition to libdrm.
# Keep the dependency conditional on the upstream PACKAGECONFIG feature.
PACKAGECONFIG[gbm] = "-DUSE_GBM=ON,-DUSE_GBM=OFF,libdrm virtual/libgbm"
