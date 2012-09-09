#include <clib/exec_protos.h>
#include <proto/exec.h>

#include "std/resource.h"
#include "system/check.h"
#include "system/fileio.h"
#include "system/display.h"
#include "system/input.h"
#include "system/vblank.h"

#include "startup.h"

struct DosLibrary *DOSBase;
struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;

extern void MainLoop();
extern bool SetupDisplay();
extern void TearDownEffect();
extern void SetupEffect();
extern void AddInitialResources();

int main() {
  DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 39);
  GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
  IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);

  if (DOSBase && GfxBase && IntuitionBase && SystemCheck() && InitFileIo()) {
    StartResourceManager();
    AddInitialResources();
    StartEventQueue();

    if (SetupDisplay()) {
      InstallVBlankIntServer();
      SetupEffect();
      MainLoop();
      TearDownEffect();
      RemoveVBlankIntServer();
      KillDisplay();
    }

    StopEventQueue();
    StopResourceManager();
  }

  CloseLibrary((struct Library *)IntuitionBase);
  CloseLibrary((struct Library *)GfxBase);
  CloseLibrary((struct Library *)DOSBase);

  return 0;
}