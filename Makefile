
SRC_DIR := src
BOOTLOADER_SRC_DIR := $(SRC_DIR)
HEADERS_DIR := include
BUILD_DIR := bin
OBJ_DIR := $(BUILD_DIR)/obj
MINGW := gcc -m64 -march=x86-64 -fpic -ffreestanding -I$(HEADERS_DIR) -I$(HEADERS_DIR)/efi/x86_64 -I$(HEADERS_DIR)/efi/protocol
MINGW_LINK := gcc

dirs:
	mkdir $(BUILD_DIR)
	mkdir $(OBJ_DIR)

bootloader: dirs
	$(MINGW) -c $(BOOTLOADER_SRC_DIR)/loader.c -o $(OBJ_DIR)/loader.o
	$(MINGW) -c $(BOOTLOADER_SRC_DIR)/data.c -o $(OBJ_DIR)/data.o
	$(MINGW) -c $(BOOTLOADER_SRC_DIR)/elf.c -o $(OBJ_DIR)/elf.o
	$(MINGW) -c $(BOOTLOADER_SRC_DIR)/error.c -o $(OBJ_DIR)/error.o
	$(MINGW) -c $(BOOTLOADER_SRC_DIR)/fs.c -o $(OBJ_DIR)/fs.o
	$(MINGW) -c $(BOOTLOADER_SRC_DIR)/graphics.c -o $(OBJ_DIR)/graphics.o
	$(MINGW) -c $(BOOTLOADER_SRC_DIR)/main.c -o $(OBJ_DIR)/main.o
	$(MINGW) -c $(BOOTLOADER_SRC_DIR)/serial.c -o $(OBJ_DIR)/serial.o
	ld $(OBJ_DIR)/main.o $(OBJ_DIR)/loader.o $(OBJ_DIR)/data.o $(OBJ_DIR)/elf.o $(OBJ_DIR)/error.o $(OBJ_DIR)/fs.o $(OBJ_DIR)/graphics.o /usr/lib/crt0-efi-x86_64.o $(OBJ_DIR)/serial.o -nostdlib -melf_x86_64 -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic -L /usr/lib -o $(BUILD_DIR)/boot.so -fpic -lefi -lgnuefi
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym  -j .rel -j .rela -j .rel.* -j .rela.* -j .reloc --target efi-app-x86_64 --subsystem=10 $(BUILD_DIR)/boot.so $(BUILD_DIR)/bootloader.efi
	# x86_64-w64-mingw32-gcc -nostdlib -shared -Wl,--subsystem,10 -e efi_main -o bootloader.efi $(OBJ_DIR)/main.o $(OBJ_DIR)/loader.o $(OBJ_DIR)/data.o $(OBJ_DIR)/elf.o $(OBJ_DIR)/error.o $(OBJ_DIR)/fs.o $(OBJ_DIR)/graphics.o /usr/lib/crt0-efi-x86_64.o $(OBJ_DIR)/serial.o -lgcc
