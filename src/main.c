/**
 * @file main.c
 * @author ajxs, rizet
 * @date Aug 2019
 * @brief Bootloader entry point and main application.
 * The entry point for the application. Contains the main bootloader code that
 * initiates the loading of the Kernel executable. The main application is
 * contained within the `efi_main` function.
 */

#include <efi/efi.h>
#include <efi/efilib.h>
#include <stdarg.h>

#include <bootloader/bootloader.h>
#include <bootloader/elf.h>
#include <bootloader/error.h>
#include <bootloader/fs.h>
#include <bootloader/graphics.h>
#include <bootloader/serial.h>

#define TARGET_SCREEN_WIDTH     1024
#define TARGET_SCREEN_HEIGHT    768
#define TARGET_PIXEL_FORMAT     PixelBlueGreenRedReserved8BitPerColor

/**
 * Whether to draw a test pattern to video output to test the graphics output
 * service.
 */
#define DRAW_TEST_SCREEN 0

/**
 * @brief Allocates the memory map.
 * Allocates the memory map. This function needs to be run prior to exiting
 * UEFI boot services.
 * @param[out] memory_map            A pointer to pointer to the memory map
 *                                   buffer to be allocated in this function.
 * @param[out] memory_map_size       The size of the allocated buffer.
 * @param[out] memory_map_key        They key of the allocated memory map.
 * @param[out] descriptor_size       A pointer to the size of an individual
 *                                   EFI_MEMORY_DESCRIPTOR.
 * @param[out] descriptor_version    A pointer to the version number associated
 *                                   with the EFI_MEMORY_DESCRIPTOR.
 * @return                   The program status.
 * @retval EFI_SUCCESS       The function executed successfully.
 * @retval other             A fatal error occurred getting the memory map.
 * @warn    After this function has been run, no other boot services may be used
 *          otherwise the memory map will be considered invalid.
 */

EFI_STATUS get_memory_map(OUT VOID** memory_map,
	OUT UINTN* memory_map_size,
	OUT UINTN* memory_map_key,
	OUT UINTN* descriptor_size,
	OUT UINT32* descriptor_version);


/**
 * Print
 */
EFI_STATUS debug_print_line(IN CHAR16* fmt,
	...)
{EFIAPI
	/** Main bootloader application status. */
	EFI_STATUS status;
	/** The variadic argument list passed to the VSPrintf function. */
	va_list args;
	/**
	 * The output message buffer.
	 * The string content is copied into this buffer. The maximum length is set
	 * to the maximum serial buffer length.
	 */
	CHAR16 output_message[MAX_SERIAL_OUT_STRING_LENGTH];

	va_start(args, fmt);

	// If the serial service has been initialised, use this as the output medium.
	// Otherwise use the default GNU-EFI output.
	VSPrint(output_message, MAX_SERIAL_OUT_STRING_LENGTH, fmt, args);

	print_to_serial_out(serial_service.protocol, output_message);

	va_end(args);

	return EFI_SUCCESS;
};


/**
 * get_memory_map
 */
EFI_STATUS get_mem_map(OUT EFI_MEMORY_DESCRIPTOR** memory_map,
	OUT UINTN* memory_map_size,
	OUT UINTN* memory_map_key,
	OUT UINTN* descriptor_size,
	OUT UINT32* descriptor_version)
{
	/** Program status. */
	EFI_STATUS status;


	status = uefi_call_wrapper(gBS->GetMemoryMap, 5,
		memory_map_size, *memory_map, memory_map_key,
		descriptor_size, descriptor_version);
	if(EFI_ERROR(status)) {
		// This will always fail on the first attempt, this call will return the
		// required buffer size.
		if(status != EFI_BUFFER_TOO_SMALL) {
			Print(u"Fatal Error: Error getting memory map size: %s\n",
				get_efi_error_message(status));
		}
	}

	status = uefi_call_wrapper(gBS->AllocatePool, 3,
		EfiLoaderData, ((*memory_map_size) + (*descriptor_size) * 5) , memory_map);

	if(EFI_ERROR(status)) {
		Print(u"Fatal Error: Error allocating memory map buffer: %s\n",
			get_efi_error_message(status));

		return status;
	}

	status = uefi_call_wrapper(gBS->GetMemoryMap, 5,
		memory_map_size, *memory_map, memory_map_key,
		descriptor_size, descriptor_version);


	if(EFI_ERROR(status)) {
		Print(u"Fatal Error: Error getting memory map: %s\n",
			get_efi_error_message(status));

		while (1);

		return status;
	}

//	Print(u"Debug: Allocated memory map buffer at: 0x%llx "
//		u"with size: %llu\n", *memory_map, *memory_map_size);

	return EFI_SUCCESS;
}


/**
 * efi_main
 */
_Noreturn EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle,
	EFI_SYSTEM_TABLE* SystemTable)
{
	/** Main bootloader application status. */
	EFI_STATUS status;
	/**
	 * Graphics output protocol instance.
	 * This protocol instance will be opened from the active console out handle.
	 */
	EFI_GRAPHICS_OUTPUT_PROTOCOL* graphics_output_protocol = NULL;
	/**
	 * The root file system entity.
	 * This is the file root from which the kernel binary will be loaded.
	 */
	EFI_FILE* root_file_system;
	/** The kernel entry point address. */
	EFI_PHYSICAL_ADDRESS* kernel_entry_point = 0;
	/** The EFI memory map descriptor. */
	EFI_MEMORY_DESCRIPTOR* memory_map = NULL;
	/** The memory map key. */
	UINTN memory_map_key = 0;
	/** The size of the memory map buffer. */
	UINTN memory_map_size = 0;
	/** The memory map descriptor size. */
	UINTN descriptor_size;
	/** The memory map descriptor. */
	UINT32 descriptor_version;
	/** Function pointer to the kernel entry point. */
	void (*kernel_entry)(Kernel_Boot_Info* boot_info);

	// Initialise service protocols to NULL, so that we can detect if they are
	// properly initialised in service functions.
	serial_service.protocol = NULL;
	file_system_service.protocol = NULL;

	// Initialise the UEFI lib.
	InitializeLib(ImageHandle, SystemTable);


	// Disable the watchdog timer.
	status = uefi_call_wrapper(gBS->SetWatchdogTimer, 4, 0, 0, 0, NULL);
	if(EFI_ERROR(status)) {
		Print(u"Fatal Error: Error setting watchdog timer: %s\n",
			get_efi_error_message(status));

		return status;
	}

	// Reset console input.
	status = uefi_call_wrapper(ST->ConIn->Reset, 2, SystemTable->ConIn, FALSE);
	if(EFI_ERROR(status)) {
		Print(u"Fatal Error: Error resetting console input: %s\n",
			get_efi_error_message(status));

		return status;
	}

	// Initialise the serial service.
	status = init_serial_service();
	if(EFI_ERROR(status)) {
		Print(u"Warning: No serial IO port available\n\n");

		status = 0;
	}

	// Initialise the graphics output service.
	status = init_graphics_output_service();
	if(EFI_ERROR(status)) {
		Print(u"Fatal Error: Error initialising Graphics service\n\n");

		return status;
	}

	// Locate the graphics output protocol before and open it
	status = uefi_call_wrapper(gBS->LocateProtocol, 3,
		&gEfiGraphicsOutputProtocolGuid, NULL,
		&graphics_output_protocol);
	if (EFI_ERROR(status)) {
		Print(u"Error: Failed to locate the graphics output protocol on "
			u"the current firmware: %s\n", get_efi_error_message(status));
	}

	// Open the graphics output protocol from the handle for the active console
	// output device and use it to draw the boot screen.
	// The console out handle exposed by the System Table is documented in the
	// UEFI Spec page 92.
	//
///////// NOTICE: removed for compatibility
//	
//	status = uefi_call_wrapper(gBS->HandleProtocol, 3,
//		ST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid,
//		&graphics_output_protocol);
//	if (EFI_ERROR(status)) {
//		Print(u"Error: Failed to open the graphics output protocol on "
//			u"the active console output device: %s\n", get_efi_error_message(status));
//	}

	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *pref_mode;
	UINTN SizeOfInfo, numModes, nativeMode;

	status = uefi_call_wrapper(graphics_output_protocol->QueryMode, 4, graphics_output_protocol, graphics_output_protocol->Mode == NULL ? 0 : graphics_output_protocol->Mode->Mode, &SizeOfInfo, &info);
	// this is needed to get the current video mode
	if (status == EFI_NOT_STARTED)
	status = uefi_call_wrapper(graphics_output_protocol->SetMode, 2, graphics_output_protocol, 0);
	if (EFI_ERROR(status)) {
		debug_print_line(u"Unable to get native mode");
	} else {
		nativeMode = graphics_output_protocol->Mode->Mode;
		numModes = graphics_output_protocol->Mode->MaxMode;
	}

	for (int i = 0; i < numModes; i++) {
		status = uefi_call_wrapper(graphics_output_protocol->QueryMode, 4, graphics_output_protocol, i, &SizeOfInfo, &info);
		debug_print_line(u"mode %03d width %d height %d format %x%s\n",
			i,
			info->HorizontalResolution,
			info->VerticalResolution,
			info->PixelFormat,
			i == nativeMode ? "(current)" : ""
		);
		if (i == nativeMode)
			pref_mode = info;
	}

	status = uefi_call_wrapper(graphics_output_protocol->QueryMode, 4, graphics_output_protocol, nativeMode, &SizeOfInfo, &info);

	// If we were able to obtain a protocol on the current output device handle
	// set the graphics mode to the target and draw the boot screen.
	if(graphics_output_protocol) {
		status = set_graphics_mode(graphics_output_protocol, info->HorizontalResolution,
			info->VerticalResolution, info->PixelFormat);
		if(EFI_ERROR(status)) {
			Print(u"Fatal Error: Error setting graphics mode: %s\n",
				get_efi_error_message(status));

			return status;
		}

		draw_test_screen(graphics_output_protocol);
	}

	Print(u"\n\n\n\n\n\n\n                \n    microNET    \n                ");

	// Initialise the simple file system service.
	// This will be used to load the kernel binary.
	status = init_file_system_service();
	if(EFI_ERROR(status)) {
		Print(u"Fatal Error: Error initialising File System service: %s\n",
			get_efi_error_message(status));

		return status;
	}

	status = uefi_call_wrapper(file_system_service.protocol->OpenVolume, 2,
		file_system_service.protocol, &root_file_system);
	if(EFI_ERROR(status)) {
		Print(u"Fatal Error: Error opening root volume: %s\n",
			get_efi_error_message(status));

		return status;
	}

	status = load_kernel_image(root_file_system, KERNEL_EXECUTABLE_PATH,
		kernel_entry_point);

//	Print(u"%d", status);

	if(EFI_ERROR(status)) {
		Print(u"Fatal Error: Error loading Kernel image\n\n");
		return status;
	}

	// Get the memory map prior to exiting the boot service.
	status = get_mem_map(&memory_map, &memory_map_size,
		&memory_map_key, &descriptor_size, &descriptor_version);

	if(EFI_ERROR(status)) {
		// Error will have already been printed;
		return status;
	}

	status = uefi_call_wrapper(gBS->ExitBootServices, 2,
		ImageHandle, memory_map_key);
	if(EFI_ERROR(status)) {
		Print(u"Fatal Error: Error exiting boot services: %s\n",
			get_efi_error_message(status));

		return status;
	}


	/** Boot info struct, passed to the kernel. */
	Kernel_Boot_Info *boot_info;

	/* Framebuffer struct, containing info about the framebuffer */
	Kernel_Framebuffer *k_framebuffer;
	
	// Set kernel boot info.
	boot_info->memory_map = memory_map;
	boot_info->memory_map_size = memory_map_size;
	boot_info->memory_map_descriptor_size = descriptor_size;

	k_framebuffer->framebuffer_base_addr = graphics_output_protocol->Mode->FrameBufferBase;
	k_framebuffer->framebuffer_size = graphics_output_protocol->Mode->FrameBufferSize;
	k_framebuffer->framebuffer_mode = graphics_output_protocol->Mode->Mode;
	k_framebuffer->pixels_per_scan_line = graphics_output_protocol->Mode->Info->PixelsPerScanLine;
	k_framebuffer->x_resolution = graphics_output_protocol->Mode->Info->HorizontalResolution;
	k_framebuffer->y_resolution = graphics_output_protocol->Mode->Info->VerticalResolution;

	boot_info->framebuffer = k_framebuffer;

	boot_info->verification = 0xDEADBEEFCAFECAFE;

	boot_info->RT = ST->RuntimeServices;
	// Cast pointer to kernel entry.
	kernel_entry = (void (*)(Kernel_Boot_Info*))*kernel_entry_point;
	// Jump to kernel entry.
	kernel_entry(boot_info);

	// Return an error if this code is ever reached.
	return EFI_LOAD_ERROR;
}


/**
 * wait_for_input
 */
EFI_STATUS wait_for_input(OUT EFI_INPUT_KEY* key) {
	/** The program status. */
	EFI_STATUS status;
	do {
		status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, key);
	} while(status == EFI_NOT_READY);

	return EFI_SUCCESS;
}
