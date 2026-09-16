#!/usr/bin/env python3
# Generate an mgbarun key-injection script that drives DiscoBSD/GBA's on-screen
# soft keyboard to "type" text (login + commands) headlessly. See mgbarun.c and
# sys/arch/gba/dev/swkbd.c. Cursor starts at row0/col0 (`); SELECT shows the
# keyboard, D-pad moves the cursor, A selects, B = Enter, L = shift (for e.g. |).
#   python3 swkbd_type.py > keys.txt      (edit the script list below)
GRID={'`':(0,0),'1':(0,1),'2':(0,2),'3':(0,3),'4':(0,4),'5':(0,5),'6':(0,6),'7':(0,7),'8':(0,8),'9':(0,9),'0':(0,10),'-':(0,11),'=':(0,12),
 'q':(1,0),'w':(1,1),'e':(1,2),'r':(1,3),'t':(1,4),'y':(1,5),'u':(1,6),'i':(1,7),'o':(1,8),'p':(1,9),
 'a':(2,0),'s':(2,1),'d':(2,2),'f':(2,3),'g':(2,4),'h':(2,5),'j':(2,6),'k':(2,7),'l':(2,8),
 'z':(3,0),'x':(3,1),'c':(3,2),'v':(3,3),'b':(3,4),'n':(3,5),'m':(3,6),' ':(4,1)}
SHIFTED={'|':(1,12)}   # extend as needed (uppercase = SHIFTED[lower], etc.)
A=0x1;B=0x2;SEL=0x4;RIGHT=0x10;LEFT=0x20;UP=0x40;DOWN=0x80;L=0x200
ev=[]; fr=[3000]; cx=[0]; cy=[0]; sh=[0]   # base frame ~3000 = after boot->login
def press(m,h=6,g=7): ev.append((fr[0],m)); fr[0]+=h; ev.append((fr[0],0)); fr[0]+=g
def wait(n): fr[0]+=n
def moveto(ty,tx):
    while cx[0]>0: press(LEFT); cx[0]-=1
    while cy[0]<ty: press(DOWN); cy[0]+=1
    while cy[0]>ty: press(UP); cy[0]-=1
    while cx[0]<tx: press(RIGHT); cx[0]+=1
def setsh(w):
    if w!=sh[0]: press(L); sh[0]=w
def typ(s):
    for ch in s:
        if ch=='\n': setsh(0); press(B); continue
        if ch in GRID: setsh(0); ty,tx=GRID[ch]
        else: setsh(1); ty,tx=SHIFTED[ch]
        moveto(ty,tx); press(A)
    setsh(0)
if __name__=='__main__':
    import sys
    press(SEL); typ("root\n"); wait(900)        # log in
    typ("echo injectok\n"); wait(1200)           # a command (edit me)
    sys.stdout.write("".join("%d %x\n"%(f,m) for f,m in ev))
