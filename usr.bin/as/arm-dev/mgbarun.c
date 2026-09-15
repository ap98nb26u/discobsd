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
#define STEP(s) do{ fprintf(stderr,"mgbarun: %s\n",s); fflush(stderr);}while(0)

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr,"usage: %s <rom> [frames]\n",argv[0]); return 2; }
    int frames = argc>2?atoi(argv[2]):1200;
    mLogSetDefaultLogger(&logger);
    struct mCore* core = mCoreFind(argv[1]);
    if (!core){ fprintf(stderr,"no core\n"); return 1; }
    STEP("config init"); mCoreConfigInit(&core->config, NULL);
    STEP("init"); if (!core->init(core)){ fprintf(stderr,"init failed\n"); return 1; }
    STEP("loadROM");
    struct VFile* rom = VFileOpen(argv[1], O_RDONLY);
    if (!rom || !core->loadROM(core, rom)){ fprintf(stderr,"loadROM failed\n"); return 1; }
    STEP("audio buffer"); core->setAudioBufferSize(core, 2048);
    unsigned w=240,h=160; core->desiredVideoDimensions(core,&w,&h);
    void* vbuf = calloc((size_t)w*h,4);
    STEP("setVideoBuffer"); core->setVideoBuffer(core,(color_t*)vbuf,w);
    STEP("reset"); core->reset(core);
    STEP("run");
    for (int i=0;i<frames;i++) core->runFrame(core);
    STEP("done"); return 0;
}
