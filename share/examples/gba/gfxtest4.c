/* Heavy continuous drawing: a full-screen fill every iteration. Each fill is
   one ioctl that writes 240x160 pixels with IME=0, so IME=0 dominates -
   stress for the A+B+START escape (does input polling still see it?). */
#include <stdio.h>
#include <unistd.h>
#include <sys/gpanel.h>
int main(){
    int xs=0,ys=0,c=0;
    if (gpanel_open("/dev/tft0")<0){ printf("no tft0\n"); return 1; }
    gpanel_clear(0,&xs,&ys);
    for(;;){
        int col = (c & 1) ? 0x001f : 0x7c00;   /* alternate blue/??? each frame */
        gpanel_fill(col, 0, 0, xs-1, ys-1);
        c++;
    }
    return 0;
}
