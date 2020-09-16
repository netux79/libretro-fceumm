#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "libretro.h"

#include "../../fceu.h"
#include "../../fceu-endian.h"
#include "../../input.h"
#include "../../state.h"
#include "../../ppu.h"
#include "../../cart.h"
#include "../../x6502.h"
#include "../../git.h"
#include "../../palette.h"
#include "../../sound.h"
#include "../../file.h"
#include "../../cheat.h"
#include "../../ines.h"
#include "../../unif.h"
#include "../../fds.h"
#include "../../vsuni.h"

#include <string.h>
#include "../../memstream.h"

#if defined(_3DS)
void* linearMemAlign(size_t size, size_t alignment);
void linearFree(void* mem);
#endif

static retro_video_refresh_t video_cb = NULL;
static retro_input_poll_t poll_cb = NULL;
static retro_input_state_t input_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;
static retro_environment_t environ_cb = NULL;
static bool use_overscan;
static int zapx;
static int zapy;
static bool show_zapper_crosshair;

/* emulator-specific variables */

int FCEUnetplay;
#ifdef PSP
#include "pspgu.h"
static __attribute__((aligned(16))) uint16_t retro_palette[256];
#else
static uint16_t retro_palette[256];
#endif
static uint16_t* fceu_video_out;


/* Some timing-related variables. */
static int maxconbskip = 9;			/* Maximum consecutive blit skips. */
static int ffbskip = 9;				/* Blit skips per blit when FF-ing */

static int soundo = 1;

static volatile int nofocus = 0;

static int32_t *sound = 0;
static uint32_t JSReturn;
static uint32_t ZapperInfo[3];
static int32_t current_palette = 0;

int PPUViewScanline=0;
int PPUViewer=0;

/* extern forward decls.*/
extern FCEUGI *GameInfo;
extern uint8 *XBuf;
extern CartInfo iNESCart;
extern CartInfo UNIFCart;

/* emulator-specific callback functions */

void UpdatePPUView(int refreshchr) { }

const char * GetKeyboard(void)
{
   return "";
}

int FCEUD_SendData(void *data, uint32 len)
{
   return 1;
}

#define BUILD_PIXEL_RGB565(R,G,B) (((int) ((R)&0x1f) << RED_SHIFT) | ((int) ((G)&0x3f) << GREEN_SHIFT) | ((int) ((B)&0x1f) << BLUE_SHIFT))

#if defined (PSP)
#define RED_SHIFT 0
#define GREEN_SHIFT 5
#define BLUE_SHIFT 11
#define RED_EXPAND 3
#define GREEN_EXPAND 2
#define BLUE_EXPAND 3
#elif defined (FRONTEND_SUPPORTS_RGB565)
#define RED_SHIFT 11
#define GREEN_SHIFT 5
#define BLUE_SHIFT 0
#define RED_EXPAND 3
#define GREEN_EXPAND 2
#define BLUE_EXPAND 3
#define RED_MASK 0xF800
#define GREEN_MASK 0x7e0
#define BLUE_MASK 0x1f
#else
#define RED_SHIFT 10
#define GREEN_SHIFT 5
#define BLUE_SHIFT 0
#define RED_EXPAND 3
#define GREEN_EXPAND 3
#define BLUE_EXPAND 3
#endif


void FCEUD_SetPalette(unsigned char index, unsigned char r, unsigned char g, unsigned char b)
{
#ifdef FRONTEND_SUPPORTS_RGB565
   retro_palette[index] = BUILD_PIXEL_RGB565(r >> RED_EXPAND, g >> GREEN_EXPAND, b >> BLUE_EXPAND);
#else
   retro_palette[index] =
      ((r >> RED_EXPAND) << RED_SHIFT) | ((g >> GREEN_EXPAND) << GREEN_SHIFT) | ((b >> BLUE_EXPAND) << BLUE_SHIFT);
#endif
}

void FCEUD_GetPalette(unsigned char i, unsigned char *r, unsigned char *g, unsigned char *b)
{
}


bool FCEUD_ShouldDrawInputAids (void)
{
   return 1;
}

static struct retro_log_callback log_cb;

static void default_logger(enum retro_log_level level, const char *fmt, ...) {}

void FCEUD_PrintError(char *c)
{
   log_cb.log(RETRO_LOG_WARN, "%s", c);
}

void FCEUD_Message(char *s)
{
   log_cb.log(RETRO_LOG_INFO, "%s", s);
}

void FCEUD_NetworkClose(void)
{ }

void FCEUD_SoundToggle (void)
{
   FCEUI_SetSoundVolume(100);
}

void FCEUD_VideoChanged (void)
{ }

FILE *FCEUD_UTF8fopen(const char *n, const char *m)
{
   return fopen(n, m);
}

#define MAX_PATH 1024

struct st_palettes {
	char name[32];
	char desc[32];
	unsigned int data[64];
};


struct st_palettes palettes[] = {
   { "crashman", "CrashMan's palette",
       { 0X585858, 0X001173, 0X000062, 0X472BBF,
         0X970087, 0X910009, 0X6F1100, 0X4C1008,
         0X371E00, 0X002F00, 0X005500, 0X004D15,
         0X002840, 0X000000, 0X000000, 0X000000,
         0XA0A0A0, 0X004499, 0X2C2CC8, 0X590DAA,
         0XAE006A, 0XB00040, 0XB83418, 0X983010,
         0X704000, 0X308000, 0X207808, 0X007B33,
         0X1C6888, 0X000000, 0X000000, 0X000000,
         0XF8F8F8, 0X267BE1, 0X5870F0, 0X9878F8,
         0XFF73C8, 0XF060A8, 0XD07B37, 0XE09040,
         0XF8B300, 0X8CBC00, 0X40A858, 0X58F898,
         0X00B7BF, 0X787878, 0X000000, 0X000000,
         0XFFFFFF, 0XA7E7FF, 0XB8B8F8, 0XD8B8F8,
         0XE6A6FF, 0XF29DC4, 0XF0C0B0, 0XFCE4B0,
         0XE0E01E, 0XD8F878, 0XC0E890, 0X95F7C8,
         0X98E0E8, 0XF8D8F8, 0X000000, 0X000000 }
   },
   { "nintendo-rgb", "Nintendo RGB PPU palette",
       { 0x6D6D6D, 0x002492, 0x0000DB, 0x6D49DB,
         0x92006D, 0xB6006D, 0xB62400, 0x924900,
         0x6D4900, 0x244900, 0x006D24, 0x009200,
         0x004949, 0x000000, 0x000000, 0x000000,
         0xB6B6B6, 0x006DDB, 0x0049FF, 0x9200FF,
         0xB600FF, 0xFF0092, 0xFF0000, 0xDB6D00,
         0x926D00, 0x249200, 0x009200, 0x00B66D,
         0x009292, 0x242424, 0x000000, 0x000000,
         0xFFFFFF, 0x6DB6FF, 0x9292FF, 0xDB6DFF,
         0xFF00FF, 0xFF6DFF, 0xFF9200, 0xFFB600,
         0xDBDB00, 0x6DDB00, 0x00FF00, 0x49FFDB,
         0x00FFFF, 0x494949, 0x000000, 0x000000,
         0xFFFFFF, 0xB6DBFF, 0xDBB6FF, 0xFFB6FF,
         0xFF92FF, 0xFFB6B6, 0xFFDB92, 0xFFFF49,
         0xFFFF6D, 0xB6FF49, 0x92FF6D, 0x49FFDB,
         0x92DBFF, 0x929292, 0x000000, 0x000000 }
   },   
   { "nintendo-vc", "Virtual Console palette",
       { 0X494949, 0X00006A, 0X090063, 0X290059,
         0X42004A, 0X490000, 0X420000, 0X291100,
         0X182700, 0X003010, 0X003000, 0X002910,
         0X012043, 0X000000, 0X000000, 0X000000,
         0X747174, 0X003084, 0X3101AC, 0X4B0194,
         0X64007B, 0X6B0039, 0X6B2101, 0X5A2F00,
         0X424900, 0X185901, 0X105901, 0X015932,
         0X01495A, 0X101010, 0X000000, 0X000000,
         0XADADAD, 0X4A71B6, 0X6458D5, 0X8450E6,
         0XA451AD, 0XAD4984, 0XB5624A, 0X947132,
         0X7B722A, 0X5A8601, 0X388E31, 0X318E5A,
         0X398E8D, 0X383838, 0X000000, 0X000000,
         0XB6B6B6, 0X8C9DB5, 0X8D8EAE, 0X9C8EBC,
         0XA687BC, 0XAD8D9D, 0XAE968C, 0X9C8F7C,
         0X9C9E72, 0X94A67C, 0X84A77B, 0X7C9D84,
         0X73968D, 0XDEDEDE, 0X000000, 0X000000 }
   },
   { "canonical", "Canonical palette",
       { 0x666666, 0x002A88, 0x1412A7, 0x3B00A4,
         0x5C007E, 0x6E0040, 0x6C0700, 0x561D00,
         0x333500, 0x0C4800, 0x005200, 0x004F08,
         0x00404D, 0x000000, 0x000000, 0x000000,
         0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE,
         0xA01ACC, 0xB71E7B, 0xB53120, 0x994E00,
         0x6B6D00, 0x388700, 0x0D9300, 0x008F32,
         0x007C8D, 0x000000, 0x000000, 0x000000,
         0xFFFFFF, 0x64B0FF, 0x9290FF, 0xC676FF,
         0xF26AFF, 0xFF6ECC, 0xFF8170, 0xEA9E22,
         0xBCBE00, 0x88D800, 0x5CE430, 0x45E082,
         0x48CDDE, 0x4F4F4F, 0x000000, 0x000000,
         0xFFFFFF, 0xC0DFFF, 0xD3D2FF, 0xE8C8FF,
         0xFAC2FF, 0xFFC4EA, 0xFFCCC5, 0xF7D8A5,
         0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC,
         0xB5EBF2, 0xB8B8B8, 0x000000, 0x000000 }
   },
   { "sony-cxa2025as", "Sony CXA2025AS US palette",
       { 0x585858, 0x00238C, 0x00139B, 0x2D0585,
         0x5D0052, 0x7A0017, 0x7A0800, 0x5F1800,
         0x352A00, 0x093900, 0x003F00, 0x003C22,
         0x00325D, 0x000000, 0x000000, 0x000000,
         0xA1A1A1, 0x0053EE, 0x153CFE, 0x6028E4,
         0xA91D98, 0xD41E41, 0xD22C00, 0xAA4400,
         0x6C5E00, 0x2D7300, 0x007D06, 0x007852,
         0x0069A9, 0x000000, 0x000000, 0x000000,
         0xFFFFFF, 0x1FA5FE, 0x5E89FE, 0xB572FE,
         0xFE65F6, 0xFE6790, 0xFE773C, 0xFE9308,
         0xC4B200, 0x79CA10, 0x3AD54A, 0x11D1A4,
         0x06BFFE, 0x424242, 0x000000, 0x000000,
         0xFFFFFF, 0xA0D9FE, 0xBDCCFE, 0xE1C2FE,
         0xFEBCFB, 0xFEBDD0, 0xFEC5A9, 0xFED18E,
         0xE9DE86, 0xC7E992, 0xA8EEB0, 0x95ECD9,
         0x91E4FE, 0xACACAC, 0x000000, 0x000000 }
   },
   { "bmf-final_2", "BMF's Final 2 palette",
       { 0x525252, 0x000080, 0x08008A, 0x2C007E,
         0x4A004E, 0x500006, 0x440000, 0x260800,
         0x0A2000, 0x002E00, 0x003200, 0x00260A,
         0x001C48, 0x000000, 0x000000, 0x000000,
         0xA4A4A4, 0x0038CE, 0x3416EC, 0x5E04DC,
         0x8C00B0, 0x9A004C, 0x901800, 0x703600,
         0x4C5400, 0x0E6C00, 0x007400, 0x006C2C,
         0x005E84, 0x000000, 0x000000, 0x000000,
         0xFFFFFF, 0x4C9CFF, 0x7C78FF, 0xA664FF,
         0xDA5AFF, 0xF054C0, 0xF06A56, 0xD68610,
         0xBAA400, 0x76C000, 0x46CC1A, 0x2EC866,
         0x34C2BE, 0x3A3A3A, 0x000000, 0x000000,
         0xFFFFFF, 0xB6DAFF, 0xC8CAFF, 0xDAC2FF,
         0xF0BEFF, 0xFCBCEE, 0xFAC2C0, 0xF2CCA2,
         0xE6DA92, 0xCCE68E, 0xB8EEA2, 0xAEEABE,
         0xAEE8E2, 0xB0B0B0, 0x000000, 0x000000 }
   },
   { "fbx-unsaturated_v6", "FBX's Unsaturated-V6 palette",
       { 0x6B6B6B, 0x001E87, 0x1F0B96, 0x3B0C87,
         0x590D61, 0x5E0528, 0x551100, 0x461B00,
         0x303200, 0x0A4800, 0x004E00, 0x004619,
         0x003A58, 0x000000, 0x000000, 0x000000,
         0xB2B2B2, 0x1A53D1, 0x4835EE, 0x7123EC,
         0x9A1EB7, 0xA51E62, 0xA52D19, 0x874B00,
         0x676900, 0x298400, 0x038B00, 0x008240,
         0x007891, 0x000000, 0x000000, 0x000000,
         0xFFFFFF, 0x63ADFD, 0x908AFE, 0xB977FC,
         0xE771FE, 0xF76FC9, 0xF5836A, 0xDD9C29,
         0xBDB807, 0x84D107, 0x5BDC3B, 0x48D77D,
         0x48CCCE, 0x555555, 0x000000, 0x000000,
         0xFFFFFF, 0xC4E3FE, 0xD7D5FE, 0xE6CDFE,
         0xF9CAFE, 0xFEC9F0, 0xFED1C7, 0xF7DCAC,
         0xE8E89C, 0xD1F29D, 0xBFF4B1, 0xB7F5CD,
         0xB7F0EE, 0xBEBEBE, 0x000000, 0x000000 }
   },   
   { "fbx-smooth", "FBX's Smooth palette",
       { 0x6A6D6A, 0x001380, 0x1E008A, 0x39007A,
         0x550056, 0x5A0018, 0x4F1000, 0x3D1C00,
         0x253200, 0x003D00, 0x004000, 0x003924,
         0x002E55, 0x000000, 0x000000, 0x000000,
         0xB9BCB9, 0x1850C7, 0x4B30E3, 0x7322D6,
         0x951FA9, 0x9D285C, 0x983700, 0x7F4C00,
         0x5E6400, 0x227700, 0x027E02, 0x007645,
         0x006E8A, 0x000000, 0x000000, 0x000000,
         0xFFFFFF, 0x68A6FF, 0x8C9CFF, 0xB586FF,
         0xD975FD, 0xE377B9, 0xE58D68, 0xD49D29,
         0xB3AF0C, 0x7BC211, 0x55CA47, 0x46CB81,
         0x47C1C5, 0x4A4D4A, 0x000000, 0x000000,
         0xFFFFFF, 0xCCEAFF, 0xDDDEFF, 0xECDAFF,
         0xF8D7FE, 0xFCD6F5, 0xFDDBCF, 0xF9E7B5,
         0xF1F0AA, 0xDAFAA9, 0xC9FFBC, 0xC3FBD7,
         0xC4F6F6, 0xBEC1BE, 0x000000, 0x000000 }
   },   
   { "fbx-composite_direct", "FBX's Composite Direct palette",
       { 0x656565, 0x00127D, 0x18008E, 0x360082,
         0x56005D, 0x5A0018, 0x4F0500, 0x381900,
         0x1D3100, 0x003D00, 0x004100, 0x003B17,
         0x002E55, 0x000000, 0x000000, 0x000000,
         0xAFAFAF, 0x194EC8, 0x472FE3, 0x6B1FD7,
         0x931BAE, 0x9E1A5E, 0x993200, 0x7B4B00,
         0x5B6700, 0x267A00, 0x008200, 0x007A3E,
         0x006E8A, 0x000000, 0x000000, 0x000000,
         0xFFFFFF, 0x64A9FF, 0x8E89FF, 0xB676FF,
         0xE06FFF, 0xEF6CC4, 0xF0806A, 0xD8982C,
         0xB9B40A, 0x83CB0C, 0x5BD63F, 0x4AD17E,
         0x4DC7CB, 0x4C4C4C, 0x000000, 0x000000,
         0xFFFFFF, 0xC7E5FF, 0xD9D9FF, 0xE9D1FF,
         0xF9CEFF, 0xFFCCF1, 0xFFD4CB, 0xF8DFB1,
         0xEDEAA4, 0xD6F4A4, 0xC5F8B8, 0xBEF6D3,
         0xBFF1F1, 0xB9B9B9, 0x000000, 0x000000 }
   },
   { "fbx-pvm_style_d93", "FBX's PVM Style D93 palette",
       { 0x696B63, 0x001774, 0x1E0087, 0x340073,
         0x560057, 0x5E0013, 0x531A00, 0x3B2400,
         0x243000, 0x063A00, 0x003F00, 0x003B1E,
         0x00334E, 0x000000, 0x000000, 0x000000,
         0xB9BBB3, 0x1453B9, 0x4D2CDA, 0x671EDE,
         0x98189C, 0x9D2344, 0xA03E00, 0x8D5500,
         0x656D00, 0x2C7900, 0x008100, 0x007D42,
         0x00788A, 0x000000, 0x000000, 0x000000,
         0xFFFFFF, 0x69A8FF, 0x9691FF, 0xB28AFA,
         0xEA7DFA, 0xF37BC7, 0xF28E59, 0xE6AD27,
         0xD7C805, 0x90DF07, 0x64E53C, 0x45E27D,
         0x48D5D9, 0x4E5048, 0x000000, 0x000000,
         0xFFFFFF, 0xD2EAFF, 0xE2E2FF, 0xE9D8FF,
         0xF5D2FF, 0xF8D9EA, 0xFADEB9, 0xF9E89B,
         0xF3F28C, 0xD3FA91, 0xB8FCA8, 0xAEFACA,
         0xCAF3F3, 0xBEC0B8, 0x000000, 0x000000 }
   },
   { "fbx-ntsc_hardware", "FBX's NTSC Hardware palette",
       { 0x6A6D6A, 0x001380, 0x1E008A, 0x39007A,
         0x550056, 0x5A0018, 0x4F1000, 0x382100,
         0x213300, 0x003D00, 0x004000, 0x003924,
         0x002E55, 0x000000, 0x000000, 0x000000,
         0xB9BCB9, 0x1850C7, 0x4B30E3, 0x7322D6,
         0x951FA9, 0x9D285C, 0x963C00, 0x7A5100,
         0x5B6700, 0x227700, 0x027E02, 0x007645,
         0x006E8A, 0x000000, 0x000000, 0x000000,
         0xFFFFFF, 0x68A6FF, 0x9299FF, 0xB085FF,
         0xD975FD, 0xE377B9, 0xE58D68, 0xCFA22C,
         0xB3AF0C, 0x7BC211, 0x55CA47, 0x46CB81,
         0x47C1C5, 0x4A4D4A, 0x000000, 0x000000,
         0xFFFFFF, 0xCCEAFF, 0xDDDEFF, 0xECDAFF,
         0xF8D7FE, 0xFCD6F5, 0xFDDBCF, 0xF9E7B5,
         0xF1F0AA, 0xDAFAA9, 0xC9FFBC, 0xC3FBD7,
         0xC4F6F6, 0xBEC1BE, 0x000000, 0x000000 }
   },
   { "fbx-nes_classic", "FBX-FS's NES Classic palette",
       { 0x60615F, 0x000083, 0x1D0195, 0x340875,
         0x51055E, 0x56000F, 0x4C0700, 0x372308,
         0x203A0B, 0x0F4B0E, 0x194C16, 0x02421E,
         0x023154, 0x000000, 0x000000, 0x000000,
         0xA9AAA8, 0x104BBF, 0x4712D8, 0x6300CA,
         0x8800A9, 0x930B46, 0x8A2D04, 0x6F5206,
         0x5C7114, 0x1B8D12, 0x199509, 0x178448,
         0x206B8E, 0x000000, 0x000000, 0x000000,
         0xFBFBFB, 0x6699F8, 0x8974F9, 0xAB58F8,
         0xD557EF, 0xDE5FA9, 0xDC7F59, 0xC7A224,
         0xA7BE03, 0x75D703, 0x60E34F, 0x3CD68D,
         0x56C9CC, 0x414240, 0x000000, 0x000000,
         0xFBFBFB, 0xBED4FA, 0xC9C7F9, 0xD7BEFA,
         0xE8B8F9, 0xF5BAE5, 0xF3CAC2, 0xDFCDA7,
         0xD9E09C, 0xC9EB9E, 0xC0EDB8, 0xB5F4C7,
         0xB9EAE9, 0xABABAB, 0x000000, 0x000000 }
   },
   { "rgbs-nescap", "RGBSource's NESCAP palette",
       { 0x646365, 0x001580, 0x1D0090, 0x380082,
         0x56005D, 0x5A001A, 0x4F0900, 0x381B00,
         0x1E3100, 0x003D00, 0x004100, 0x003A1B,
         0x002F55, 0x000000, 0x000000, 0x000000,
         0xAFADAF, 0x164BCA, 0x472AE7, 0x6B1BDB,
         0x9617B0, 0x9F185B, 0x963001, 0x7B4800,
         0x5A6600, 0x237800, 0x017F00, 0x00783D,
         0x006C8C, 0x000000, 0x000000, 0x000000,
         0xFFFFFF, 0x60A6FF, 0x8F84FF, 0xB473FF,
         0xE26CFF, 0xF268C3, 0xEF7E61, 0xD89527,
         0xBAB307, 0x81C807, 0x57D43D, 0x47CF7E,
         0x4BC5CD, 0x4C4B4D, 0x000000, 0x000000,
         0xFFFFFF, 0xC2E0FF, 0xD5D2FF, 0xE3CBFF,
         0xF7C8FF, 0xFEC6EE, 0xFECEC6, 0xF6D7AE,
         0xE9E49F, 0xD3ED9D, 0xC0F2B2, 0xB9F1CC,
         0xBAEDED, 0xBAB9BB, 0x000000, 0x000000 }
   },
   { "wavebeam", "Nakedarthur's Wavebeam palette",
       { 0X6B6B6B, 0X001B88, 0X21009A, 0X40008C,
         0X600067, 0X64001E, 0X590800, 0X481600,
         0X283600, 0X004500, 0X004908, 0X00421D,
         0X003659, 0X000000, 0X000000, 0X000000,
         0XB4B4B4, 0X1555D3, 0X4337EF, 0X7425DF,
         0X9C19B9, 0XAC0F64, 0XAA2C00, 0X8A4B00,
         0X666B00, 0X218300, 0X008A00, 0X008144,
         0X007691, 0X000000, 0X000000, 0X000000,
         0XFFFFFF, 0X63B2FF, 0X7C9CFF, 0XC07DFE,
         0XE977FF, 0XF572CD, 0XF4886B, 0XDDA029,
         0XBDBD0A, 0X89D20E, 0X5CDE3E, 0X4BD886,
         0X4DCFD2, 0X525252, 0X000000, 0X000000,
         0XFFFFFF, 0XBCDFFF, 0XD2D2FF, 0XE1C8FF,
         0XEFC7FF, 0XFFC3E1, 0XFFCAC6, 0XF2DAAD,
         0XEBE3A0, 0XD2EDA2, 0XBCF4B4, 0XB5F1CE,
         0XB6ECF1, 0XBFBFBF, 0X000000, 0X000000 }
   },
   { "fcc-1953", "FCC 1953 NTSC Standard palette",
       { 0x4A444D, 0x000763, 0x000783, 0x02077D,
         0x370353, 0x5A000F, 0x5D0000, 0x430000,
         0x120200, 0x001400, 0x001D00, 0x001D00,
         0x001424, 0x000000, 0x000000, 0x000000,
         0xA599AD, 0x0049CB, 0x0530F6, 0x5918EF,
         0xA407B7, 0xD2025D, 0xD60A00, 0xAB2200,
         0x613D00, 0x0E5500, 0x006300, 0x006316,
         0x005B79, 0x000000, 0x000000, 0x000000,
         0xFFF9FF, 0x2C99FF, 0x5B75FF, 0x9C67FF,
         0xFD6BFF, 0xFF6DC3, 0xFF7471, 0xFF7D1C,
         0xCF9500, 0x7AAD00, 0x2EBD25, 0x00C380,
         0x00BBE4, 0x2D2A2F, 0x000000, 0x000000,
         0xFFF9FF, 0xAAD0FF, 0xB9BCFF, 0xD8B6FF,
         0xFFBEFF, 0xFFBDF4, 0xFFC0CF, 0xFFC6AE,
         0xF3CF9A, 0xD0D99D, 0xB0E0B4, 0x9DE2DA,
         0x9CDEFF, 0xB0A3B8, 0x000000, 0x000000 }
   }      
};

static void draw_crosshair(int x, int y, uint8_t *gfx)
{
   int i;
   /* never draw it outside of the buffer */
   if (x < 2 || x > 253 || y < 2 || y > 237) return;

   for(i = -2; i < 3; i += 4) {
      gfx[256 * y + x + i] = 0x00;
      gfx[256 * (y + i) + x] = 0x00;
   }

   for(i = -1; i < 3; i += 2) {
      gfx[256 * y + x + i] = 0x20;
      gfx[256 * (y + i) + x] = 0x20;
   }
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{ }

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_cb = cb;
}

void retro_set_controller_port_device(unsigned a, unsigned b)
{}


void retro_set_environment(retro_environment_t cb)
{
   static const struct retro_variable vars[] = {
      { "nes_palette", "Color Palette; crashman|nintendo-rgb|nintendo-vc|canonical|sony-cxa2025as|bmf-final_2|fbx-unsaturated_v6|fbx-smooth|fbx-composite_direct|fbx-pvm_style_d93|fbx-ntsc_hardware|fbx-nes_classic|rgbs-nescap|wavebeam|fcc-1953|default" },
      { "nes_nospritelimit", "No Sprite Limit; disabled|enabled" },
      { "nes_zappercrosshair", "Crosshair In Zapper Games; enabled|disabled" },
      { NULL, NULL },
   };

   environ_cb = cb;
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
}

void retro_get_system_info(struct retro_system_info *info)
{
   info->need_fullpath = false;
   info->valid_extensions = "fds|nes|unif";
   info->library_version = "(SVN)";
   info->library_name = "FCEUmm";
   info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   unsigned width = use_overscan ? 256 : (256 - 16);
   unsigned height = use_overscan ? 240 : (240 - 16);
   info->geometry.base_width = width;
   info->geometry.base_height = height;
   info->geometry.max_width = width;
   info->geometry.max_height = height;
   info->geometry.aspect_ratio = 4.0 / 3.0;
   info->timing.sample_rate = 32050.0;
   if (FSettings.PAL)
      info->timing.fps = 838977920.0/16777215.0;
   else
      info->timing.fps = 1008307711.0/16777215.0;
   info->timing.sample_rate = 32040.5;
}

static void check_system_specs(void)
{
   // TODO - when we get it running at fullspeed on PSP, set to 4
   unsigned level = 5;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_init(void)
{
   enum retro_pixel_format rgb565;
   log_cb.log=default_logger;
   environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log_cb);
#ifdef FRONTEND_SUPPORTS_RGB565
   rgb565 = RETRO_PIXEL_FORMAT_RGB565;
   if(environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &rgb565))
      log_cb.log(RETRO_LOG_INFO, "Frontend supports RGB565 - will use that instead of XRGB1555.\n");
#endif
   PowerNES();
   check_system_specs();
#if defined(_3DS)
   fceu_video_out = (uint16_t*)linearMemAlign(256 * 240 * sizeof(uint16_t), 128);
#elif !defined(PSP)
   fceu_video_out = (uint16_t*)malloc(256 * 240 * sizeof(uint16_t));
#endif
}

static void retro_set_custom_palette (void)
{
   uint8_t i,r,g,b;

   /* Reset to default fceumm palette */
   if (current_palette == -1)
   {
      FCEU_ResetPalette();
      return;
   }

   /* Setup selected palette*/
   for ( i = 0; i < 64; i++ )
   {
      r = palettes[current_palette].data[i] >> 16;
      g = ( palettes[current_palette].data[i] & 0xff00 ) >> 8;
      b = ( palettes[current_palette].data[i] & 0xff );
      FCEUD_SetPalette( i, r, g, b);
      FCEUD_SetPalette( i+64, r, g, b);
      FCEUD_SetPalette( i+128, r, g, b);
      FCEUD_SetPalette( i+192, r, g, b);
   }
}

void retro_deinit (void)
{
   FCEUI_CloseGame();
   FCEUI_Sound(0);
   FCEUI_Kill();
#if defined(_3DS)
   linearFree(fceu_video_out);
#else
   if (fceu_video_out)
      free(fceu_video_out);
   fceu_video_out = NULL;
#endif
}

void retro_reset(void)
{
   ResetNES();
}


typedef struct
{
   unsigned retro;
   unsigned nes;
} keymap;

static const keymap bindmap[] = {
   { RETRO_DEVICE_ID_JOYPAD_A, JOY_A },
   { RETRO_DEVICE_ID_JOYPAD_B, JOY_B },
   { RETRO_DEVICE_ID_JOYPAD_SELECT, JOY_SELECT },
   { RETRO_DEVICE_ID_JOYPAD_START, JOY_START },
   { RETRO_DEVICE_ID_JOYPAD_UP, JOY_UP },
   { RETRO_DEVICE_ID_JOYPAD_DOWN, JOY_DOWN },
   { RETRO_DEVICE_ID_JOYPAD_LEFT, JOY_LEFT },
   { RETRO_DEVICE_ID_JOYPAD_RIGHT, JOY_RIGHT },
};

static void check_variables(void)
{
   struct retro_variable var = {0};

   var.key = "nes_palette";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int32_t p;
      uint32_t s = sizeof(palettes)/sizeof(struct st_palettes);
      for (p = 0; strcmp(var.value, palettes[p].name) != 0 && p < s; p++);

      if (p >= s)
         current_palette = -1;
      else
         current_palette = p;
      
      retro_set_custom_palette();
   }
   var.key = "nes_nospritelimit";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int no_sprite_limit = (strcmp(var.value, "enabled") == 0) ? 1 : 0;
      FCEUI_DisableSpriteLimitation(no_sprite_limit);
   }

   var.key = "nes_zappercrosshair";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      show_zapper_crosshair = (GameInfo->input[1] == SI_ZAPPER && strcmp(var.value, "enabled") == 0) ? true : false;
   }
}

static void FCEUD_UpdateInput(void)
{
   unsigned i;
   unsigned char pad[2];

   pad[0] = 0;
   pad[1] = 0;

   poll_cb();

   for ( i = 0; i < 8; i++)
      pad[0] |= input_cb(0, RETRO_DEVICE_JOYPAD, 0, bindmap[i].retro) ? bindmap[i].nes : 0;

   for ( i = 0; i < 8; i++)
      pad[1] |= input_cb(1, RETRO_DEVICE_JOYPAD, 0, bindmap[i].retro) ? bindmap[i].nes : 0;

   JSReturn = pad[0] | (pad[1] << 8);

   if (input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X))
   {
      FCEU_VSUniCoin(); /* Insert Coin VS System */
   }

   if (GameInfo->type == GIT_FDS) /* Famicom Disk System */
   {
      bool curL = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
      static bool prevL = false;
      if (curL && !prevL)
      {
         FCEU_FDSSelect(); /* Swap FDisk side */
      }
      prevL = curL;

      bool curR = input_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
      static bool prevR = false;
      if (curR && !prevR)
      {
         FCEU_FDSInsert(-1); /* Insert or eject the disk */
      }
      prevR = curR;
   }

   if (GameInfo->input[1] == SI_ZAPPER) /* Zapper compatible games */
   {
#ifdef GEKKO
      /* For Wii we need the lightgun info as absolute coords to work properly */
      zapx = (input_cb(1, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_X) * 256 / 640);
      zapy = (input_cb(1, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_Y) * 240 / 480);
#else
      zapx += input_cb(1, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_X);
      zapy += input_cb(1, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_Y);

      if (zapx > 255) zapx = 255;
      else if (zapx < 0) zapx = 0;

      if (zapy > 239) zapy = 239;
      else if (zapy < 0) zapy = 0;
#endif
      ZapperInfo[0] = zapx;
      ZapperInfo[1] = zapy;
      ZapperInfo[2] = 0x0;
      if (input_cb(1, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER))
         ZapperInfo[2] = 0x01; /* normal trigger */
      if (input_cb(1, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TURBO))
         ZapperInfo[2] |= 0x02; /* hold the trigger so it means we shoot outside of the screen */
   }
}

void FCEUD_Update(uint8 *XBuf, int32 *Buffer, int Count)
{
}

static void retro_run_blit(uint8_t *gfx)
{
   unsigned x, y;
   unsigned incr   = 0;
   unsigned width  = 256;
   unsigned height = 240;
   unsigned pitch  = 512;

   if (!use_overscan)
   {
      incr    = 16;
      width  -= 16;
      height -= 16;
#ifndef PSP
      pitch  -= 32;
      gfx     = gfx + 8 + 256 * 8;

#endif
   }

#ifdef PSP
   static unsigned int __attribute__((aligned(16))) d_list[32];
   void* const texture_vram_p = (void*) (0x44200000 - (256 * 256)); // max VRAM address - frame size

   sceKernelDcacheWritebackRange(retro_palette,256 * 2);
   sceKernelDcacheWritebackRange(XBuf, 256*240 );

   sceGuStart(GU_DIRECT, d_list);

   /* sceGuCopyImage doesnt seem to work correctly with GU_PSM_T8
    * so we use GU_PSM_4444 ( 2 Bytes per pixel ) instead
    * with half the values for pitch / width / x offset
    */
   if (use_overscan)
      sceGuCopyImage(GU_PSM_4444, 0, 0, 128, 240, 128, XBuf, 0, 0, 128, texture_vram_p);
   else
      sceGuCopyImage(GU_PSM_4444, 4, 4, 120, 224, 128, XBuf, 0, 0, 128, texture_vram_p);

   sceGuTexSync();
   sceGuTexImage(0, 256, 256, 256, texture_vram_p);
   sceGuTexMode(GU_PSM_T8, 0, 0, GU_FALSE);
   sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
   sceGuDisable(GU_BLEND);
   sceGuClutMode(GU_PSM_5650, 0, 0xFF, 0);
   sceGuClutLoad(32, retro_palette);

   sceGuFinish();

   video_cb(texture_vram_p, width, height, 256);
#else
   for (y = 0; y < height; y++, gfx += incr)
      for ( x = 0; x < width; x++, gfx++)
         fceu_video_out[y * width + x] = retro_palette[*gfx];

   video_cb(fceu_video_out, width, height, pitch);
#endif

}

void retro_run(void)
{
   unsigned i;
   uint8_t *gfx;
   int32_t ssize = 0;
   bool updated = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();

   FCEUD_UpdateInput();
   FCEUI_Emulate(&gfx, &sound, &ssize, 0);

   for (i = 0; i < ssize; i++)
      sound[i] = (sound[i] << 16) | (sound[i] & 0xffff);

   audio_batch_cb((const int16_t*)sound, ssize);

   if (show_zapper_crosshair)
      draw_crosshair(zapx, zapy, gfx);

   retro_run_blit(gfx);
}

static unsigned serialize_size = 0;

size_t retro_serialize_size(void)
{
   if (serialize_size == 0)
   {
      /* Something arbitrarily big.*/
      uint8_t *buffer = (uint8_t*)malloc(1000000);
      memstream_set_buffer(buffer, 1000000);

      FCEUSS_Save_Mem();
      serialize_size = memstream_get_last_size();
      free(buffer);
   }

   return serialize_size;
}

bool retro_serialize(void *data, size_t size)
{
   if (size != retro_serialize_size())
      return false;

   memstream_set_buffer((uint8_t*)data, size);
   FCEUSS_Save_Mem();
   return true;
}

bool retro_unserialize(const void * data, size_t size)
{
   if (size != retro_serialize_size())
      return false;

   memstream_set_buffer((uint8_t*)data, size);
   FCEUSS_Load_Mem();
   return true;
}

void retro_cheat_reset(void)
{
   FCEU_ResetCheats();
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   char name[256];
   uint16 a;
   uint8  v;
   int    c;
   int    type = 1;
   sprintf(name, "N/A");

   if (FCEUI_DecodeGG(code, &a, &v, &c))
      goto input_cheat;

   /* Not a Game Genie code. */

   if (FCEUI_DecodePAR(code, &a, &v, &c, &type))
      goto input_cheat;

   /* Not a Pro Action Replay code. */

   return;
input_cheat:
   FCEUI_AddCheat(name, a, v, c, type);
}

typedef struct cartridge_db
{
   char title[256];
   uint32_t crc;
} cartridge_db_t;

static const struct cartridge_db fourscore_db_list[] =
{
   {
      "Bomberman II (USA)",
      0x1ebb5b42
   },
#if 0
   {
      "Championship Bowling (USA)",
      0xeac38105
   },
#endif
   {
      "Chris Evert & Ivan Lendl in Top Players' Tennis (USA)",
      0xf99e37eb
   },
#if 0
   {
      "Crash 'n' the Boys - Street Challenge (USA)",
      0xc7f0c457
   },
#endif
   {
      "Four Players' Tennis (Europe)",
      0x48b8ee58
   },
   {
      "Danny Sullivan's Indy Heat (Europe)",
      0x27ca0679,
   },
   {
      "Gauntlet II (Europe)",
      0x79f688bc
   },
   {
      "Gauntlet II (USA)",
      0x1b71ccdb
   },
   {
      "Greg Norman's Golf Power (USA)",
      0x1352f1b9
   },
   {
      "Harlem Globetrotters (USA)",
      0x2e6ee98d
   },
   {
      "Ivan 'Ironman' Stewart's Super Off Road (Europe)",
      0x05104517
   },
   {
      "Ivan 'Ironman' Stewart's Super Off Road (USA)",
      0x4b041b6b
   },
   {
      "Kings of the Beach - Professional Beach Volleyball (USA)",
      0xf54b34bd
   },
   {
      "Magic Johnson's Fast Break (USA)",
      0xc6c2edb5
   },
   {
      "M.U.L.E. (USA)",
      0x0939852f
   },
   {
      "Monster Truck Rally (USA)",
      0x2f698c4d
   },
   {
      "NES Play Action Football (USA)",
      0xb9b4d9e0
   },
   {
      "Nightmare on Elm Street, A (USA)",
      0xda2cb59a
   },
   {
      "Nintendo World Cup (Europe)",
      0x8da6667d
   },
   {
      "Nintendo World Cup (Europe) (Rev A)",
      0x7c16f819
   },
   {
      "Nintendo World Cup (Europe) (Rev B)",
      0x7f08d0d9
   },
   {
      "Nintendo World Cup (USA)",
      0xa22657fa
   },
   {
      "R.C. Pro-Am II (Europe)",
      0x308da987
   },
   {
      "R.C. Pro-Am II (USA)",
      0x9edd2159
   },
   {
      "Rackets & Rivals (Europe)",
      0x8fa6e92c
   },
   {
      "Roundball - 2-on-2 Challenge (Europe)",
      0xad0394f0
   },
   {
      "Roundball - 2-on-2 Challenge (USA)",
      0x6e4dcfd2
   },
   {
      "Spot - The Video Game (Japan)",
      0x0abdd5ca
   },
   {
      "Spot - The Video Game (USA)",
      0xcfae9dfa
   },
   {
      "Smash T.V. (Europe)",
      0x0b8f8128
   },
   {
      "Smash T.V. (USA)",
      0x6ee94d32
   },
   {
      "Super Jeopardy! (USA)",
      0xcf4487a2
   },
   {
      "Super Spike V'Ball (Europe)",
      0xc05a63b2
   },
   {
      "Super Spike V'Ball (USA)",
      0xe840fd21
   },
   {
      "Super Spike V'Ball + Nintendo World Cup (USA)",
      0x407d6ffd
   },
   {
      "Swords and Serpents (Europe)",
      0xd153caf6
   },
   {
      "Swords and Serpents (France)",
      0x46135141
   },
   {
      "Swords and Serpents (USA)",
      0x3417ec46
   },
   {
      "Battle City (Japan) (4 Players Hack) http://www.romhacking.net/hacks/2142/",
      0x69977c9e
   },
   {
      "Bomberman 3 (Homebrew) http://tempect.de/senil/games.html",
      0x2da5ece0
   },
   {
      "K.Y.F.F. (Homebrew) http://slydogstudios.org/index.php/k-y-f-f/",
      0x90d2e9f0
   },
   {
      "Super PakPak (Homebrew) http://wiki.nesdev.com/w/index.php/Super_PakPak",
      0x1394ded0
   },
   {
      "Super Mario Bros. + Tetris + Nintendo World Cup (Europe)",
      0x73298c87
   },
   {
      "Super Mario Bros. + Tetris + Nintendo World Cup (Europe) (Rev A)",
      0xf46ef39a
   }
};

static const struct cartridge_db famicom_4p_db_list[] =
{
   {
      "Bakutoushi Patton-Kun (Japan) (FDS)",
      0xc39b3bb2
   },
   {
      "Bomber Man II (Japan)",
      0x0c401790
   },
   {
      "Championship Bowling (Japan)",
      0x9992f445
   },
   {
      "Downtown - Nekketsu Koushinkyoku - Soreyuke Daiundoukai (Japan)",
      0x3e470fe0
   },
   {
      "Ike Ike! Nekketsu Hockey-bu - Subette Koronde Dairantou (Japan)",
      0x4f032933
   },
   {
      "Kunio-kun no Nekketsu Soccer League (Japan)",
      0x4b5177e9
   },
   {
      "Moero TwinBee - Cinnamon Hakase o Sukue! (Japan)",
      0x9f03b11f
   },
   {
      "Moero TwinBee - Cinnamon Hakase wo Sukue! (Japan) (FDS)",
      0x13205221
   },
   {
      "Nekketsu Kakutou Densetsu (Japan)",
      0x37e24797
   },
   {
      "Nekketsu Koukou Dodgeball-bu (Japan)",
      0x62c67984
   },
   {
      "Nekketsu! Street Basket - Ganbare Dunk Heroes (Japan)",
      0x88062d9a
   },
   {
      "Super Dodge Ball (USA) (3-4p with Game Genie code GEUOLZZA)",
      0x689971f9
   },
   {
      "Super Dodge Ball (USA) (patched) http://www.romhacking.net/hacks/71/",
      0x4ff17864
   },
   {
      "U.S. Championship V'Ball (Japan)",
      0x213cb3fb
   },
   {
      "U.S. Championship V'Ball (Japan) (Beta)",
      0xd7077d96
   },
   {
      "Wit's (Japan)",
      0xb1b16b8a
   }
};

extern uint32_t iNESGameCRC32;

bool retro_load_game(const struct retro_game_info *game)
{
   unsigned i;
   char* dir=NULL;
   size_t fourscore_len = sizeof(fourscore_db_list)   / sizeof(fourscore_db_list[0]);
   size_t famicom_4p_len = sizeof(famicom_4p_db_list) / sizeof(famicom_4p_db_list[0]);

   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "(VSSystem) Insert Coin" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "(FDS) Disk Side Change" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "(FDS) Insert/Eject Disk" },

      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "B" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "A" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,   "Select" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },

      { 0 },
   };
   
   if (!game)
      return false;

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
      FCEUI_SetBaseDirectory(dir);

   FCEUI_Initialize();

   FCEUI_SetSoundVolume(256);
   FCEUI_Sound(32050);
   /* init zapper position */
   zapx = zapy = 0;

   GameInfo = (FCEUGI*)FCEUI_LoadGame(game->path, (uint8_t*)game->data, game->size);
   if (!GameInfo)
      return false;

   FCEUI_SetInput(0, SI_GAMEPAD, &JSReturn, 0);
   if (GameInfo->input[1] == SI_ZAPPER)
      FCEUI_SetInput(1, SI_ZAPPER, &ZapperInfo, 0);
   else
	  FCEUI_SetInput(1, SI_GAMEPAD, &JSReturn, 0);

   retro_set_custom_palette();

   FCEUD_SoundToggle();
   check_variables();

   if (!environ_cb(RETRO_ENVIRONMENT_GET_OVERSCAN, &use_overscan))
      use_overscan = true;

   FCEUI_DisableFourScore(1);

   for (i = 0; i < fourscore_len; i++)
   {
      if (fourscore_db_list[i].crc == iNESGameCRC32)
      {
         FCEUI_DisableFourScore(0);
         break;
      }
   }

   for (i = 0; i < famicom_4p_len; i++)
   {
      if (famicom_4p_db_list[i].crc == iNESGameCRC32)
      {
         FCEUI_SetInputFC(SIFC_4PLAYER, &JSReturn, 0);
         break;
      }
   }

   return true;
}

bool retro_load_game_special(
  unsigned game_type,
  const struct retro_game_info *info, size_t num_info
)
{
   return false;
}

void retro_unload_game(void)
{
   FCEUI_CloseGame();
}

unsigned retro_get_region(void)
{
   return FSettings.PAL ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
   if (id != RETRO_MEMORY_SAVE_RAM)
      return NULL;

   if (iNESCart.battery)
	   return iNESCart.SaveGame[0];
   if (UNIFCart.battery)
      return UNIFCart.SaveGame[0];

   return 0;
}

size_t retro_get_memory_size(unsigned id)
{
   if (id != RETRO_MEMORY_SAVE_RAM)
      return 0;

   if (iNESCart.battery)
      return iNESCart.SaveGameLen[0];
   if (UNIFCart.battery)
      return UNIFCart.SaveGameLen[0];

   return 0;
}
