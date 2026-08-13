#if defined RW_GL3 && defined LIBRW_SDL2

long _dwOperatingSystemVersion;

#ifndef __EMSCRIPTEN__
#include <sys/sysinfo.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/heap.h>

/*
 * One frame's worth of yielding, on the browser's own clock.
 *
 * emscripten_sleep(0) would also return control to the page, but it resumes from a timeout, so
 * the engine's drawing happens outside any animation frame — the WebGL drawing buffer is not
 * preserved between composites, and the result is a running game on a black canvas. Suspending
 * on requestAnimationFrame instead puts every frame inside the compositor's cadence, and pacing
 * comes free with it.
 *
 * EM_ASYNC_JS suspends the whole C++ stack through Asyncify and resumes it when the promise
 * settles, so the engine's blocking game loop is untouched.
 *
 * The worker tick is not a fallback for slow frames, it is the hidden-tab case: browsers stop
 * firing requestAnimationFrame entirely for a background tab, and a loop suspended on one that
 * never fires never resumes. Whichever source fires first wins.
 */
/* Frame counter, exported so the page can tell "the loop is alive" from "the status line still
   says Running". Learned from the Generals port: status lies, a counter does not. */
static volatile int reVCFrames = 0;
extern "C" EMSCRIPTEN_KEEPALIVE int ViceLogicFrame(void) { return reVCFrames; }

EM_ASYNC_JS(void, reVCYieldFrame, (), {
    var g = globalThis;
    if (!g.__reVCTick) {
        // A worker's timers are exempt from the throttling a browser applies to a background
        // tab, where requestAnimationFrame stops firing altogether. Without this the game does
        // not merely slow down when the tab is hidden, it stops between frames for as long as
        // the tab stays hidden.
        var src = "setInterval(function(){ postMessage(0); }, 16);";
        var w = new Worker(URL.createObjectURL(new Blob([src], { type: "text/javascript" })));
        g.__reVCWaiters = [];
        w.onmessage = function() {
            var waiting = g.__reVCWaiters;
            g.__reVCWaiters = [];
            for (var i = 0; i < waiting.length; i++) waiting[i]();
        };
        g.__reVCTick = w;
    }
    await new Promise(function(resolve) {
        var done = false;
        var finish = function() { if (!done) { done = true; resolve(); } };
        requestAnimationFrame(finish);   // visible: the compositor sets the pace
        g.__reVCWaiters.push(finish);    // hidden: the worker keeps it moving
    });
});
#endif
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <unordered_map>

#include "common.h"
#include "Timecycle.h"
#include "Clock.h"
#include "rwcore.h"
#include "skeleton.h"
#include "platform.h"
#include "crossplatform.h"

#include "main.h"
#include "FileMgr.h"
#include "Text.h"
#include "Pad.h"
#include "Timer.h"
#include "DMAudio.h"
#include "ControllerConfig.h"
#include "Frontend.h"
#include "Game.h"
#include "PCSave.h"
#include "MemoryCard.h"
#include "Sprite2d.h"
#include "AnimViewer.h"
#include "Font.h"
#include "MemoryMgr.h"

#if defined ANDROID
#include "JavaWrapper.h"
extern char* StorageRootBuffer;
#endif

#define MAX_SUBSYSTEMS		(16)

rw::EngineOpenParams openParams;

static RwBool		  ForegroundApp = TRUE;
static RwBool		  WindowIconified = FALSE;
static RwBool		  WindowFocused = TRUE;

static RwBool		  RwInitialised = FALSE;

static RwSubSystemInfo GsubSysInfo[MAX_SUBSYSTEMS];
static RwInt32		GnumSubSystems = 0;
static RwInt32		GcurSel = 0, GcurSelVM = 0;

static RwBool useDefault;

// What is that for anyway?
#ifndef IMPROVED_VIDEOMODE
static RwBool defaultFullscreenRes = TRUE;
#else
static RwBool defaultFullscreenRes = FALSE;
static RwInt32 bestWndMode = -1;
#endif

static psGlobalType PsGlobal;

static SDL_GameController* gamepad1 = nullptr;
static SDL_GameController* gamepad2 = nullptr;

#define PSGLOBAL(var) (((psGlobalType *)(RsGlobal.ps))->var)

size_t _dwMemAvailPhys;
RwUInt32 gGameState;

#ifdef DETECT_JOYSTICK_MENU
char gSelectedJoystickName[128] = "";
#endif

/*
 *****************************************************************************
 */
void _psCreateFolder(const char *path)
{
#if defined(ANDROID)
	const char* pathroot = StorageRootBuffer;
	char dbPath[1024];
	snprintf(dbPath, sizeof(dbPath), "%s%s", pathroot, path);
	mkdir(dbPath, 0755);
	debug("Creating Folder Path: %s", dbPath);
#else
    debug("Creating Folder Path: %s", path);
    struct stat info;
    char fullpath[PATH_MAX];
    realpath(path, fullpath);

    if (lstat(fullpath, &info) != 0) {
        if (errno == ENOENT || (errno != EACCES && !S_ISDIR(info.st_mode))) {
		debug("Creating Folder FullPath: %s", fullpath);
            mkdir(fullpath, 0755);
        }
    }
#endif
}

#ifdef __EMSCRIPTEN__
/*
 * Start in the player's own language the first time, instead of English.
 *
 * A desktop build asks the OS; there is no OS here, and the browser's answer - the languages the
 * player told it they read - is a better one anyway. It is only ever consulted when no settings
 * file exists, so the moment the player picks a language in Options that choice wins forever.
 *
 * A translation is only offered if its GXT is actually in the install: Portuguese is a file the
 * player has to have added themselves, and selecting a language whose text is missing leaves the
 * game showing nothing but key names.
 */
static void viceLanguageFromBrowser(void)
{
    // LoadSettings leaves the working directory in userfiles, and the GXT check below is relative
    // to the install root - without this it never finds a translation and always says English.
    CFileMgr::SetDir("");

    int lang = EM_ASM_INT({
        var tags = (navigator.languages && navigator.languages.length ? navigator.languages
                                                                     : [navigator.language || "en"]);
        var tag = String(tags[0]).toLowerCase();
        if (tag.indexOf("pt") === 0) return 8;   // LANGUAGE_PORTUGUESE
        if (tag.indexOf("fr") === 0) return 1;
        if (tag.indexOf("de") === 0) return 2;
        if (tag.indexOf("it") === 0) return 3;
        if (tag.indexOf("es") === 0) return 4;
        if (tag.indexOf("ru") === 0) return 6;   // LANGUAGE_RUSSIAN
        if (tag.indexOf("ja") === 0) return 7;   // LANGUAGE_JAPANESE
        return 0;                                 // LANGUAGE_AMERICAN
    });

    static const struct { int lang; const char *gxt; } extras[] = {
        { CMenuManager::LANGUAGE_PORTUGUESE, "text/portuguese.gxt" },
        { CMenuManager::LANGUAGE_RUSSIAN,    "text/russian.gxt" },
        { CMenuManager::LANGUAGE_JAPANESE,   "text/japanese.gxt" },
    };
    for (int i = 0; i < ARRAY_SIZE(extras); i++) {
        if (lang != extras[i].lang)
            continue;
        int fd = CFileMgr::OpenFile(extras[i].gxt, "r");
        if (fd)
            CFileMgr::CloseFile(fd);
        else
            lang = CMenuManager::LANGUAGE_AMERICAN;
    }

    if (lang == FrontEndMenuManager.m_PrefsLanguage)
        return;

    debug("Browser language selected: %d\n", lang);
    FrontEndMenuManager.m_PrefsLanguage = lang;
    FrontEndMenuManager.m_bFrontEnd_ReloadObrTxtGxt = true;
    FrontEndMenuManager.InitialiseChangedLanguageSettings();
    FrontEndMenuManager.SaveSettings();
}
#endif

/*
 *****************************************************************************
 */
const char *_psGetUserFilesFolder()
{
    static char szUserFiles[256];
#ifdef __EMSCRIPTEN__
    /*
     * Absolute, because a relative one is resolved against the working directory, and the working
     * directory at save time is wherever the last thing to read a file left it.
     *
     * Every save path in the game is built from this string - SetSaveDirectory bakes it into
     * DefaultPCSaveFileName once, and SaveSlot, PopulateSlotInfo, DeleteSlot, CheckDataNotCorrupt
     * and GetSaveType all open relative to it. On a desktop that is harmless: the whole tree is on
     * disk and a save that lands one directory over is still a file. Here exactly one path is
     * mounted on IndexedDB, and a save written anywhere else is in memory and gone with the tab.
     * The player saw the save succeed and the slot appear, and found it missing or unreadable on
     * the next visit - and _psCreateFolder below had quietly made a second userfiles wherever the
     * write went, which is why it never looked like a failure.
     *
     * Forward slashes throughout: fcaseopen takes either, but _psCreateFolder calls realpath,
     * which takes the backslash the root directory name ends with as part of a filename.
     */
    strncpy(szUserFiles, CFileMgr::GetRootDirName(), sizeof(szUserFiles) - 1);
    szUserFiles[sizeof(szUserFiles) - 1] = '\0';
    for (char *c = szUserFiles; *c; c++)
        if (*c == '\\')
            *c = '/';
    size_t len = strlen(szUserFiles);
    if (len == 0 || szUserFiles[len - 1] != '/')
        strncat(szUserFiles, "/", sizeof(szUserFiles) - len - 1);
    strncat(szUserFiles, "userfiles", sizeof(szUserFiles) - strlen(szUserFiles) - 1);
#else
    strcpy(szUserFiles, "userfiles");
#endif
    _psCreateFolder(szUserFiles);
    return szUserFiles;
}

/*
 *****************************************************************************
 */
RwBool
psCameraBeginUpdate(RwCamera *camera)
{
    if ( !RwCameraBeginUpdate(Scene.camera) )
    {
        ForegroundApp = FALSE;
        RsEventHandler(rsACTIVATE, (void *)FALSE);
        return FALSE;
    }

    return TRUE;
}

/*
 *****************************************************************************
 */
void
psCameraShowRaster(RwCamera *camera)
{
#ifdef LEGACY_MENU_OPTIONS
    if (FrontEndMenuManager.m_PrefsVsync || FrontEndMenuManager.m_bMenuActive)
#else
        if (FrontEndMenuManager.m_PrefsFrameLimiter || FrontEndMenuManager.m_bMenuActive)
#endif
        RwCameraShowRaster(camera, PSGLOBAL(window), rwRASTERFLIPWAITVSYNC);
    else
        RwCameraShowRaster(camera, PSGLOBAL(window), rwRASTERFLIPDONTWAIT);

    return;
}

/*
 *****************************************************************************
 */
RwImage *
psGrabScreen(RwCamera *pCamera)
{
#ifndef LIBRW
    RwRaster *pRaster = RwCameraGetRaster(pCamera);
	if (RwImage *pImage = RwImageCreate(pRaster->width, pRaster->height, 32)) {
		RwImageAllocatePixels(pImage);
		RwImageSetFromRaster(pImage, pRaster);
		return pImage;
	}
#else
    rw::Image *image = RwCameraGetRaster(pCamera)->toImage();
    image->removeMask();
    if(image)
        return image;
#endif
    return nil;
}

/*
 *****************************************************************************
 */
double
psTimer(void)
{
#ifdef __EMSCRIPTEN__
    /*
     * The whole game clock, and it has to come from here.
     *
     * emscripten only implements CLOCK_REALTIME and CLOCK_MONOTONIC. CLOCK_MONOTONIC_RAW is
     * *declared*, so the branch below compiles and is taken, but the call fails and leaves the
     * timespec untouched — the clock then reads the same value forever. Everything the engine
     * times off it stops: the frontend fade never leaves alpha 0, so the menu draws its black
     * backdrop and nothing else, which looks exactly like a broken renderer and is not one.
     */
    return emscripten_get_now();
#else
    struct timespec start;
#if defined(CLOCK_MONOTONIC_RAW)
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
#elif defined(CLOCK_MONOTONIC_FAST)
    clock_gettime(CLOCK_MONOTONIC_FAST, &start);
#else
	clock_gettime(CLOCK_MONOTONIC, &start);
#endif
    return start.tv_sec * 1000.0 + start.tv_nsec/1000000.0;
#endif
}

/*
 *****************************************************************************
 */
void
psMouseSetPos(RwV2d *pos)
{
    SDL_WarpMouseInWindow(PSGLOBAL(window), pos->x, pos->y);
    PSGLOBAL(lastMousePos.x) = (RwInt32)pos->x;
    PSGLOBAL(lastMousePos.y) = (RwInt32)pos->y;
}

/*
 *****************************************************************************
 */
RwMemoryFunctions*
psGetMemoryFunctions(void)
{
#ifdef USE_CUSTOM_ALLOCATOR
    return &memFuncs;
#else
    return nil;
#endif
}

/*
 *****************************************************************************
 */
RwBool
psInstallFileSystem(void)
{
    return (TRUE);
}

/*
 *****************************************************************************
 */
RwBool
psNativeTextureSupport(void)
{
    return true;
}

/*
 *****************************************************************************
 */
#ifdef UNDER_CE
#define CMDSTR	LPWSTR
#else
#define CMDSTR	LPSTR
#endif

/*
 *****************************************************************************
 */

static void _psInitializeVibration() {}
static void _psHandleVibration() {}

/*
 *****************************************************************************
 */
RwBool
psInitialize(void)
{
    PsGlobal.lastMousePos.x = PsGlobal.lastMousePos.y = 0.0f;

    RsGlobal.ps = &PsGlobal;

    PsGlobal.fullScreen = FALSE;
    PsGlobal.cursorIsInWindow = FALSE;
    WindowFocused = TRUE;
    WindowIconified = FALSE;

    PsGlobal.joy1id	= -1;
    PsGlobal.joy2id	= -1;

    CFileMgr::Initialise();

#ifdef PS2_MENU
    CPad::Initialise();
	CPad::GetPad(0)->Mode = 0;

	CGame::frenchGame = false;
	CGame::germanGame = false;
	CGame::nastyGame = true;
	CMenuManager::m_PrefsAllowNastyGame = true;

	// Mandatory for Linux(Unix? Posix?) to set lang. to environment lang.
	setlocale(LC_ALL, "");

	char *systemLang, *keyboardLang;

	systemLang = setlocale (LC_ALL, NULL);
	keyboardLang = setlocale (LC_CTYPE, NULL);

	short lang;
	lang = !strncmp(systemLang, "fr_",3) ? LANG_FRENCH :
					!strncmp(systemLang, "de_",3) ? LANG_GERMAN :
					!strncmp(systemLang, "en_",3) ? LANG_ENGLISH :
					!strncmp(systemLang, "it_",3) ? LANG_ITALIAN :
					!strncmp(systemLang, "es_",3) ? LANG_SPANISH :
					LANG_OTHER;

	if ( lang  == LANG_ITALIAN )
		CMenuManager::m_PrefsLanguage = CMenuManager::LANGUAGE_ITALIAN;
	else if ( lang  == LANG_SPANISH )
		CMenuManager::m_PrefsLanguage = CMenuManager::LANGUAGE_SPANISH;
	else if ( lang  == LANG_GERMAN )
	{
		CGame::germanGame = true;
		CGame::nastyGame = false;
		CMenuManager::m_PrefsAllowNastyGame = false;
		CMenuManager::m_PrefsLanguage = CMenuManager::LANGUAGE_GERMAN;
	}
	else if ( lang  == LANG_FRENCH )
	{
		CGame::frenchGame = true;
		CGame::nastyGame = false;
		CMenuManager::m_PrefsAllowNastyGame = false;
		CMenuManager::m_PrefsLanguage = CMenuManager::LANGUAGE_FRENCH;
	}
	else
		CMenuManager::m_PrefsLanguage = CMenuManager::LANGUAGE_AMERICAN;

	FrontEndMenuManager.InitialiseMenuContentsAfterLoadingGame();

	TheMemoryCard.Init();
#else
    C_PcSave::SetSaveDirectory(_psGetUserFilesFolder());

    InitialiseLanguage();

#endif

    _psInitializeVibration();

    gGameState = GS_START_UP;
    TRACE("gGameState = GS_START_UP");
    _dwOperatingSystemVersion = OS_WINXP; // To fool other classes

#ifndef PS2_MENU
#ifdef __EMSCRIPTEN__
    // Whether the player has ever saved settings decides whether the browser gets to pick the
    // language. reVC.ini is where settings live with LOAD_INI_SETTINGS - gta_vc.set is not written
    // at all in that build, so testing for it said "first run" on every single boot and the
    // browser overrode the player's own choice forever.
    bool viceFirstRun;
    {
        int fd = CFileMgr::OpenFile("userfiles/reVC.ini", "r");
        viceFirstRun = fd == 0;
        if (fd)
            CFileMgr::CloseFile(fd);
    }
#endif
    FrontEndMenuManager.LoadSettings();
#ifdef __EMSCRIPTEN__
    if (viceFirstRun)
        viceLanguageFromBrowser();
#endif
#endif

#if defined(__EMSCRIPTEN__)
    // There is no OS to ask. The heap the module was linked with is the whole budget, and the
    // engine only uses this to size its own pools.
    _dwMemAvailPhys = (long)emscripten_get_heap_size();
    debug("Wasm heap size %lu\n", (unsigned long)emscripten_get_heap_size());
#elif !defined(__APPLE__)
    struct sysinfo systemInfo;
    sysinfo(&systemInfo);
    _dwMemAvailPhys = systemInfo.freeram;
    debug("Physical memory size %u\n", systemInfo.totalram);
    debug("Available physical memory %u\n", systemInfo.freeram);
#else
    uint64_t size = 0;
	uint64_t page_size = 0;
	size_t uint64_len = sizeof(uint64_t);
	size_t ull_len = sizeof(unsigned long long);
	sysctl((int[]){CTL_HW, HW_PAGESIZE}, 2, &page_size, &ull_len, NULL, 0);
	sysctl((int[]){CTL_HW, HW_MEMSIZE}, 2, &size, &uint64_len, NULL, 0);
	vm_statistics_data_t vm_stat;
	mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
	host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&vm_stat, &count);
	_dwMemAvailPhys = (uint64_t)(vm_stat.free_count * page_size);
	debug("Physical memory size %llu\n", _dwMemAvailPhys);
	debug("Available physical memory %llu\n", size);
#endif

    TheText.Unload();

    return TRUE;
}


/*
 *****************************************************************************
 */
void
psTerminate(void)
{
    return;
}

/*
 *****************************************************************************
 */
static RwChar **_VMList;

#include <unistd.h>
#include <malloc.h>

#ifdef __EMSCRIPTEN__
extern "C" void ViceSetResolution(int w, int h);

/*
 * The browser's own resolution list is useless here: librw builds it from SDL's display modes,
 * which describe the user's monitor, not the canvas we render into. Offer a normal set of game
 * resolutions instead and let the page scale the result to fit.
 *
 * Unlike a real mode switch these cost nothing: only the canvas backing store and the camera
 * raster change, the GL context survives, so no RW device reset and no texture reload.
 */
struct ViceVideoMode { RwInt32 w, h; };
static const ViceVideoMode viceVideoModes[] = {
    { 640, 480}, { 800, 600}, {1024, 768}, {1152, 864}, {1280, 960}, {1600,1200},   // 4:3
    {1280, 720}, {1366, 768}, {1600, 900}, {1920,1080}, {2560,1440},                // 16:9
    {1280, 800}, {1440, 900}, {1680,1050},                                          // 16:10
};
static const RwInt32 viceNumVideoModes = sizeof(viceVideoModes) / sizeof(viceVideoModes[0]);
static const RwInt32 viceDefaultVideoMode = 6;   // 1280x720

// The engine persists this in gta_vc.set, which needs a writable userfiles directory we do not
// have. localStorage is the browser's equivalent and survives a reload just as well.
static RwInt32 viceLoadVideoMode(void)
{
    RwInt32 mode = EM_ASM_INT({
        try {
            var v = parseInt(localStorage.getItem("vice.mode"), 10);
            return isNaN(v) ? -1 : v;
        } catch (e) { return -1; }
    });
    if ( mode >= 0 && mode < viceNumVideoModes )
        return mode;

    // Nothing chosen yet: start at whichever default matches the aspect the page asked for.
    const int wantFourThree = EM_ASM_INT({
        try { return localStorage.getItem("vice.aspect") === "4:3" ? 1 : 0; } catch (e) { return 0; }
    });
    return wantFourThree ? 2 /* 1024x768 */ : viceDefaultVideoMode;
}

static void viceSaveVideoMode(RwInt32 mode)
{
    EM_ASM({ try { localStorage.setItem("vice.mode", $0); } catch (e) {} }, mode);
}
#endif

RwInt32 _psGetNumVideModes()
{
#ifdef __EMSCRIPTEN__
    return viceNumVideoModes;
#else
    return RwEngineGetNumVideoModes();
#endif
}

/*
 *****************************************************************************
 */
RwBool _psFreeVideoModeList()
{
    RwInt32 numModes;
    RwInt32 i;

    numModes = _psGetNumVideModes();

    if ( _VMList == nil )
        return TRUE;

    for ( i = 0; i < numModes; i++ )
    {
        RwFree(_VMList[i]);
    }

    RwFree(_VMList);

    _VMList = nil;

    return TRUE;
}

/*
 *****************************************************************************
 */
RwChar **_psGetVideoModeList()
{
    RwInt32 numModes;
    RwInt32 i;

    if ( _VMList != nil )
    {
        return _VMList;
    }

    numModes = _psGetNumVideModes();

    _VMList = (RwChar **)RwCalloc(numModes, sizeof(RwChar*));

#ifdef __EMSCRIPTEN__
    // Every entry gets a name: the menu walks the list skipping nils, so a nil is how a mode is
    // hidden. Upstream hides all the non-exclusive ones, which in the browser is all of them.
    for ( i = 0; i < numModes; i++ )
    {
        _VMList[i] = (RwChar*)RwCalloc(100, sizeof(RwChar));
        rwsprintf(_VMList[i], "%d X %d", viceVideoModes[i].w, viceVideoModes[i].h);
    }

    return _VMList;
#endif

    for ( i = 0; i < numModes; i++	)
    {
        RwVideoMode			vm;

        RwEngineGetVideoModeInfo(&vm, i);

        if ( vm.flags & rwVIDEOMODEEXCLUSIVE )
        {
            _VMList[i] = (RwChar*)RwCalloc(100, sizeof(RwChar));
            rwsprintf(_VMList[i],"%d X %d X %d", vm.width, vm.height, vm.depth);
        }
        else
            _VMList[i] = nil;
    }

    return _VMList;
}

/*
 *****************************************************************************
 */
void _psSelectScreenVM(RwInt32 videoMode)
{
#ifdef __EMSCRIPTEN__
    if ( videoMode < 0 || videoMode >= viceNumVideoModes )
        return;

    // No _psSetVideoMode: that terminates and re-initialises the RW device, which here means
    // dropping the WebGL context and every texture on it. Resizing the canvas is enough.
    ViceSetResolution(viceVideoModes[videoMode].w, viceVideoModes[videoMode].h);
    viceSaveVideoMode(videoMode);
    return;
#else
    RwTexDictionarySetCurrent( nil );

    FrontEndMenuManager.UnloadTextures();

    if (!_psSetVideoMode(RwEngineGetCurrentSubSystem(), videoMode))
    {
        RsGlobal.quit = TRUE;
        printf("ERROR: Failed to select new screen resolution\n");
    }
    else
        FrontEndMenuManager.LoadAllTextures();
#endif
}

/*
 *****************************************************************************
 */

RwBool IsForegroundApp()
{
    return !!ForegroundApp;
}
/*
 *****************************************************************************
 */
RwBool
psSelectDevice()
{
    RwVideoMode			vm;
    RwInt32				subSysNum;
    RwInt32				AutoRenderer = 0;

    RwBool modeFound = FALSE;

    if (!useDefault)
    {
        GnumSubSystems = RwEngineGetNumSubSystems();
        if (!GnumSubSystems)
        {
            return FALSE;
        }

        /* Just to be sure ... */
        GnumSubSystems = (GnumSubSystems > MAX_SUBSYSTEMS) ? MAX_SUBSYSTEMS : GnumSubSystems;

        /* Get the names of all the sub systems */
        for (subSysNum = 0; subSysNum < GnumSubSystems; subSysNum++)
        {
            RwEngineGetSubSystemInfo(&GsubSysInfo[subSysNum], subSysNum);
        }

        /* Get the default selection */
        GcurSel = RwEngineGetCurrentSubSystem();
#ifdef IMPROVED_VIDEOMODE
        if (FrontEndMenuManager.m_nPrefsSubsystem < GnumSubSystems)
            GcurSel = FrontEndMenuManager.m_nPrefsSubsystem;
#endif
    }

    /* Set the driver to use the correct sub system */
    if (!RwEngineSetSubSystem(GcurSel))
    {
        return FALSE;
    }

#ifdef IMPROVED_VIDEOMODE
    FrontEndMenuManager.m_nPrefsSubsystem = GcurSel;
#endif

#ifndef IMPROVED_VIDEOMODE
    if (!useDefault)
	{
		if (_psGetVideoModeList()[FrontEndMenuManager.m_nDisplayVideoMode] && FrontEndMenuManager.m_nDisplayVideoMode)
		{
			FrontEndMenuManager.m_nPrefsVideoMode = FrontEndMenuManager.m_nDisplayVideoMode;
			GcurSelVM = FrontEndMenuManager.m_nDisplayVideoMode;
		}
		else
		{
#ifdef DEFAULT_NATIVE_RESOLUTION
			// get the native video mode
			HDC hDevice = GetDC(NULL);
			int w = GetDeviceCaps(hDevice, HORZRES);
			int h = GetDeviceCaps(hDevice, VERTRES);
			int d = GetDeviceCaps(hDevice, BITSPIXEL);
#else
			const int w = 640;
			const int h = 480;
			const int d = 16;
#endif
			while ( !modeFound && GcurSelVM < RwEngineGetNumVideoModes() )
			{
				RwEngineGetVideoModeInfo(&vm, GcurSelVM);
				if ( defaultFullscreenRes	&& vm.width	 != w
											|| vm.height != h
											|| vm.depth	 != d
											|| !(vm.flags & rwVIDEOMODEEXCLUSIVE) )
					++GcurSelVM;
				else
					modeFound = TRUE;
			}

			if ( !modeFound )
			{
#ifdef DEFAULT_NATIVE_RESOLUTION
				GcurSelVM = 1;
#else
				printf("WARNING: Cannot find 640x480 video mode, selecting device cancelled\n");
				return FALSE;
#endif
			}
		}
	}
#else
    if (!useDefault)
    {
        if(FrontEndMenuManager.m_nPrefsWidth == 0 ||
           FrontEndMenuManager.m_nPrefsHeight == 0 ||
           FrontEndMenuManager.m_nPrefsDepth == 0){
            // Defaults if nothing specified
            SDL_DisplayMode mode;
            // TODO how to get displayIndex for the current display?
            SDL_GetCurrentDisplayMode(0, &mode);
            FrontEndMenuManager.m_nPrefsWidth = mode.w;
            FrontEndMenuManager.m_nPrefsHeight = mode.h;
            FrontEndMenuManager.m_nPrefsDepth = 32;
            FrontEndMenuManager.m_nPrefsWindowed = 0;
        }

        // Find the videomode that best fits what we got from the settings file
        RwInt32 bestFsMode = -1;
        RwInt32 bestWidth = -1;
        RwInt32 bestHeight = -1;
        RwInt32 bestDepth = -1;
        for(GcurSelVM = 0; GcurSelVM < RwEngineGetNumVideoModes(); GcurSelVM++){
            RwEngineGetVideoModeInfo(&vm, GcurSelVM);

            if (!(vm.flags & rwVIDEOMODEEXCLUSIVE)){
                bestWndMode = GcurSelVM;
            } else {
                // try the largest one that isn't larger than what we wanted
                if(vm.width >= bestWidth && vm.width <= FrontEndMenuManager.m_nPrefsWidth &&
                   vm.height >= bestHeight && vm.height <= FrontEndMenuManager.m_nPrefsHeight &&
                   vm.depth >= bestDepth && vm.depth <= FrontEndMenuManager.m_nPrefsDepth){
                    bestWidth = vm.width;
                    bestHeight = vm.height;
                    bestDepth = vm.depth;
                    bestFsMode = GcurSelVM;
                }
            }
        }

        if(bestFsMode < 0){
            printf("WARNING: Cannot find desired video mode, selecting device cancelled\n");
            return FALSE;
        }
        GcurSelVM = bestFsMode;

        FrontEndMenuManager.m_nDisplayVideoMode = GcurSelVM;
        FrontEndMenuManager.m_nPrefsVideoMode = FrontEndMenuManager.m_nDisplayVideoMode;

        FrontEndMenuManager.m_nSelectedScreenMode = FrontEndMenuManager.m_nPrefsWindowed;
    }
#endif

    RwEngineGetVideoModeInfo(&vm, GcurSelVM);

#ifdef IMPROVED_VIDEOMODE
    if (FrontEndMenuManager.m_nPrefsWindowed)
        GcurSelVM = bestWndMode;

    // Now GcurSelVM is 0 but vm has sizes(and fullscreen flag) of the video mode we want, that's why we changed the rwVIDEOMODEEXCLUSIVE conditions below
    FrontEndMenuManager.m_nPrefsWidth = vm.width;
    FrontEndMenuManager.m_nPrefsHeight = vm.height;
    FrontEndMenuManager.m_nPrefsDepth = vm.depth;
#endif

#ifndef PS2_MENU
    FrontEndMenuManager.m_nCurrOption = 0;
#endif

#ifdef __EMSCRIPTEN__
    /*
     * A canvas is never a fullscreen video mode. Mode 0 is the only one librw leaves without
     * rwVIDEOMODEEXCLUSIVE; every other entry comes from the display list and is marked exclusive.
     *
     * The selection loop above runs before this port pins the resolution below, so it still saw
     * m_nPrefsWindowed == 0 and chose bestFsMode — an exclusive mode carrying the *desktop* size.
     * That leaks far past this function: CameraSize(camera, nil, ...) has a dedicated branch for
     * exclusive modes that ignores the current raster and resizes the camera to the video mode.
     * reVC calls it that way from four places, so the camera kept snapping back to 2560x1440
     * while the canvas stayed 1280x720 — the world rendered into a raster twice the framebuffer
     * and only its bottom-left quarter was ever presented.
     */
    GcurSelVM = 0;
#endif

    /* Set up the video mode and set the apps window
    * dimensions to match */
    if (!RwEngineSetVideoMode(GcurSelVM))
    {
        return FALSE;
    }
    /*
    TODO
    if (vm.flags & rwVIDEOMODEEXCLUSIVE)
    {
        debug("%dx%dx%d", vm.width, vm.height, vm.depth);

        UINT refresh = GetBestRefreshRate(vm.width, vm.height, vm.depth);

        if ( refresh != (UINT)-1 )
        {
            debug("refresh %d", refresh);
            RwD3D8EngineSetRefreshRate((RwUInt32)refresh);
        }
    }
    */
#ifndef IMPROVED_VIDEOMODE
    if (vm.flags & rwVIDEOMODEEXCLUSIVE)
	{
		RsGlobal.maximumWidth = vm.width;
		RsGlobal.maximumHeight = vm.height;
		RsGlobal.width = vm.width;
		RsGlobal.height = vm.height;

		PSGLOBAL(fullScreen) = TRUE;
	}
#else
    RsGlobal.maximumWidth = FrontEndMenuManager.m_nPrefsWidth;
    RsGlobal.maximumHeight = FrontEndMenuManager.m_nPrefsHeight;
    RsGlobal.width = FrontEndMenuManager.m_nPrefsWidth;
    RsGlobal.height = FrontEndMenuManager.m_nPrefsHeight;

    PSGLOBAL(fullScreen) = !FrontEndMenuManager.m_nPrefsWindowed;
#endif

#ifdef __EMSCRIPTEN__
    /*
     * Pin one resolution for the browser, and make it windowed.
     *
     * Two things conspire otherwise. openParams is filled from RsGlobal.maximum* *before* this
     * function runs, so librw opens the canvas at the startup default; then the saved preference
     * read here comes back as the whole desktop, because a browser advertises the screen as a
     * video mode. The engine then laid the frontend out in a 1512x982 space inside a 640x480
     * framebuffer — entirely off-screen, which left only the cursor visible.
     *
     * psPostRWinit sizes the canvas to match. The page scales it to fit, so this is the render
     * resolution, not the display size.
     */
    // Start at whatever the Display menu last chose. Options -> Display can change it freely
    // afterwards; _psSelectScreenVM applies it without a device reset.
    const RwInt32 startMode = viceLoadVideoMode();
    const int chosenW = viceVideoModes[startMode].w;
    const int chosenH = viceVideoModes[startMode].h;

    RsGlobal.maximumWidth = RsGlobal.width = chosenW;
    RsGlobal.maximumHeight = RsGlobal.height = chosenH;
    FrontEndMenuManager.m_nPrefsWidth = chosenW;
    FrontEndMenuManager.m_nPrefsHeight = chosenH;
    FrontEndMenuManager.m_nPrefsWindowed = 1;
    FrontEndMenuManager.m_nPrefsVideoMode = FrontEndMenuManager.m_nDisplayVideoMode = startMode;
    PSGLOBAL(fullScreen) = FALSE;
#endif

#ifdef MULTISAMPLING
    RwD3D8EngineSetMultiSamplingLevels(1 << FrontEndMenuManager.m_nPrefsMSAALevel);
#endif
    return TRUE;
}

bool IsThisJoystickBlacklisted(int i)
{
#ifndef DETECT_JOYSTICK_MENU
    return false;
#else
    if (SDL_IsGameController(i))
		return false;

	const char* joyname = SDL_JoystickNameForIndex(i);

	if (gSelectedJoystickName[0] != '\0'
			&& strncmp(joyname, gSelectedJoystickName, strlen(gSelectedJoystickName)) == 0) {
		return false;
	}

	return true;
#endif
}

void _InputInitialiseJoys()
{
    PSGLOBAL(joy1id) = -1;
    PSGLOBAL(joy2id) = -1;

    // librw only brings up SDL_INIT_VIDEO, and without the gamecontroller subsystem SDL emits no
    // joystick events at all - so joysChangeCB never ran and gamepad1 stayed null however many
    // pads were plugged in. In the browser this is what connects SDL to navigator.getGamepads().
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        debug("Could not start the gamepad subsystem: %s", SDL_GetError());

    // Load our gamepad mappings
    const char* EnvControlConfig = getenv("SDL_GAMECONTROLLERCONFIG_FILE");

    if (EnvControlConfig != nil) {
        if (SDL_GameControllerAddMappingsFromFile(EnvControlConfig) <= 0) {
            Error("Could not load custom controller mapping (SDL_GAMECONTROLLERCONFIG_FILE env variable\n)");
        }
    } else {
#if defined ANDROID
        const char* pathRoot = getenv("STORAGE_ROOT");
        char SDL_GAMEPAD_DB_PATH[MAX_PATH];
        snprintf(SDL_GAMEPAD_DB_PATH, sizeof(SDL_GAMEPAD_DB_PATH), "%s%s", pathRoot, "gamecontrollerdb.txt");
#else
        const char* SDL_GAMEPAD_DB_PATH = "gamecontrollerdb.txt";
#endif
        if (SDL_GameControllerAddMappingsFromFile(SDL_GAMEPAD_DB_PATH) <= 0) {
            debug ("You don't seem to have copied %s file from reVC/gamefiles "
                   "to GTA: Vice City directory. Some gamepads may not be recognized.\n",
                   SDL_GAMEPAD_DB_PATH);
        }
#ifdef __EMSCRIPTEN__
        // Browsers report every pad through one standard mapping, so anything the db misses is
        // still usable - SDL's own default covers it.
        else
            debug("Loaded gamepad mappings from %s", SDL_GAMEPAD_DB_PATH);
#endif
    }

    // TODO SDL2 the part below seems unnecessary SDL2 (at least on Linux), remove in the future
    /*for (int i = 0; i <= SDL_NumJoysticks(); i++) {
        if (!IsThisJoystickBlacklisted(i)) {
            if (PSGLOBAL(joy1id) == -1)
                PSGLOBAL(joy1id) = i;
            else if (PSGLOBAL(joy2id) == -1)
                PSGLOBAL(joy2id) = i;
            else
                break;
        }
    }

    if (PSGLOBAL(joy1id) != -1) {
        SDL_Joystick* joy1 = SDL_JoystickOpen(PSGLOBAL(joy1id));
        int count = SDL_JoystickNumButtons(joy1);
        SDL_JoystickClose(joy1);
#ifdef DETECT_JOYSTICK_MENU
        strncpy(gSelectedJoystickName, SDL_JoystickNameForIndex(PSGLOBAL(joy1id)), sizeof(gSelectedJoystickName));
#endif
        ControlsManager.InitDefaultControlConfigJoyPad(count);
    }*/
}

#if 0
TODO SDL2
-long _InputInitialiseMouse()
+int lastCursorMode = GLFW_CURSOR_HIDDEN;
+long _InputInitialiseMouse(bool exclusive)
 {
-	glfwSetInputMode(PSGLOBAL(window), GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
+	// Disabled = keep cursor centered and hide
+	lastCursorMode = exclusive ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_HIDDEN;
+	glfwSetInputMode(PSGLOBAL(window), GLFW_CURSOR, lastCursorMode);
 	return 0;
 }
#endif

long _InputInitialiseMouse(bool exclusive)
{
    // TODO SDL2 what to do about exclusive?
    SDL_ShowCursor(SDL_DISABLE);
    return 0;
}

void _InputShutdownMouse()
{
    // Not needed
}

bool _InputMouseNeedsExclusive()
{
#if 0
    TODO SDL2
	// That was the cause of infamous mouse bug on Win.
	RwVideoMode vm;
	RwEngineGetVideoModeInfo(&vm, GcurSelVM);

	// If windowed, free the cursor on menu(where this func. is called and DISABLED-HIDDEN transition is done accordingly)
	// If it's fullscreen, be sure that it didn't stuck on HIDDEN.
	return !(vm.flags & rwVIDEOMODEEXCLUSIVE) || lastCursorMode == GLFW_CURSOR_HIDDEN;
#endif
    return false;
}

void resizeCB(int width, int height);

void psPostRWinit(void)
{
    RwVideoMode vm;
    RwEngineGetVideoModeInfo(&vm, GcurSelVM);

    _InputInitialiseJoys();
    _InputInitialiseMouse(false);

#ifdef __EMSCRIPTEN__
    // A canvas is never an exclusive video mode, and it must always match the render size.
    SDL_SetWindowSize(PSGLOBAL(window), RsGlobal.maximumWidth, RsGlobal.maximumHeight);
    // …and resize the camera by hand. Changing the canvas size raises no SDL resize event in the
    // browser, so rsCAMERASIZE never fired: the world kept rendering into the 640x480 raster
    // RwEngineOpen was handed at startup while the HUD drew at the real resolution — a small
    // picture in the corner of a large canvas.
    //
    // Not via resizeCB: that guards on RwInitialised, which the main loop only sets *after* this
    // runs, so it would quietly do nothing.
    {
        RwRect r;
        r.x = 0;
        r.y = 0;
        r.w = RsGlobal.maximumWidth;
        r.h = RsGlobal.maximumHeight;
        RsGlobal.width = RsGlobal.maximumWidth;
        RsGlobal.height = RsGlobal.maximumHeight;
        RsEventHandler(rsCAMERASIZE, &r);
    }
#else
    if(!(vm.flags & rwVIDEOMODEEXCLUSIVE))
        SDL_SetWindowSize(PSGLOBAL(window), RsGlobal.maximumWidth, RsGlobal.maximumHeight);
#endif

    // Make sure all keys are released
    CPad::GetPad(0)->Clear(true);
    CPad::GetPad(1)->Clear(true);
}

/*
 *****************************************************************************
 */
RwBool _psSetVideoMode(RwInt32 subSystem, RwInt32 videoMode)
{
    RwInitialised = FALSE;

    RsEventHandler(rsRWTERMINATE, nil);

    GcurSel = subSystem;
    GcurSelVM = videoMode;

    useDefault = TRUE;

    if (RsEventHandler(rsRWINITIALIZE, &openParams) == rsEVENTERROR)
        return FALSE;

    RwInitialised = TRUE;
    useDefault = FALSE;

    RwRect r;

    r.x = 0;
    r.y = 0;
    r.w = RsGlobal.maximumWidth;
    r.h = RsGlobal.maximumHeight;

    RsEventHandler(rsCAMERASIZE, &r);

    psPostRWinit();

    return TRUE;
}


/*
 *****************************************************************************
 */
static RwChar **
CommandLineToArgv(RwChar *cmdLine, RwInt32 *argCount)
{
    RwInt32 numArgs = 0;
    RwBool inArg, inString;
    RwInt32 i, len;
    RwChar *res, *str, **aptr;

    len = strlen(cmdLine);

    /*
     * Count the number of arguments...
     */
    inString = FALSE;
    inArg = FALSE;

    for(i=0; i<=len; i++)
    {
        if( cmdLine[i] == '"' )
        {
            inString = !inString;
        }

        if( (cmdLine[i] <= ' ' && !inString) || i == len )
        {
            if (inArg)
            {
                inArg = FALSE;

                numArgs++;
            }
        }
        else if( !inArg )
        {
            inArg = TRUE;
        }
    }

    /*
     * Allocate memory for result...
     */
    res = (RwChar *)malloc(sizeof(RwChar *) * numArgs + len + 1);
    str = res + sizeof(RwChar *) * numArgs;
    aptr = (RwChar **)res;

    strcpy(str, cmdLine);

    /*
     * Walk through cmdLine again this time setting pointer to each arg...
     */
    inArg = FALSE;
    inString = FALSE;

    for(i=0; i<=len; i++)
    {
        if( cmdLine[i] == '"' )
        {
            inString = !inString;
        }

        if( (cmdLine[i] <= ' ' && !inString) || i == len )
        {
            if (inArg)
            {
                if( str[i-1] == '"' )
                {
                    str[i-1] = '\0';
                }
                else
                {
                    str[i] = '\0';
                }

                inArg = FALSE;
            }
        }
        else if (!inArg && cmdLine[i] != '"')
        {
            inArg = TRUE;

            *aptr++ = &str[i];
        }
    }

    *argCount = numArgs;

    return (RwChar **)res;
}

/*
 *****************************************************************************
 */
void InitialiseLanguage()
{
    // Mandatory for Linux(Unix? Posix?) to set lang. to environment lang.
    setlocale(LC_ALL, "");

    char *systemLang, *keyboardLang;

    systemLang = setlocale (LC_ALL, NULL);
    keyboardLang = setlocale (LC_CTYPE, NULL);

    short primUserLCID, primSystemLCID;
    primUserLCID = primSystemLCID = !strncmp(systemLang, "fr_",3) ? LANG_FRENCH :
                                    !strncmp(systemLang, "de_",3) ? LANG_GERMAN :
                                    !strncmp(systemLang, "en_",3) ? LANG_ENGLISH :
                                    !strncmp(systemLang, "it_",3) ? LANG_ITALIAN :
                                    !strncmp(systemLang, "es_",3) ? LANG_SPANISH :
                                    LANG_OTHER;

    short primLayout = !strncmp(keyboardLang, "fr_",3) ? LANG_FRENCH : (!strncmp(keyboardLang, "de_",3) ? LANG_GERMAN : LANG_ENGLISH);

    short subUserLCID, subSystemLCID;
    subUserLCID = subSystemLCID = !strncmp(systemLang, "en_AU",5) ? SUBLANG_ENGLISH_AUS : SUBLANG_OTHER;
    short subLayout = !strncmp(keyboardLang, "en_AU",5) ? SUBLANG_ENGLISH_AUS : SUBLANG_OTHER;

    if (   primUserLCID	  == LANG_GERMAN
           || primSystemLCID == LANG_GERMAN
           || primLayout	  == LANG_GERMAN )
    {
        CGame::nastyGame = false;
        FrontEndMenuManager.m_PrefsAllowNastyGame = false;
        CGame::germanGame = true;
    }

    if (   primUserLCID	  == LANG_FRENCH
           || primSystemLCID == LANG_FRENCH
           || primLayout	  == LANG_FRENCH )
    {
        CGame::nastyGame = false;
        FrontEndMenuManager.m_PrefsAllowNastyGame = false;
        CGame::frenchGame = true;
    }

    if (   subUserLCID	 == SUBLANG_ENGLISH_AUS
           || subSystemLCID == SUBLANG_ENGLISH_AUS
           || subLayout	 == SUBLANG_ENGLISH_AUS )
        CGame::noProstitutes = true;

#ifdef NASTY_GAME
    CGame::nastyGame = true;
    FrontEndMenuManager.m_PrefsAllowNastyGame = true;
    CGame::noProstitutes = false;
#endif

    int32 lang;

    switch ( primSystemLCID )
    {
        case LANG_GERMAN:
        {
            lang = LANG_GERMAN;
            break;
        }
        case LANG_FRENCH:
        {
            lang = LANG_FRENCH;
            break;
        }
        case LANG_SPANISH:
        {
            lang = LANG_SPANISH;
            break;
        }
        case LANG_ITALIAN:
        {
            lang = LANG_ITALIAN;
            break;
        }
        default:
        {
            lang = ( subSystemLCID == SUBLANG_ENGLISH_AUS ) ? -99 : LANG_ENGLISH;
            break;
        }
    }

    FrontEndMenuManager.OS_Language = primUserLCID;

    switch ( lang )
    {
        case LANG_GERMAN:
        {
            FrontEndMenuManager.m_PrefsLanguage = CMenuManager::LANGUAGE_GERMAN;
            break;
        }
        case LANG_SPANISH:
        {
            FrontEndMenuManager.m_PrefsLanguage = CMenuManager::LANGUAGE_SPANISH;
            break;
        }
        case LANG_FRENCH:
        {
            FrontEndMenuManager.m_PrefsLanguage = CMenuManager::LANGUAGE_FRENCH;
            break;
        }
        case LANG_ITALIAN:
        {
            FrontEndMenuManager.m_PrefsLanguage = CMenuManager::LANGUAGE_ITALIAN;
            break;
        }
        default:
        {
            FrontEndMenuManager.m_PrefsLanguage = CMenuManager::LANGUAGE_AMERICAN;
            break;
        }
    }

    // TODO this is needed for strcasecmp to work correctly across all languages, but can these cause other problems??
    setlocale(LC_CTYPE, "C");
    setlocale(LC_COLLATE, "C");
    setlocale(LC_NUMERIC, "C");

    TheText.Unload();
    TheText.Load();
}

/*
 *****************************************************************************
 */

void HandleExit()
{
    // We now handle terminate message always, why handle on some cases?
    return;
}

void terminateHandler(int sig, siginfo_t *info, void *ucontext) {
#if defined(ANDROID)
    if(g_pJavaWrapper)
        g_pJavaWrapper->ExitGame();
#endif
    RsGlobal.quit = TRUE;
}

#ifdef FLUSHABLE_STREAMING
void dummyHandler(int sig){
    // Don't kill the app pls
}
#endif

void resizeCB(int width, int height) {
    /*
    * Handle event to ensure window contents are displayed during re-size
    * as this can be disabled by the user, then if there is not enough
    * memory things don't work.
    */
    /* redraw window */

#ifndef __EMSCRIPTEN__
    if (RwInitialised && gGameState == GS_PLAYING_GAME)
    {
        RsEventHandler(rsIDLE, (void *)TRUE);
    }
#else
    // No frame from here. This runs inside a DOM event - the shell calls ViceSetResolution when
    // fullscreen is entered or left - and rsIDLE is a whole game frame, so it re-enters the game
    // loop while the module is suspended mid-frame on its Asyncify yield. That leaves two
    // unwinds interleaved and the game never comes back: it froze on leaving fullscreen. The
    // reason it exists at all is to keep a window painted during a drag-resize, which a canvas
    // does not have; the loop paints the new size on its next frame anyway.
#endif

    if (RwInitialised && height > 0 && width > 0) {
        RwRect r;

        // TODO fix artifacts of resizing with mouse
        RsGlobal.maximumHeight = height;
        RsGlobal.maximumWidth = width;
        // width/height is the 3D render size; maximum* is the space the HUD and menus lay out
        // in. Leaving these behind renders the world at one resolution and the HUD at another.
        RsGlobal.height = height;
        RsGlobal.width = width;

        r.x = 0;
        r.y = 0;
        r.w = width;
        r.h = height;

        RsEventHandler(rsCAMERASIZE, &r);
    }
}

void scrollCB(double xoffset, double yoffset) {
    PSGLOBAL(mouseWheel) = yoffset;
}

bool lshiftStatus = false;
bool rshiftStatus = false;

static const std::unordered_map<int, int> keymap = {
        {SDLK_SPACE,		' '},
        {SDLK_QUOTE, 		'\''},
        {SDLK_COMMA, 		','},
        {SDLK_MINUS, 		'-'},
        {SDLK_PERIOD,		'.'},
        {SDLK_SLASH,		'/'},
        {SDLK_0,			'0'},
        {SDLK_1,			'1'},
        {SDLK_2,			'2'},
        {SDLK_3,			'3'},
        {SDLK_4,			'4'},
        {SDLK_5,			'5'},
        {SDLK_6,			'6'},
        {SDLK_7,			'7'},
        {SDLK_8,			'8'},
        {SDLK_9,			'9'},
        {SDLK_SEMICOLON,	';'},
        {SDLK_EQUALS,		'='},
        {SDLK_LEFTBRACKET,	'['},
        {SDLK_BACKSLASH,	'\\'},
        {SDLK_RIGHTBRACKET,	']'},
        {SDLK_BACKQUOTE,	'`'},
        {SDLK_ESCAPE,		rsESC},
        {SDLK_RETURN,		rsENTER},
        {SDLK_TAB,			rsTAB},
        {SDLK_BACKSPACE,	rsBACKSP},
        {SDLK_INSERT,		rsINS},
        {SDLK_DELETE,		rsDEL},
        {SDLK_RIGHT,		rsRIGHT},
        {SDLK_LEFT,			rsLEFT},
        {SDLK_DOWN,			rsDOWN},
        {SDLK_UP,			rsUP},
        {SDLK_PAGEUP,		rsPGUP},
        {SDLK_PAGEDOWN,		rsPGDN},
        {SDLK_HOME,			rsHOME},
        {SDLK_END,			rsEND},
        {SDLK_CAPSLOCK,		rsCAPSLK},
        {SDLK_SCROLLLOCK,	rsSCROLL},
        //{SDLK_PRINTSCREEN,	rsNULL},
        {SDLK_PAUSE,		rsPAUSE},
        {SDLK_F1,			rsF1},
        {SDLK_F2,			rsF2},
        {SDLK_F3,			rsF3},
        {SDLK_F4,			rsF4},
        {SDLK_F5,			rsF5},
        {SDLK_F6,			rsF6},
        {SDLK_F7,			rsF7},
        {SDLK_F8,			rsF8},
        {SDLK_F9,			rsF9},
        {SDLK_F10,			rsF10},
        {SDLK_F11,			rsF11},
        {SDLK_F12,			rsF12},
        //{SDLK_F13,			rsNULL},
        //{SDLK_F14,			rsNULL},
        //{SDLK_F15,			rsNULL},
        //{SDLK_F16,			rsNULL},
        //{SDLK_F17,			rsNULL},
        //{SDLK_F18,			rsNULL},
        //{SDLK_F19,			rsNULL},
        //{SDLK_F20,			rsNULL},
        //{SDLK_F21,			rsNULL},
        //{SDLK_F22,			rsNULL},
        //{SDLK_F23,			rsNULL},
        //{SDLK_F24,			rsNULL},
        //{SDLK_F25,			rsNULL},
        {SDLK_KP_0,			rsPADINS},
        {SDLK_KP_1,			rsPADEND},
        {SDLK_KP_2,			rsPADDOWN},
        {SDLK_KP_3,			rsPADPGDN},
        {SDLK_KP_4,			rsPADLEFT},
        {SDLK_KP_5,			rsPAD5},
        {SDLK_KP_6,			rsPADRIGHT},
        {SDLK_KP_7,			rsPADHOME},
        {SDLK_KP_8,			rsPADUP},
        {SDLK_KP_9,			rsPADPGUP},
        {SDLK_KP_DECIMAL,	rsPADDEL},
        {SDLK_KP_DIVIDE,	rsDIVIDE},
        {SDLK_KP_MULTIPLY,	rsTIMES},
        {SDLK_KP_MINUS,		rsMINUS},
        {SDLK_KP_PLUS,		rsPLUS},
        {SDLK_KP_ENTER,		rsPADENTER},
        //{SDLK_KP_EQUAL,		rsNULL},
        {SDLK_LSHIFT,		rsLSHIFT},
        {SDLK_LCTRL,		rsLCTRL},
        {SDLK_LALT,			rsLALT},
        {SDLK_LGUI,			rsLWIN},
        {SDLK_RSHIFT,		rsRSHIFT},
        {SDLK_RCTRL,		rsRCTRL},
        {SDLK_RALT,			rsRALT},
        {SDLK_RGUI,			rsRWIN},
        //{SDLK_MENU,			rsNULL}
};

void
keypressCB(int key, int action, int mods)
{
    RsKeyCodes ks = rsNULL;

    if (key <= 0)
        return;

    // for a mysterious reason, isalpha() crashes for e.g. SDLK_VOLUMEUP/DOWN
    if (key >= 'A' && key <= 'Z') {
        ks = (RsKeyCodes) key;
    } else if (key >= 'a' && key <= 'z') {
        ks = (RsKeyCodes) toupper(key);
    } else {
        auto it = keymap.find(key);

        if (it != keymap.end()) {
            ks = (RsKeyCodes) it->second;
        }
    }

    if (key == SDLK_LSHIFT)
        lshiftStatus = (action != SDL_KEYUP);

    if (key == SDLK_RSHIFT)
        rshiftStatus = (action != SDL_KEYUP);

    if (ks == rsNULL)
        return;

    switch (action) {
        case SDL_KEYDOWN:	RsKeyboardEventHandler(rsKEYDOWN, &ks); break;
        case SDL_KEYUP:		RsKeyboardEventHandler(rsKEYUP, &ks); break;
    }
}

// R* calls that in ControllerConfig, idk why
void
_InputTranslateShiftKeyUpDown(RsKeyCodes *rs) {
    RsKeyboardEventHandler(lshiftStatus ? rsKEYDOWN : rsKEYUP, &(*rs = rsLSHIFT));
    RsKeyboardEventHandler(rshiftStatus ? rsKEYDOWN : rsKEYUP, &(*rs = rsRSHIFT));
}

#ifdef __EMSCRIPTEN__
/* Render resolution, exported so the page can see when it disagrees with the canvas. */
extern "C" EMSCRIPTEN_KEEPALIVE int ViceScreenWidth(void) { return RsGlobal.maximumWidth; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceScreenHeight(void) { return RsGlobal.maximumHeight; }
/* RsGlobal.width/height is the 3D render size and what Scene.camera is created from; maximum* is
   the HUD's layout space. They are different numbers and only one of them sizes the camera. */
extern "C" EMSCRIPTEN_KEEPALIVE int ViceRenderWidth(void)  { return RsGlobal.width; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceRenderHeight(void) { return RsGlobal.height; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceRasterW(void) {
    return Scene.camera ? RwRasterGetWidth(RwCameraGetRaster(Scene.camera)) : -1; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceRasterH(void) {
    return Scene.camera ? RwRasterGetHeight(RwCameraGetRaster(Scene.camera)) : -1; }
#endif

void
cursorCB(double xpos, double ypos) {
    if (!FrontEndMenuManager.m_bMenuActive)
        return;

    // The menu is laid out in RsGlobal.maximum* space, but SDL reports the pointer in window
    // space. On a desktop those are the same number so the scale was left out; in a browser the
    // window is the canvas element and the render resolution is whatever video mode the game
    // picked, so the cursor drifted away from the real pointer. The ratio is 1 when they match,
    // which leaves every native build exactly as it was.
    int winw = 0, winh = 0;
    SDL_GetWindowSize(PSGLOBAL(window), &winw, &winh);
    // SCREEN_WIDTH, not maximumWidth. The cursor sprite is drawn at m_nMousePos* translated in
    // SCREEN_* space (RsGlobal.width/height) — scaling to anything else puts the drawn pointer
    // somewhere the real one is not, in exact proportion to how far the two disagree.
    FrontEndMenuManager.m_nMouseTempPosX = winw > 0 ? xpos * (SCREEN_WIDTH / winw) : xpos;
    FrontEndMenuManager.m_nMouseTempPosY = winh > 0 ? ypos * (SCREEN_HEIGHT / winh) : ypos;
}

void
cursorEnterCB(int entered) {
    PSGLOBAL(cursorIsInWindow) = !!entered;
}

void
windowFocusCB(int focused) {
    WindowFocused = !!focused;
}

void
windowIconifyCB(int iconified) {
    WindowIconified = !!iconified;
}

void inputEventHandler() {
    SDL_Event event;

    // Drain the queue, do not sip from it. Taking a single event per frame is survivable on a
    // desktop, where the queue is usually near-empty; in a browser the pointer produces a steady
    // stream of motion events, so a key press ends up behind a permanent backlog and is dequeued
    // seconds late or never. Symptom: the cursor moves perfectly and no key or click ever
    // registers.
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_KEYDOWN:	/* fall-through */
            case SDL_KEYUP:
                keypressCB(event.key.keysym.sym, event.type, 0);
                break;

            case SDL_MOUSEMOTION: cursorCB(event.motion.x, event.motion.y); break;
            case SDL_MOUSEWHEEL: scrollCB(event.wheel.x, event.wheel.y); break;

            // note that SDL_CONTROLLERDEVICEADDED/REMOVED exists, but it did not work for me
            case SDL_JOYDEVICEADDED:	/* fall-through */
            case SDL_JOYDEVICEREMOVED:
                joysChangeCB(event.jdevice.which, event.type);
                break;

            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_ENTER: cursorEnterCB(true); break;
                    case SDL_WINDOWEVENT_LEAVE: cursorEnterCB(false); break;
                    case SDL_WINDOWEVENT_FOCUS_GAINED: windowFocusCB(true); break;
                    case SDL_WINDOWEVENT_FOCUS_LOST: windowFocusCB(false); break;
                    // TODO should it be minimized/maximized/restored instead of shown/hidden?
                    case SDL_WINDOWEVENT_SHOWN: windowIconifyCB(false); break;
                    case SDL_WINDOWEVENT_HIDDEN: windowIconifyCB(true); break;
                }
                break;

            default:
                break;
        }
    }
}

/*
 *****************************************************************************
 */
#ifdef __EMSCRIPTEN__
/*
 * Read-only probes for the page. C++ state is otherwise invisible from JavaScript, and the
 * difference between "the menu is off-screen", "the menu is not active" and "the render path is
 * skipped entirely" is impossible to tell apart from the outside — all three look like a black
 * canvas with a cursor on it.
 */
extern "C" EMSCRIPTEN_KEEPALIVE int ViceGameState(void)   { return gGameState; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceMenuActive(void)  { return FrontEndMenuManager.m_bMenuActive; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceMenuScreen(void)  { return FrontEndMenuManager.m_nCurrScreen; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceForeground(void)  { return ForegroundApp; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceIconified(void)   { return WindowIconified; }
/* Render at whatever size the page gives us, so the canvas is never scaled and never soft.
   resizeCB is the same path a desktop window resize takes: it retargets the camera and updates
   RsGlobal.maximum*, which is the space the menu lays itself out in. */
extern "C" EMSCRIPTEN_KEEPALIVE void ViceSetResolution(int w, int h)
{
    if (w < 320 || h < 240) return;
    if (w == RsGlobal.maximumWidth && h == RsGlobal.maximumHeight) return;
    SDL_SetWindowSize(PSGLOBAL(window), w, h);
    resizeCB(w, h);
}

/*
 * Flush the save directory to IndexedDB.
 *
 * userfiles is an IDBFS mount (see the shell): writes land in memory and are only durable once
 * syncfs runs, so without this every save and every settings change dies with the tab. Debounced
 * because the engine writes a save as a burst of small files and one flush covers the lot.
 */
// How much the allocator is actually holding, which is the number that decides how small
// -sINITIAL_MEMORY can safely be: reserving a gigabyte the game never touches is what keeps this
// off iOS, where a tab is capped well below that.
//
// Not sbrk(0) - emscripten's dlmalloc does not move the program break, so that reads back a
// constant (the end of static data) however much is allocated, which reads like a 27 MB game.
extern "C" EMSCRIPTEN_KEEPALIVE int ViceHeapUsed(void)
{
    struct mallinfo info = mallinfo();
    return (int)info.uordblks;
}

extern "C" EMSCRIPTEN_KEEPALIVE void ViceSyncSaves(void)
{
    EM_ASM({
        if (typeof FS === "undefined" || !FS.syncfs) return;
        clearTimeout(Module.__viceSyncTimer);
        Module.__viceSyncTimer = setTimeout(function () {
            FS.syncfs(false, function (err) { if (err) console.warn("save sync failed", err); });
        }, 400);
    });
}

extern "C" EMSCRIPTEN_KEEPALIVE int ViceMenuFade(void)    { return FrontEndMenuManager.m_nMenuFadeAlpha; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceFirstStart(void)  { return FrontEndMenuManager.m_firstStartCounter; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceTimeMs(void)      { return (int)CTimer::GetTimeInMillisecondsPauseMode(); }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceLogicalFrames(void) { return (int)CTimer::GetLogicalFramesPassed(); }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceMouseX(void)      { return (int)FrontEndMenuManager.m_nMouseTempPosX; }
/* Sky and ambient as the timecycle currently computes them. If these come back grey (R==G==B)
   the timecycle is the problem; if they are colourful and the screen is not, the renderer is. */
/* Leave the frontend without going through the menu. The game loop drops out of GS_FRONTEND the
   moment the menu stops being active, which is all "Start Game" ultimately does — and it makes
   the in-game state reachable from a test that cannot click. */
extern "C" EMSCRIPTEN_KEEPALIVE void ViceStartGame(void) {
    FrontEndMenuManager.m_bMenuActive = false;
    // The menu pauses the timer when it opens (CMenuManager::Initialise), and the normal exit
    // path is what ends that pause. Skipping straight past the menu leaves the game paused:
    // it renders, but CClock, CWeather and CTimeCycle never tick, so the world sits unlit and
    // frozen — which looks exactly like a broken renderer.
    CTimer::EndUserPause();
}

extern "C" EMSCRIPTEN_KEEPALIVE int ViceSkyTop(void) {
    return (CTimeCycle::GetSkyTopRed() << 16) | (CTimeCycle::GetSkyTopGreen() << 8) | CTimeCycle::GetSkyTopBlue();
}
extern "C" EMSCRIPTEN_KEEPALIVE int ViceSkyBottom(void) {
    return (CTimeCycle::GetSkyBottomRed() << 16) | (CTimeCycle::GetSkyBottomGreen() << 8) | CTimeCycle::GetSkyBottomBlue();
}
extern "C" EMSCRIPTEN_KEEPALIVE int ViceAmbient(void) {
    return ((int)CTimeCycle::GetAmbientRed() << 16) | ((int)CTimeCycle::GetAmbientGreen() << 8) | (int)CTimeCycle::GetAmbientBlue();
}
extern "C" EMSCRIPTEN_KEEPALIVE int ViceClockHours(void) { return CClock::GetHours(); }
/* Minutes advancing is the test for whether CGame::Process runs at all — the timecycle update
   lives inside it, so a frozen clock and empty colours would have one cause, not two. */
extern "C" EMSCRIPTEN_KEEPALIVE int ViceClockMinutes(void) { return CClock::GetMinutes(); }


extern "C" EMSCRIPTEN_KEEPALIVE int ViceDrawnMouseX(void) { return (int)FrontEndMenuManager.m_nMousePosX; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceDrawnMouseY(void) { return (int)FrontEndMenuManager.m_nMousePosY; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceMouseY(void)      { return (int)FrontEndMenuManager.m_nMouseTempPosY; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceWindowW(void)     { int w=0,h=0; SDL_GetWindowSize(PSGLOBAL(window), &w, &h); return w; }
extern "C" EMSCRIPTEN_KEEPALIVE int ViceWindowH(void)     { int w=0,h=0; SDL_GetWindowSize(PSGLOBAL(window), &w, &h); return h; }
#endif

int
main(int argc, char *argv[])
{
    RwV2d pos;
    RwInt32 i;

#ifdef USE_CUSTOM_ALLOCATOR
    InitMemoryMgr();
#endif

    struct sigaction act;
    act.sa_sigaction = terminateHandler;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGTERM, &act, NULL);
#ifdef FLUSHABLE_STREAMING
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = dummyHandler;
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);
#endif

    for(i = 1; i < argc; i++)
    {
        if(strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            const char *gamePath = argv[i+1];
            setenv("STORAGE_ROOT", gamePath, 1);
        }
    }

    /*
     * Initialize the platform independent data.
     * This will in turn initialize the platform specific data...
     */
    if( RsEventHandler(rsINITIALIZE, nil) == rsEVENTERROR )
    {
        return FALSE;
    }

    for(i=1; i<argc; i++)
    {
        RsEventHandler(rsPREINITCOMMANDLINE, argv[i]);
    }

    /*
     * Parameters to be used in RwEngineOpen / rsRWINITIALISE event
     */

    openParams.width = RsGlobal.maximumWidth;
    openParams.height = RsGlobal.maximumHeight;
    openParams.windowtitle = RsGlobal.appName;
    openParams.window = &PSGLOBAL(window);

    ControlsManager.MakeControllerActionsBlank();
    ControlsManager.InitDefaultControlConfiguration();

    /*
     * Initialize the 3D (RenderWare) components of the app...
     */
    if( rsEVENTERROR == RsEventHandler(rsRWINITIALIZE, &openParams) )
    {
        RsEventHandler(rsTERMINATE, nil);

        return 0;
    }

    psPostRWinit();

    {
        CMouseControllerState mouseSetup = MousePointerStateHelper.GetMouseSetUp();
        debug("Mouse buttons bound: LMB=%d RMB=%d MMB=%d wheel=%d",
              mouseSetup.LMB, mouseSetup.RMB, mouseSetup.MMB, mouseSetup.WHEELUP);
        ControlsManager.InitDefaultControlConfigMouse(mouseSetup);
    }

//	glfwSetWindowPos(PSGLOBAL(window), 0, 0);

    /*
     * Parse command line parameters (except program name) one at
     * a time AFTER RenderWare initialization...
     */
    for(i=1; i<argc; i++)
    {
        RsEventHandler(rsCOMMANDLINE, argv[i]);
    }

    /*
     * Force a camera resize event...
     */
    {
        RwRect r;

        r.x = 0;
        r.y = 0;
        r.w = RsGlobal.maximumWidth;
        r.h = RsGlobal.maximumHeight;

        RsEventHandler(rsCAMERASIZE, &r);
    }

    {
        CFileMgr::SetDirMyDocuments();

#ifdef LOAD_INI_SETTINGS
        // At this point InitDefaultControlConfigJoyPad must have set all bindings to default and ms_padButtonsInited to number of detected buttons.
        // We will load stored bindings below, but let's cache ms_padButtonsInited before LoadINIControllerSettings and LoadSettings clears it,
        // so we can add new joy bindings **on top of** stored bindings.
        int connectedPadButtons = ControlsManager.ms_padButtonsInited;
#endif

        int32 gta3set = CFileMgr::OpenFile("gta_vc.set", "r");

        if ( gta3set )
        {
            ControlsManager.LoadSettings(gta3set);
            CFileMgr::CloseFile(gta3set);
        }

        CFileMgr::SetDir("");

#ifdef LOAD_INI_SETTINGS
        LoadINIControllerSettings();
        if (connectedPadButtons != 0)
            ControlsManager.InitDefaultControlConfigJoyPad(connectedPadButtons); // add (connected-saved) amount of new button assignments on top of ours

        // these have 2 purposes: creating .ini at the start, and adding newly introduced settings to old .ini at the start
        SaveINISettings();
        SaveINIControllerSettings();
#endif
    }

#ifdef PS2_MENU
    int32 r = TheMemoryCard.CheckCardStateAtGameStartUp(CARD_ONE);
	if (   r == CMemoryCard::ERR_DIRNOENTRY  || r == CMemoryCard::ERR_NOFORMAT
		&& r != CMemoryCard::ERR_OPENNOENTRY && r != CMemoryCard::ERR_NONE )
	{
		LoadingScreen(nil, nil, "loadsc0");

		TheText.Unload();
		TheText.Load();

		CFont::Initialise();

		FrontEndMenuManager.DrawMemoryCardStartUpMenus();
	}
#endif

    while ( TRUE )
    {
        RwInitialised = TRUE;

        /*
        * Set the initial mouse position...
        */
        pos.x = RsGlobal.maximumWidth * 0.5f;
        pos.y = RsGlobal.maximumHeight * 0.5f;

        RsMouseSetPos(&pos);

        /*
        * Enter the message processing loop...
        */

#ifndef MASTER
        if (gbModelViewer) {
            // This is TheModelViewer in LCS
            LoadingScreen("Loading the ModelViewer", NULL, GetRandomSplashScreen());
            CAnimViewer::Initialise();
            CTimer::Update();
#ifndef PS2_MENU
            FrontEndMenuManager.m_bGameNotLoaded = false;
#endif
        }
#endif

#ifdef PS2_MENU
        if (TheMemoryCard.m_bWantToLoad)
			LoadSplash(GetLevelSplashScreen(CGame::currLevel));

		TheMemoryCard.m_bWantToLoad = false;

		CTimer::Update();

		while( !RsGlobal.quit && !(FrontEndMenuManager.m_bWantToRestart || TheMemoryCard.b_FoundRecentSavedGameWantToLoad) && !SDL_QuitRequested())
#else
        while( !RsGlobal.quit && !FrontEndMenuManager.m_bWantToRestart && !SDL_QuitRequested())
#endif
        {
#ifdef __EMSCRIPTEN__
            // Nothing is presented until control goes back to the browser, and a tab that never
            // yields is a hung tab. Top of the frame is the one place on this stack with no
            // invoke_* JS frame above us, which Asyncify cannot unwind through.
            reVCFrames++;
            reVCYieldFrame();
#endif
            inputEventHandler();

#ifndef MASTER
            if (gbModelViewer) {
                // This is TheModelViewerCore in LCS
                TheModelViewer();
            } else
#endif
            if ( ForegroundApp )
            {
                switch ( gGameState )
                {
                    case GS_START_UP:
                    {
#ifdef NO_MOVIES
                        gGameState = GS_INIT_ONCE;
#else
                        gGameState = GS_INIT_LOGO_MPEG;
#endif
                        TRACE("gGameState = GS_INIT_ONCE");
                        break;
                    }

                    case GS_INIT_LOGO_MPEG:
                    {
                        //if (!startupDeactivate)
                        //    PlayMovieInWindow(cmdShow, "movies\\Logo.mpg");
                        gGameState = GS_LOGO_MPEG;
                        TRACE("gGameState = GS_LOGO_MPEG;");
                        break;
                    }

                    case GS_LOGO_MPEG:
                    {
//					    CPad::UpdatePads();

//					    if (startupDeactivate || ControlsManager.GetJoyButtonJustDown() != 0)
                        ++gGameState;
//					    else if (CPad::GetPad(0)->GetLeftMouseJustDown())
//						    ++gGameState;
//					    else if (CPad::GetPad(0)->GetEnterJustDown())
//						    ++gGameState;
//					    else if (CPad::GetPad(0)->GetCharJustDown(' '))
//						    ++gGameState;
//					    else if (CPad::GetPad(0)->GetAltJustDown())
//						    ++gGameState;
//					    else if (CPad::GetPad(0)->GetTabJustDown())
//						    ++gGameState;

                        break;
                    }

                    case GS_INIT_INTRO_MPEG:
                    {
//#ifndef NO_MOVIES
//					    CloseClip();
//					    CoUninitialize();
//#endif
//
//					    if (CMenuManager::OS_Language == LANG_FRENCH || CMenuManager::OS_Language == LANG_GERMAN)
//						    PlayMovieInWindow(cmdShow, "movies\\GTAtitlesGER.mpg");
//					    else
//						    PlayMovieInWindow(cmdShow, "movies\\GTAtitles.mpg");

                        gGameState = GS_INTRO_MPEG;
                        TRACE("gGameState = GS_INTRO_MPEG;");
                        break;
                    }

                    case GS_INTRO_MPEG:
                    {
//					    CPad::UpdatePads();
//
//					    if (startupDeactivate || ControlsManager.GetJoyButtonJustDown() != 0)
                        ++gGameState;
//					    else if (CPad::GetPad(0)->GetLeftMouseJustDown())
//						    ++gGameState;
//					    else if (CPad::GetPad(0)->GetEnterJustDown())
//						    ++gGameState;
//					    else if (CPad::GetPad(0)->GetCharJustDown(' '))
//						    ++gGameState;
//					    else if (CPad::GetPad(0)->GetAltJustDown())
//						    ++gGameState;
//					    else if (CPad::GetPad(0)->GetTabJustDown())
//						    ++gGameState;

                        break;
                    }

                    case GS_INIT_ONCE:
                    {
                        //CoUninitialize();

#ifdef PS2_MENU
                        extern char version_name[64];
						if ( CGame::frenchGame || CGame::germanGame )
							LoadingScreen(NULL, version_name, "loadsc24");
						else
							LoadingScreen(NULL, version_name, "loadsc0");

						printf("Into TheGame!!!\n");
#else
                        LoadingScreen(nil, nil, "loadsc0");
#endif
                        if ( !CGame::InitialiseOnceAfterRW() )
                            RsGlobal.quit = TRUE;

#ifdef PS2_MENU
                        gGameState = GS_INIT_PLAYING_GAME;
#else
                        gGameState = GS_INIT_FRONTEND;
                        TRACE("gGameState = GS_INIT_FRONTEND;");
#endif
                        break;
                    }

#ifndef PS2_MENU
                    case GS_INIT_FRONTEND:
                    {
                        LoadingScreen(nil, nil, "loadsc0");

                        FrontEndMenuManager.m_bGameNotLoaded = true;

                        FrontEndMenuManager.m_bStartUpFrontEndRequested = true;

                        if ( defaultFullscreenRes )
                        {
                            defaultFullscreenRes = FALSE;
                            FrontEndMenuManager.m_nPrefsVideoMode = GcurSelVM;
                            FrontEndMenuManager.m_nDisplayVideoMode = GcurSelVM;
                        }

                        gGameState = GS_FRONTEND;
                        TRACE("gGameState = GS_FRONTEND;");
                        break;
                    }

                    case GS_FRONTEND:
                    {
                        if(!WindowIconified)
                            RsEventHandler(rsFRONTENDIDLE, nil);

#ifdef PS2_MENU
                        if ( !FrontEndMenuManager.m_bMenuActive || TheMemoryCard.m_bWantToLoad )
#else
                        if ( !FrontEndMenuManager.m_bMenuActive || FrontEndMenuManager.m_bWantToLoad )
#endif
                        {
                            gGameState = GS_INIT_PLAYING_GAME;
                            TRACE("gGameState = GS_INIT_PLAYING_GAME;");
                        }

#ifdef PS2_MENU
                        if (TheMemoryCard.m_bWantToLoad )
#else
                        if ( FrontEndMenuManager.m_bWantToLoad )
#endif
                        {
                            InitialiseGame();
                            FrontEndMenuManager.m_bGameNotLoaded = false;
                            gGameState = GS_PLAYING_GAME;
                            TRACE("gGameState = GS_PLAYING_GAME;");
                        }
                        break;
                    }
#endif

                    case GS_INIT_PLAYING_GAME:
                    {
#ifdef PS2_MENU
                        CGame::Initialise("DATA\\GTA3.DAT");

						//LoadingScreen("Starting Game", NULL, GetRandomSplashScreen());

						if (   TheMemoryCard.CheckCardInserted(CARD_ONE) == CMemoryCard::NO_ERR_SUCCESS
							&& TheMemoryCard.ChangeDirectory(CARD_ONE, TheMemoryCard.Cards[CARD_ONE].dir)
							&& TheMemoryCard.FindMostRecentFileName(CARD_ONE, TheMemoryCard.MostRecentFile) == true
							&& TheMemoryCard.CheckDataNotCorrupt(TheMemoryCard.MostRecentFile))
						{
							strcpy(TheMemoryCard.LoadFileName, TheMemoryCard.MostRecentFile);
							TheMemoryCard.b_FoundRecentSavedGameWantToLoad = true;

							if (CMenuManager::m_PrefsLanguage != TheMemoryCard.GetLanguageToLoad())
							{
								CMenuManager::m_PrefsLanguage = TheMemoryCard.GetLanguageToLoad();
								TheText.Unload();
								TheText.Load();
							}

							CGame::currLevel = (eLevelName)TheMemoryCard.GetLevelToLoad();
						}
#else
                        InitialiseGame();

                        FrontEndMenuManager.m_bGameNotLoaded = false;
#endif
                        gGameState = GS_PLAYING_GAME;
                        TRACE("gGameState = GS_PLAYING_GAME;");
                        break;
                    }

                    case GS_PLAYING_GAME:
                    {
                        float ms = (float)CTimer::GetCurrentTimeInCycles() / (float)CTimer::GetCyclesPerMillisecond();
                        if ( RwInitialised )
                        {
                            if (!FrontEndMenuManager.m_PrefsFrameLimiter || (1000.0f / (float)RsGlobal.maxFPS) < ms)
                                RsEventHandler(rsIDLE, (void *)TRUE);
                        }
                        break;
                    }
                }
            }
            else
            {
                if ( RwCameraBeginUpdate(Scene.camera) )
                {
                    RwCameraEndUpdate(Scene.camera);
                    ForegroundApp = TRUE;
                    RsEventHandler(rsACTIVATE, (void *)TRUE);
                }

            }
        }


        /*
        * About to shut down - block resize events again...
        */
        RwInitialised = FALSE;

        FrontEndMenuManager.UnloadTextures();
#ifdef PS2_MENU
        if ( !(FrontEndMenuManager.m_bWantToRestart || TheMemoryCard.b_FoundRecentSavedGameWantToLoad))
			break;
#else
        if ( !FrontEndMenuManager.m_bWantToRestart )
            break;
#endif

        CPad::ResetCheats();
        CPad::StopPadsShaking();

        DMAudio.ChangeMusicMode(MUSICMODE_DISABLE);

#ifdef PS2_MENU
        CGame::ShutDownForRestart();
#endif

        CTimer::Stop();

#ifdef PS2_MENU
        if (FrontEndMenuManager.m_bWantToRestart || TheMemoryCard.b_FoundRecentSavedGameWantToLoad)
		{
			if (TheMemoryCard.b_FoundRecentSavedGameWantToLoad)
			{
				FrontEndMenuManager.m_bWantToRestart = true;
				TheMemoryCard.m_bWantToLoad = true;
			}

			CGame::InitialiseWhenRestarting();
			DMAudio.ChangeMusicMode(MUSICMODE_GAME);
			FrontEndMenuManager.m_bWantToRestart = false;

			continue;
		}

		CGame::ShutDown();
		CTimer::Stop();

		break;
#else
        if ( FrontEndMenuManager.m_bWantToLoad )
        {
            CGame::ShutDownForRestart();
            CGame::InitialiseWhenRestarting();
            DMAudio.ChangeMusicMode(MUSICMODE_GAME);
            LoadSplash(GetLevelSplashScreen(CGame::currLevel));
            FrontEndMenuManager.m_bWantToLoad = false;
        }
        else
        {
#ifndef MASTER
            if ( gbModelViewer )
                CAnimViewer::Shutdown();
            else
#endif
            if ( gGameState == GS_PLAYING_GAME )
                CGame::ShutDown();

            CTimer::Stop();

            if ( FrontEndMenuManager.m_bFirstTime == true )
            {
                gGameState = GS_INIT_FRONTEND;
                TRACE("gGameState = GS_INIT_FRONTEND;");
            }
            else
            {
                gGameState = GS_INIT_PLAYING_GAME;
                TRACE("gGameState = GS_INIT_PLAYING_GAME;");
            }
        }

        FrontEndMenuManager.m_bFirstTime = false;
        FrontEndMenuManager.m_bWantToRestart = false;
#endif
    }


#ifndef MASTER
    if ( gbModelViewer )
        CAnimViewer::Shutdown();
    else
#endif
    if ( gGameState == GS_PLAYING_GAME )
        CGame::ShutDown();

    DMAudio.Terminate();

    _psFreeVideoModeList();


    /*
     * Tidy up the 3D (RenderWare) components of the application...
     */
    RsEventHandler(rsRWTERMINATE, nil);

    /*
     * Free the platform dependent data...
     */
    RsEventHandler(rsTERMINATE, nil);

    return 0;
}

/*
 *****************************************************************************
 */

RwV2d leftStickPos;
RwV2d rightStickPos;

void CapturePad(RwInt32 padID)
{
    static SDL_GameController* gamepad = nullptr;

    if (padID == 0)
        gamepad = gamepad1;
    else if(padID == 1)
        gamepad = gamepad2;
    else
        assert("invalid padID");

    if (gamepad == nullptr)
        return;

    SDL_Joystick* joy = SDL_GameControllerGetJoystick(gamepad);
    int joyId = SDL_JoystickInstanceID(joy);
    int numAxes = SDL_JoystickNumAxes(joy);

    if (ControlsManager.m_bFirstCapture == false) {
        memcpy(&ControlsManager.m_OldState, &ControlsManager.m_NewState, sizeof(ControlsManager.m_NewState));
    } else {
        // In case connected gamepad doesn't have L-R trigger axes.
        ControlsManager.m_NewState.mappedButtons[15] = 0;	// left trigger
        ControlsManager.m_NewState.mappedButtons[16] = 0;	// right trigger
    }

    // Update buttons state
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; ++i) {
        int state = SDL_GameControllerGetButton(gamepad, (SDL_GameControllerButton) i);
        assert(state == 0 || state == 1);
        ControlsManager.m_NewState.buttons[i] = state;
        ControlsManager.m_NewState.mappedButtons[i] = !!state;
    }

    ControlsManager.m_NewState.numButtons = SDL_CONTROLLER_BUTTON_MAX - 1;
    ControlsManager.m_NewState.id = joyId;
    ControlsManager.m_NewState.isGamepad = true; // SDL_IsGameController(joyId);

    if (ControlsManager.m_NewState.isGamepad) {
        // TRIGGERLEFT/RIGHT are in range 0..32767, which needs to be converted to -1 (released) .. 1 (pressed)
        float lt = (SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) - 16384.0) / 16384.0;
        float rt = (SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) - 16384.0) / 16384.0;

        // glfw returns 0.0 for non-existent axises(which is bullocks) so we treat it as deadzone, and keep value of previous frame.
        // otherwise if this axis is present, -1 = released, 1 = pressed
        if (lt != 0.0f)
            ControlsManager.m_NewState.mappedButtons[15] = lt > -0.8f;

        if (rt != 0.0f)
            ControlsManager.m_NewState.mappedButtons[16] = rt > -0.8f;
    }
    // TODO? L2-R2 axes(not buttons-that's fine) on joysticks that don't have SDL gamepad mapping AREN'T handled, and I think it's impossible to do without mapping.

    if (ControlsManager.m_bFirstCapture == true) {
        memcpy(&ControlsManager.m_OldState, &ControlsManager.m_NewState, sizeof(ControlsManager.m_NewState));
        ControlsManager.m_bFirstCapture = false;
    }

    RsPadButtonStatus bs;
    bs.padID = padID;
    RsPadEventHandler(rsPADBUTTONUP, (void *)&bs);

    // Gamepad axes are guaranteed to return 0.0f if that particular gamepad doesn't have that axis.
    // And that's really good for sticks, because gamepads return 0.0 for them when sticks are in released state.
    // Stick position is converted from range [-32768; 32767] to [-1; +1]
    leftStickPos.x = (ControlsManager.m_NewState.isGamepad && numAxes >= 1) ? SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTX) / 32768.0f : 0.0f;
    leftStickPos.y = (ControlsManager.m_NewState.isGamepad && numAxes >= 2) ? SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_LEFTY) / 32768.0f : 0.0f;

    rightStickPos.x = (ControlsManager.m_NewState.isGamepad && numAxes >= 3) ? SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_RIGHTX) / 32768.0f : 0.0f;
    rightStickPos.y = (ControlsManager.m_NewState.isGamepad && numAxes >= 4) ? SDL_GameControllerGetAxis(gamepad, SDL_CONTROLLER_AXIS_RIGHTY) / 32768.0f : 0.0f;

    {
        if (CPad::m_bMapPadOneToPadTwo)
            bs.padID = 1;

        RsPadEventHandler(rsPADBUTTONUP,   (void *)&bs);
        RsPadEventHandler(rsPADBUTTONDOWN, (void *)&bs);
    }

    {
        if (CPad::m_bMapPadOneToPadTwo)
            bs.padID = 1;

        CPad *pad = CPad::GetPad(bs.padID);

        if (Abs(leftStickPos.x)  > ControlsManager.m_lStickDeadzone)
            pad->PCTempJoyState.LeftStickX	= (int32)(leftStickPos.x  * 128.0f * ControlsManager.m_lStickSensX);

        if (Abs(leftStickPos.y)  > ControlsManager.m_lStickDeadzone)
            pad->PCTempJoyState.LeftStickY	= (int32)(leftStickPos.y  * 128.0f * ControlsManager.m_lStickSensY);

        if (Abs(rightStickPos.x) > ControlsManager.m_rStickDeadzone)
            pad->PCTempJoyState.RightStickX = (int32)(rightStickPos.x * 128.0f * ControlsManager.m_rStickSensX);

        if (Abs(rightStickPos.y) > ControlsManager.m_rStickDeadzone)
            pad->PCTempJoyState.RightStickY = (int32)(rightStickPos.y * 128.0f * ControlsManager.m_rStickSensY);
    }

    _psHandleVibration();
}

void joysChangeCB(int jid, int event)
{
    if (event == SDL_JOYDEVICEADDED && !IsThisJoystickBlacklisted(jid)) {
        if (PSGLOBAL(joy1id) == -1) {
            assert(gamepad1 == nullptr);
            gamepad1 = SDL_GameControllerOpen(jid);
            assert(gamepad1 != nullptr);
#ifdef DETECT_JOYSTICK_MENU
            strncpy(gSelectedJoystickName, SDL_JoystickNameForIndex(jid), sizeof(gSelectedJoystickName));
#endif
            // This is behind LOAD_INI_SETTINGS, because otherwise the Init call below will destroy/overwrite your bindings.
#ifdef LOAD_INI_SETTINGS
            SDL_Joystick* joy = SDL_GameControllerGetJoystick(gamepad1);
            int count = SDL_JoystickNumButtons(joy);
            ControlsManager.InitDefaultControlConfigJoyPad(count);
            PSGLOBAL(joy1id) = SDL_JoystickInstanceID(joy);	// needed to get right ID for remove event
            // Do not close the controller handle, it is done in the device remove handler (below)
#endif
        } else if (PSGLOBAL(joy2id) == -1) {
            assert(gamepad2 == nullptr);
            gamepad2 = SDL_GameControllerOpen(jid);
            assert(gamepad2 != nullptr);
            SDL_Joystick* joy = SDL_GameControllerGetJoystick(gamepad2);
            PSGLOBAL(joy2id) = SDL_JoystickInstanceID(joy);	// needed to get right ID for remove event
        }

    } else if (event == SDL_JOYDEVICEREMOVED) {
        if (PSGLOBAL(joy1id) == jid) {
            assert(gamepad1 != nullptr);
            SDL_GameControllerClose(gamepad1);
            PSGLOBAL(joy1id) = -1;
            gamepad1 = nullptr;
        } else if (PSGLOBAL(joy2id) == jid) {
            assert(gamepad2 != nullptr);
            SDL_GameControllerClose(gamepad2);
            PSGLOBAL(joy2id) = -1;
            gamepad2 = nullptr;
        }
    }
}

#endif
