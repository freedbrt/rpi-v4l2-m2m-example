CC = gcc
CFLAGS = -Wall -O3 -g3
LIBS = -lv4l2

OBJS = main.o

all: v4l2_align_problem

v4l2_align_problem: $(OBJS)
	$(CC) $(CFLAGS) -o v4l2_align_problem $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o v4l2_align_problem
