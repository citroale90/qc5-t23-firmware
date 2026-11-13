#ifndef QC5_WIFI_STORE_H
#define QC5_WIFI_STORE_H
#include "qc5_wifi_profile.h"
/* Literal, bounded config update. No shell, radio, flash or native settings. */
int qc5_wifi_config_profile(const char*,unsigned long,struct qc5_wifi_profile*);
/* Existing config wins over seed. Keep a synced previous copy, then publish
 * the complete new file by rename. Caller must first verify the association. */
int qc5_wifi_store(const char*sd,const char*seed,unsigned long seed_size,const struct qc5_wifi_profile*);
#endif
