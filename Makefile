CFLAGS += -Wall
obj := socketUser
src := socketUser.c
CC = arm-fsl-linux-gnueabi-gcc 

$(obj): $(src)
	$(CC) $(CFLAGS) $^ -o $@ -g

.PHONY: clean
clean:
	-rm autoStart
