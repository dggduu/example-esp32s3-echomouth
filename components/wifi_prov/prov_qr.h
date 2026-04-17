#ifndef PROV_QR_H
#define PROV_QR_H

#include "gs_qrcode_comp.h"

void prov_qr_init(void);

void prov_qr_process(void);

void wifi_prov_print_qr(const char *name, const char *username, const char *pop,
                        const char *transport);

void prov_qr_set_status(gs_qr_status_t status);

void prov_qr_close();

#endif