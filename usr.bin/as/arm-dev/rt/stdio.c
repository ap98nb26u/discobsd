/*
 * stdio.c - buffered stdio + printf family + ctype functions for the native
 * ARM a.out libc (DiscoBSD/GBA), compiled by smlrc.
 *
 * FILE is laid out exactly like struct _iobuf in the shipped <stdio.h>, and
 * _iob[], _filbuf() and _flsbuf() are provided, so programs that include the
 * header and use its getc/putc/getchar/putchar macros or stdin/stdout/stderr
 * link and run.  Files opened with fopen() are fully buffered (BUFSIZ);
 * stdout and stderr are unbuffered, so output order matches write(2).
 * exit() flushes every open stream (crt0 calls exit after main returns).
 *
 * This is the subset smlrc needs to run natively (fopen/fclose/fgetc/fputc/
 * fputs/fgetpos/fsetpos/vfprintf/vprintf/isalpha/isalnum/isdigit) plus the
 * functions they are built on and their direct counterparts.
 */
int write(int, char*, int);
int read(int, char*, int);
int open(char*, int, int);
int close(int);
int lseek(int, int, int);
int _exit(int);
char* malloc(int);
int strlen(char*);

#define BUFSIZ  1024
#define NFILE   8
#define EOF     (-1)

#define _IOREAD 01
#define _IOWRT  02
#define _IONBF  04
#define _IOEOF  020
#define _IOERR  040
#define _IOSTRG 0100
#define _IORW   0400

struct _iobuf {
    int     _cnt;
    char    *_ptr;
    char    *_base;
    int     _bufsiz;
    short   _flag;
    short   _file;
};

struct _iobuf _iob[NFILE] = {
    { 0, 0, 0, 0, _IOREAD, 0 },
    { 0, 0, 0, 0, _IOWRT | _IONBF, 1 },
    { 0, 0, 0, 0, _IOWRT | _IONBF, 2 }
};

/* Buffers are kept per slot and reused, since free() is a no-op. */
char *_iobufs[NFILE];

int _getbuf(struct _iobuf *p)
{
    int i;

    i = p - _iob;
    if (!_iobufs[i]) {
        _iobufs[i] = malloc(BUFSIZ);
        if (!_iobufs[i])
            return -1;
    }
    p->_base = _iobufs[i];
    p->_ptr = p->_base;
    p->_bufsiz = BUFSIZ;
    return 0;
}

/* Write out what is buffered; leave the buffer empty and ready. */
int _flush(struct _iobuf *p)
{
    int n;

    n = p->_ptr - p->_base;
    p->_ptr = p->_base;
    p->_cnt = p->_bufsiz;
    if (n > 0 && write(p->_file, p->_base, n) != n) {
        p->_flag = p->_flag | _IOERR;
        return EOF;
    }
    return 0;
}

int fflush(struct _iobuf *p)
{
    if ((p->_flag & _IOWRT) && p->_base)
        return _flush(p);
    return 0;
}

/* Called by putc when the buffer is full (or there is none yet). */
int _flsbuf(int c, struct _iobuf *p)
{
    char ch;

    if (p->_flag & _IORW)
        p->_flag = (p->_flag & ~_IOREAD) | _IOWRT;
    if (!(p->_flag & _IOWRT))
        return EOF;
    if (p->_flag & _IONBF) {
        p->_cnt = 0;
        ch = c;
        if (write(p->_file, &ch, 1) != 1) {
            p->_flag = p->_flag | _IOERR;
            return EOF;
        }
        return c & 255;
    }
    if (!p->_base) {
        if (_getbuf(p) < 0)
            return EOF;
    } else if (p->_ptr > p->_base) {
        if (_flush(p) == EOF)
            return EOF;
    }
    *p->_ptr = c;
    p->_ptr = p->_ptr + 1;
    p->_cnt = p->_bufsiz - 1;
    return c & 255;
}

/* Called by getc when the buffer is empty. */
int _filbuf(struct _iobuf *p)
{
    int n;

    if (p->_flag & _IORW)
        p->_flag = (p->_flag & ~_IOWRT) | _IOREAD;
    if (!(p->_flag & _IOREAD))
        return EOF;
    if (!p->_base && _getbuf(p) < 0)
        return EOF;
    n = read(p->_file, p->_base, p->_bufsiz);
    if (n <= 0) {
        if (n == 0)
            p->_flag = p->_flag | _IOEOF;
        else
            p->_flag = p->_flag | _IOERR;
        p->_cnt = 0;
        return EOF;
    }
    p->_cnt = n - 1;
    p->_ptr = p->_base + 1;
    return *p->_base & 255;
}

int fgetc(struct _iobuf *p)
{
    int c;

    p->_cnt = p->_cnt - 1;
    if (p->_cnt >= 0) {
        c = *p->_ptr & 255;
        p->_ptr = p->_ptr + 1;
        return c;
    }
    return _filbuf(p);
}

int fputc(int c, struct _iobuf *p)
{
    p->_cnt = p->_cnt - 1;
    if (p->_cnt >= 0) {
        *p->_ptr = c;
        p->_ptr = p->_ptr + 1;
        return c & 255;
    }
    return _flsbuf(c, p);
}

int fputs(char *s, struct _iobuf *p)
{
    while (*s) {
        if (fputc(*s, p) == EOF)
            return EOF;
        s = s + 1;
    }
    return 0;
}

int putchar(int c)
{
    return fputc(c, &_iob[1]);
}

int puts(char *s)
{
    if (fputs(s, &_iob[1]) == EOF)
        return EOF;
    return fputc(10, &_iob[1]);
}

struct _iobuf *fopen(char *name, char *mode)
{
    struct _iobuf *p;
    char *m;
    int i, fd, rw, oflag, flag;

    for (i = 3; i < NFILE; i++)
        if (_iob[i]._flag == 0)
            break;
    if (i == NFILE)
        return 0;
    p = &_iob[i];

    rw = 0;
    for (m = mode; *m; m++)
        if (*m == '+')
            rw = 1;
    if (*mode == 'r') {
        oflag = 0;
        flag = _IOREAD;
    } else if (*mode == 'w') {
        oflag = 1 | 0x200 | 0x400;          /* O_WRONLY|O_CREAT|O_TRUNC */
        flag = _IOWRT;
    } else if (*mode == 'a') {
        oflag = 1 | 0x200 | 0x8;            /* O_WRONLY|O_CREAT|O_APPEND */
        flag = _IOWRT;
    } else
        return 0;
    if (rw) {
        oflag = (oflag & ~3) | 2;           /* O_RDWR */
        flag = _IORW;
    }
    fd = open(name, oflag, 0666);
    if (fd < 0)
        return 0;
    p->_cnt = 0;
    p->_ptr = 0;
    p->_base = 0;
    p->_bufsiz = 0;
    p->_file = fd;
    p->_flag = flag;
    return p;
}

int fclose(struct _iobuf *p)
{
    int r;

    if (p->_flag == 0)
        return EOF;
    r = fflush(p);
    if (close(p->_file) < 0)
        r = EOF;
    p->_flag = 0;
    p->_cnt = 0;
    p->_ptr = 0;
    p->_base = 0;
    return r;
}

long ftell(struct _iobuf *p)
{
    int pos;

    pos = lseek(p->_file, 0, 1);
    if (pos < 0)
        return -1;
    if ((p->_flag & _IOREAD) && p->_base && p->_cnt > 0)
        pos = pos - p->_cnt;
    else if ((p->_flag & _IOWRT) && p->_base)
        pos = pos + (p->_ptr - p->_base);
    return pos;
}

int fseek(struct _iobuf *p, long off, int whence)
{
    if (p->_flag & _IOWRT) {
        if (fflush(p) == EOF)
            return -1;
    } else if (p->_flag & _IOREAD) {
        if (whence == 1 && p->_cnt > 0)
            off = off - p->_cnt;
        p->_cnt = 0;
        p->_ptr = p->_base;
    }
    p->_flag = p->_flag & ~_IOEOF;
    if (p->_flag & _IORW)
        p->_flag = p->_flag & ~(_IOREAD | _IOWRT);
    if (lseek(p->_file, off, whence) < 0)
        return -1;
    return 0;
}

/* fpos_t is a long-sized cookie holding the file offset. */
int fgetpos(struct _iobuf *p, long *pos)
{
    *pos = ftell(p);
    if (*pos < 0)
        return -1;
    return 0;
}

int fsetpos(struct _iobuf *p, long *pos)
{
    return fseek(p, *pos, 0);
}

int exit(int status)
{
    int i;

    for (i = 0; i < NFILE; i++)
        if (_iob[i]._flag & _IOWRT)
            fflush(&_iob[i]);
    _exit(status);
    return 0;
}

/*
 * printf engine.  Supports flags - 0 + space #, width and precision (also
 * as *), the l/h length modifiers (ignored: int and long are both 32-bit),
 * and the conversions d i u o x X c s p %.  %p prints (nil) for a null
 * pointer, else 0x-prefixed hex, like the cross libc's doprnt.
 */
int _nout;

int _out(int c, struct _iobuf *fp)
{
    _nout = _nout + 1;
    return fputc(c, fp);
}

int _pad(int c, int n, struct _iobuf *fp)
{
    while (n > 0) {
        _out(c, fp);
        n = n - 1;
    }
    return 0;
}

int _doprnt(char *fmt, char *ap, struct _iobuf *fp)
{
    char buf[16];
    char *s, *pre, *dig;
    int c, v, left, zero, plus, space, width, prec, len, base, isnum, plen;
    unsigned u;

    _nout = 0;
    while (*fmt) {
        if (*fmt != '%') {
            _out(*fmt, fp);
            fmt = fmt + 1;
            continue;
        }
        fmt = fmt + 1;
        left = 0; zero = 0; plus = 0; space = 0;
        for (;;) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else if (*fmt == '+') plus = 1;
            else if (*fmt == ' ') space = 1;
            else if (*fmt != '#') break;
            fmt = fmt + 1;
        }
        width = 0;
        if (*fmt == '*') {
            width = *(int *)ap; ap = ap + 4; fmt = fmt + 1;
            if (width < 0) { left = 1; width = 0 - width; }
        } else
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0'); fmt = fmt + 1;
            }
        prec = -1;
        if (*fmt == '.') {
            fmt = fmt + 1;
            prec = 0;
            if (*fmt == '*') {
                prec = *(int *)ap; ap = ap + 4; fmt = fmt + 1;
            } else
                while (*fmt >= '0' && *fmt <= '9') {
                    prec = prec * 10 + (*fmt - '0'); fmt = fmt + 1;
                }
        }
        while (*fmt == 'l' || *fmt == 'h')
            fmt = fmt + 1;
        c = *fmt;
        if (c == 0)
            break;
        fmt = fmt + 1;

        pre = "";
        isnum = 0;
        base = 10;
        dig = "0123456789abcdef";
        if (c == 'd' || c == 'i') {
            v = *(int *)ap; ap = ap + 4;
            if (v < 0) { pre = "-"; u = 0 - v; }
            else { u = v; if (plus) pre = "+"; else if (space) pre = " "; }
            isnum = 1;
        } else if (c == 'u') {
            u = *(unsigned *)ap; ap = ap + 4; isnum = 1;
        } else if (c == 'o') {
            u = *(unsigned *)ap; ap = ap + 4; base = 8; isnum = 1;
        } else if (c == 'x' || c == 'X') {
            u = *(unsigned *)ap; ap = ap + 4; base = 16; isnum = 1;
            if (c == 'X') dig = "0123456789ABCDEF";
        } else if (c == 'p') {
            u = *(unsigned *)ap; ap = ap + 4;
            if (u) { pre = "0x"; base = 16; isnum = 1; }
            else { s = "(nil)"; len = 5; }
        } else if (c == 'c') {
            buf[0] = *(int *)ap; ap = ap + 4;
            s = buf; len = 1;
        } else if (c == 's') {
            s = *(char **)ap; ap = ap + 4;
            if (!s) s = "(null)";
            len = strlen(s);
            if (prec >= 0 && len > prec) len = prec;
        } else {
            /* %% and anything unknown: print the character itself */
            buf[0] = c;
            s = buf; len = 1;
        }

        if (isnum) {
            s = buf + 15;
            *s = 0;
            len = 0;
            while (u) {
                s = s - 1; *s = dig[u % base]; u = u / base; len = len + 1;
            }
            if (prec < 0) prec = 1;
            else zero = 0;          /* a precision overrides the 0 flag */
            while (len < prec) {
                s = s - 1; *s = '0'; len = len + 1;
            }
        } else
            zero = 0;

        plen = strlen(pre);
        width = width - len - plen;
        if (!left && !zero)
            _pad(' ', width, fp);
        while (*pre) {
            _out(*pre, fp);
            pre = pre + 1;
        }
        if (!left && zero)
            _pad('0', width, fp);
        for (v = 0; v < len; v++)
            _out(s[v], fp);
        if (left)
            _pad(' ', width, fp);
    }
    return _nout;
}

int vfprintf(struct _iobuf *fp, char *fmt, char *ap)
{
    return _doprnt(fmt, ap, fp);
}

int vprintf(char *fmt, char *ap)
{
    return _doprnt(fmt, ap, &_iob[1]);
}

int fprintf(struct _iobuf *fp, char *fmt, ...)
{
    return _doprnt(fmt, (char *)&fmt + 4, fp);
}

int printf(char *fmt, ...)
{
    return _doprnt(fmt, (char *)&fmt + 4, &_iob[1]);
}

/* sprintf writes through a string "stream" that never needs flushing. */
struct _iobuf _sfile;

int vsprintf(char *buf, char *fmt, char *ap)
{
    int n;

    _sfile._ptr = buf;
    _sfile._base = buf;
    _sfile._cnt = 0x7fffffff;
    _sfile._flag = _IOWRT | _IOSTRG;
    n = _doprnt(fmt, ap, &_sfile);
    *_sfile._ptr = 0;
    return n;
}

int sprintf(char *buf, char *fmt, ...)
{
    return vsprintf(buf, fmt, (char *)&fmt + 4);
}

/* ctype functions, for programs that do not include <ctype.h> (whose
   macros read the same _ctype_ table, defined in libc.c). */
extern char _ctype_[];

int isalpha(int c)  { return (_ctype_ + 1)[c] & (01 | 02); }
int isupper(int c)  { return (_ctype_ + 1)[c] & 01; }
int islower(int c)  { return (_ctype_ + 1)[c] & 02; }
int isdigit(int c)  { return (_ctype_ + 1)[c] & 04; }
int isxdigit(int c) { return (_ctype_ + 1)[c] & (04 | 0100); }
int isspace(int c)  { return (_ctype_ + 1)[c] & 010; }
int ispunct(int c)  { return (_ctype_ + 1)[c] & 020; }
int isalnum(int c)  { return (_ctype_ + 1)[c] & (01 | 02 | 04); }
int isprint(int c)  { return (_ctype_ + 1)[c] & (020 | 01 | 02 | 04 | 0200); }
int iscntrl(int c)  { return (_ctype_ + 1)[c] & 040; }
