#include <gccore.h>
#include <fat.h>
#include <stdio.h>
#include <string.h>
#include "../deps/hbc_agent/sdk/hbc_agent.h"
#include "../deps/rcheevos/include/rc_hash.h"
#include "../deps/rcheevos/include/rc_consoles.h"
int main(int argc, char **argv) {
    GXRModeObj *m;void *fb;hbc_agent_config cfg={0};FILE *in,*out;char path[256],line[512],hash[33];int pass=1,n=0;
    VIDEO_Init();m=VIDEO_GetPreferredMode(NULL);fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(m));
    console_init(fb,20,20,m->fbWidth,m->xfbHeight,m->fbWidth*VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(m);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
    if(argc!=2 || strncmp(argv[1],"sd:/wiistation/bench-",21) || strlen(argv[1])>180 || !fatInitDefault())return 1;
    cfg.name="WiiStation tests";cfg.version=__DATE__ " " __TIME__;cfg.crash_reload_s=3;
    if(hbc_agent_init(&cfg))return 1;
    hbc_agent_net_wait(20000);
    snprintf(path,sizeof path,"%s/games.txt",argv[1]);in=fopen(path,"r");if(!in)return 1;
    snprintf(path,sizeof path,"%s/hashes.txt",argv[1]);out=fopen(path,"w");if(!out)return 1;
    while(fgets(line,sizeof line,in)) {
        line[strcspn(line,"\r\n")]=0;
        if(strncmp(line,"sd:/",4) || strstr(line,"..")) {pass=0;break;}
        hbc_agent_alive();
        if(!rc_hash_generate_from_file(hash,RC_CONSOLE_PLAYSTATION,line)) {pass=0;fprintf(out,"FAILED %s\n",line);}
        else {fprintf(out,"%s %s\n",hash,line);n++;}
    }
    fclose(in);fclose(out);
    snprintf(path,sizeof path,"%s/result.json",argv[1]);out=fopen(path,"w");if(!out)return 1;
    fprintf(out,"{\"pass\":%s,\"suite\":\"ps1-hash\",\"images\":%d}\n",pass&&n?"true":"false",n);fclose(out);
    fatUnmount("sd");return 0;
}
