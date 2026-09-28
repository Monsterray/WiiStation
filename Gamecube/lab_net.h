/* lab_net.h -- a test run driven over the network, for a Wii on the bench (scripts/wii_lab.py) */
#ifndef LAB_NET_H
#define LAB_NET_H

#ifdef __cplusplus
extern "C" {
#endif

/* "lab=HOST:PORT" among the loader's arguments (wiiload passes them) turns the lab on */
void lab_args(int argc, char **argv);
int  lab_active(void);
/* before autoboot.txt is read: take the run's files from the PC onto the SD card */
int  lab_fetch(void);
/* after the chain: send the files the PC asked for back to it */
void lab_report(void);

#ifdef __cplusplus
}
#endif

#endif
