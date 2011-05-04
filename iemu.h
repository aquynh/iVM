#ifndef IOS_FW_H
#define IOS_FW_H

typedef enum {
    BOOT_TYPE_IBOOT = 1,
    BOOT_TYPE_OPENIBOOT,
    BOOT_TYPE_VROM
} boot_type;

void iemu_fw_list(const char *device);
int iemu_fw_load(const char *deviceName, const char *iemuVersion);
int iemu_fw_init(const char *optarg, const char *device);
char *iemu_get_skin(void);

#endif
