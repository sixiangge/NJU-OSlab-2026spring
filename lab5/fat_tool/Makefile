CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
TARGET = fat_tool
SOURCES = fat_tool.c fat_io.c fat_table.c fat_utils.c
OBJECTS = $(SOURCES:.c=.o)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS) *.img *.txt root_dir_sector

.PHONY: clean
