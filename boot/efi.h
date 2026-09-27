// The small part of the UEFI specification that the xv6 loader uses.
// Layouts follow the UEFI 2.x specification; tables of function
// pointers must keep every entry, in order, even unused ones.

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef unsigned long long UINT64;
typedef UINT64 UINTN;
typedef unsigned short CHAR16;
typedef UINTN EFI_STATUS;
typedef void *EFI_HANDLE;

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0
#define EFI_ERROR(s) (((long long)(s)) < 0)

typedef struct {
  UINT32 Data1;
  UINT16 Data2;
  UINT16 Data3;
  UINT8 Data4[8];
} EFI_GUID;

typedef struct {
  UINT64 Signature;
  UINT32 Revision;
  UINT32 HeaderSize;
  UINT32 CRC32;
  UINT32 Reserved;
} EFI_TABLE_HEADER;

// memory
typedef enum {
  AllocateAnyPages,
  AllocateMaxAddress,
  AllocateAddress,
} EFI_ALLOCATE_TYPE;

#define EfiLoaderData 2

typedef struct {
  UINT32 Type;
  UINT64 PhysicalStart;
  UINT64 VirtualStart;
  UINT64 NumberOfPages;
  UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

// text output
typedef struct SIMPLE_TEXT_OUTPUT SIMPLE_TEXT_OUTPUT;
struct SIMPLE_TEXT_OUTPUT {
  void *Reset;
  EFI_STATUS(EFIAPI *OutputString)(SIMPLE_TEXT_OUTPUT *, CHAR16 *);
  // (more entries follow, unused)
};

typedef struct {
  EFI_TABLE_HEADER Hdr;
  void *RaiseTPL;
  void *RestoreTPL;
  EFI_STATUS(EFIAPI *AllocatePages)(EFI_ALLOCATE_TYPE, UINT32, UINTN,
                                    UINT64 *);
  void *FreePages;
  EFI_STATUS(EFIAPI *GetMemoryMap)(UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *,
                                   UINTN *, UINT32 *);
  EFI_STATUS(EFIAPI *AllocatePool)(UINT32, UINTN, void **);
  void *FreePool;
  void *CreateEvent;
  void *SetTimer;
  void *WaitForEvent;
  void *SignalEvent;
  void *CloseEvent;
  void *CheckEvent;
  void *InstallProtocolInterface;
  void *ReinstallProtocolInterface;
  void *UninstallProtocolInterface;
  EFI_STATUS(EFIAPI *HandleProtocol)(EFI_HANDLE, EFI_GUID *, void **);
  void *Reserved;
  void *RegisterProtocolNotify;
  void *LocateHandle;
  void *LocateDevicePath;
  void *InstallConfigurationTable;
  void *LoadImage;
  void *StartImage;
  void *Exit;
  void *UnloadImage;
  EFI_STATUS(EFIAPI *ExitBootServices)(EFI_HANDLE, UINTN);
  void *GetNextMonotonicCount;
  void *Stall;
  EFI_STATUS(EFIAPI *SetWatchdogTimer)(UINTN, UINT64, UINTN, CHAR16 *);
  void *ConnectController;
  void *DisconnectController;
  void *OpenProtocol;
  void *CloseProtocol;
  void *OpenProtocolInformation;
  void *ProtocolsPerHandle;
  void *LocateHandleBuffer;
  EFI_STATUS(EFIAPI *LocateProtocol)(EFI_GUID *, void *, void **);
  // (more entries follow, unused)
} EFI_BOOT_SERVICES;

typedef struct {
  EFI_GUID VendorGuid;
  void *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
  EFI_TABLE_HEADER Hdr;
  CHAR16 *FirmwareVendor;
  UINT32 FirmwareRevision;
  EFI_HANDLE ConsoleInHandle;
  void *ConIn;
  EFI_HANDLE ConsoleOutHandle;
  SIMPLE_TEXT_OUTPUT *ConOut;
  EFI_HANDLE StandardErrorHandle;
  void *StdErr;
  void *RuntimeServices;
  EFI_BOOT_SERVICES *BootServices;
  UINTN NumberOfTableEntries;
  EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

// the image being run: tells which device it was loaded from.
typedef struct {
  UINT32 Revision;
  EFI_HANDLE ParentHandle;
  EFI_SYSTEM_TABLE *SystemTable;
  EFI_HANDLE DeviceHandle;
  // (more fields follow, unused)
} EFI_LOADED_IMAGE;

// files on the boot partition
typedef struct EFI_FILE EFI_FILE;
struct EFI_FILE {
  UINT64 Revision;
  EFI_STATUS(EFIAPI *Open)(EFI_FILE *, EFI_FILE **, CHAR16 *, UINT64, UINT64);
  EFI_STATUS(EFIAPI *Close)(EFI_FILE *);
  void *Delete;
  EFI_STATUS(EFIAPI *Read)(EFI_FILE *, UINTN *, void *);
  void *Write;
  EFI_STATUS(EFIAPI *GetPosition)(EFI_FILE *, UINT64 *);
  EFI_STATUS(EFIAPI *SetPosition)(EFI_FILE *, UINT64);
  // (more entries follow, unused)
};

#define EFI_FILE_MODE_READ 1

typedef struct EFI_SIMPLE_FILE_SYSTEM EFI_SIMPLE_FILE_SYSTEM;
struct EFI_SIMPLE_FILE_SYSTEM {
  UINT64 Revision;
  EFI_STATUS(EFIAPI *OpenVolume)(EFI_SIMPLE_FILE_SYSTEM *, EFI_FILE **);
};

// graphics output: the frame buffer
typedef struct {
  UINT32 Version;
  UINT32 HorizontalResolution;
  UINT32 VerticalResolution;
  UINT32 PixelFormat;
  UINT32 PixelInformation[4];
  UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
  UINT32 MaxMode;
  UINT32 Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  UINTN SizeOfInfo;
  UINT64 FrameBufferBase;
  UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_MODE;

typedef struct {
  void *QueryMode;
  void *SetMode;
  void *Blt;
  EFI_GRAPHICS_OUTPUT_MODE *Mode;
} EFI_GRAPHICS_OUTPUT;

#define LOADED_IMAGE_GUID                                                      \
  {0x5B1B31A1, 0x9562, 0x11d2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}
#define SIMPLE_FILE_SYSTEM_GUID                                                \
  {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}
#define GRAPHICS_OUTPUT_GUID                                                   \
  {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}}
#define ACPI_20_TABLE_GUID                                                     \
  {0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}}
#define ACPI_10_TABLE_GUID                                                     \
  {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}}
