/* Headless mGBA runner for DiscoBSD/GBA. Loads a ROM, runs N frames, prints
   every mGBA-log line. Optional key-injection script (3rd arg): lines of
   "<frame> <keymask_hex>" set the GBA keys at that frame (held until the next
   event). GBA keys: A=1 B=2 SEL=4 STA=8 R>=10 L<=20 UP=40 DN=80 R=100 L=200. */
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>

static void mylog(struct mLogger* l, int cat, enum mLogLevel lvl,
                  const char* fmt, va_list args) {
    (void)l;(void)lvl; char buf[2048]; vsnprintf(buf,sizeof buf,fmt,args);
    const char* cn = mLogCategoryName(cat);
    printf("[%s] %s\n", cn?cn:"?", buf); fflush(stdout);
}
static struct mLogger logger = { .log = mylog, .filter = NULL };

#define MAXEV 100000
static int evf[MAXEV]; static unsigned evk[MAXEV]; static int nev;

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr,"usage: %s <rom> [frames] [keyscript]\n",argv[0]); return 2; }
    int frames = argc>2?atoi(argv[2]):1200;
    if (argc>3){ FILE*s=fopen(argv[3],"r"); if(s){ int f; unsigned k;
        while(fscanf(s,"%d %x",&f,&k)==2 && nev<MAXEV){ evf[nev]=f; evk[nev]=k; nev++; } fclose(s);
        fprintf(stderr,"mgbarun: %d key events\n",nev); } }
    mLogSetDefaultLogger(&logger);
    struct mCore* core = mCoreFind(argv[1]);
    if (!core){ fprintf(stderr,"no core\n"); return 1; }
    mCoreConfigInit(&core->config, NULL);
    if (!core->init(core)){ fprintf(stderr,"init failed\n"); return 1; }
    struct VFile* rom = VFileOpen(argv[1], O_RDONLY);
    if (!rom || !core->loadROM(core, rom)){ fprintf(stderr,"loadROM failed\n"); return 1; }
    core->setAudioBufferSize(core, 2048);
    unsigned w=240,h=160; core->desiredVideoDimensions(core,&w,&h);
    void* vbuf = calloc((size_t)w*h,4); core->setVideoBuffer(core,(color_t*)vbuf,w);
    core->reset(core);
    int ei=0; unsigned curkeys=0;
    for (int i=0;i<frames;i++){
        while(ei<nev && evf[ei]<=i){ curkeys=evk[ei]; ei++; }
        core->setKeys(core, curkeys);
        core->runFrame(core);
    }
    return 0;
}
