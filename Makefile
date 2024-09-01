CC = /usr/bin/gcc
CFLAGS = -Wall
TARGETS = my_program

default: my_program

all: $(TARGETS)

my_program: main.o camera_control.o send_mail.o
	$(CC) $(CFLAGS) main.o camera_control.o send_mail.o -lcurl -o my_program

main.o: main.c camera_control.h send_mail.h
	$(CC) $(CFLAGS) -c main.c

camera_control.o: camera_control.c camera_control.h
	$(CC) $(CFLAGS) -c camera_control.c

send_mail.o: send_mail.c send_mail.h
	$(CC) $(CFLAGS) -c send_mail.c

clean:
	rm -f *.o *~ a.out $(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $<
