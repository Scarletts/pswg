PROG=		pswg

PREFIX?=	/usr/local

SRCS=		pswg.c xalloc.c util.c

OBJS=		pswg.o xalloc.o util.o

CFLAGS?=	-O2 -g

CFLAGS+=	-std=c99 -Wall -D_POSIX_C_SOURCE=200809L

all: ${OBJS}
	${CC} -o ${PROG} ${OBJS}

install: all
	install -d ${DESTDIR}${PREFIX}/bin
	install -d ${DESTDIR}${PREFIX}/man/man1
	install -m 755 ${PROG} ${DESTDIR}${PREFIX}/bin
	install -m 644 pswg.1 ${DESTDIR}${PREFIX}/man/man1/${PROG}.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/${PROG}
	rm -f ${DESTDIR}${PREFIX}/man/man1/${PROG}.1

clean:
	rm -f ${OBJS} ${PROG}

