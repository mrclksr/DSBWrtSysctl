PROGRAM  = dsbwrtsysctl
PREFIX  ?= /usr/local
CFLAGS  += -Wall -DPROGRAM=\"${PROGRAM}\"
INSTALL_PROGRAM = install -g wheel -m 0755 -o root

all: ${PROGRAM}

${PROGRAM}: ${PROGRAM}.c
	${CC} -o ${PROGRAM} ${CFLAGS} ${CPPFLAGS} ${PROGRAM}.c

install: ${PROGRAM}
	${INSTALL_PROGRAM} ${PROGRAM} ${PREFIX}/bin
clean:
	-rm -f ${PROGRAM}

