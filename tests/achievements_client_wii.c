/* Run pinned upstream rc_client mocks on the real big-endian target. */
#include <gccore.h>
#include <fat.h>
#include <stdio.h>
#include <string.h>
#include "../deps/hbc_agent/sdk/hbc_agent.h"
#include "test_framework.h"
TEST_FRAMEWORK_DECLARATIONS()
void test_client(void);
int main(int argc, char **argv) {
    hbc_agent_config cfg = {0};
    GXRModeObj *m; void *fb; FILE *f; char path[256];
    VIDEO_Init(); m=VIDEO_GetPreferredMode(NULL);
    fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(m));
    console_init(fb,20,20,m->fbWidth,m->xfbHeight,m->fbWidth*VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(m); VIDEO_SetNextFramebuffer(fb); VIDEO_SetBlack(FALSE); VIDEO_Flush(); VIDEO_WaitVSync();
    if (argc!=2 || strncmp(argv[1],"sd:/wiistation/bench-",21) || strlen(argv[1])>180 || !fatInitDefault()) return 1;
    cfg.name="WiiStation client tests"; cfg.version=__DATE__ " " __TIME__;cfg.crash_reload_s=3;
    if(hbc_agent_init(&cfg)) return 1;
    hbc_agent_net_wait(20000);
    TEST_FRAMEWORK_INIT(); test_client(); TEST_FRAMEWORK_SHUTDOWN();
    snprintf(path,sizeof path,"%s/result.json",argv[1]);f=fopen(path,"w");if(!f)return 1;
    fprintf(f,"{\"pass\":%s,\"suite\":\"client-mock\",\"tests\":%u,\"failed\":%u}\n",
        TEST_FRAMEWORK_PASSED()?"true":"false",(unsigned)__test_framework_state.run_count,(unsigned)__test_framework_state.fail_count);
    fclose(f);fatUnmount("sd");return 0;
}
