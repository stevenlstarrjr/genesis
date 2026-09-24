#include "platform/web/BrowserHost.h"
#include <SDL3/SDL.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <algorithm>

EM_ASYNC_JS(void,restoreBrowserProject,(),{
    try { await Module.restoreProject(); }
    catch(error) { console.error('Browser storage unavailable:',error); Module.storageError=String(error); }
});
EM_ASYNC_JS(int,saveBrowserProject,(const char* path),{
    try { await Module.saveProject(UTF8ToString(path)); return 1; }
    catch(error) { Module.storageError=String(error); return 0; }
});
EM_JS(void,storageError,(char* text,int capacity),{ stringToUTF8(Module.storageError||'Browser storage failed',text,capacity); });
EM_JS(int,popImport,(char* text,int capacity),{
    if(!Module.imports.length)return 0;
    stringToUTF8(Module.imports.shift(),text,capacity);return 1;
});

namespace genesis::web {
void initialize() { restoreBrowserProject(); }
void prepareWindow() {
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR,"#canvas");
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT,"#canvas");
}
void frame(SDL_Window* window) {
    // JSPI preserves the renderer's stack-owned resources while allowing
    // WebGPU promises, input events and the browser compositor to make progress.
    emscripten_sleep(16);
    double width=0,height=0;
    emscripten_get_element_css_size("#canvas",&width,&height);
    const int w=std::max(1,int(width)),h=std::max(1,int(height));
    int oldW=0,oldH=0; SDL_GetWindowSize(window,&oldW,&oldH);
    if(w!=oldW || h!=oldH) SDL_SetWindowSize(window,w,h);
    EM_ASM({
        document.getElementById('loading').hidden=true;
        Module.genesisFrames=(Module.genesisFrames||0)+1;
        Module.canvas.dataset.frames=String(Module.genesisFrames);
    });
}
bool save(const std::filesystem::path& scene,std::string& error) {
    if(saveBrowserProject(scene.generic_string().c_str())) return true;
    char text[1024]{}; storageError(text,sizeof(text)); error=text; return false;
}
bool nextImport(std::string& path) {
    char text[1024]{}; if(!popImport(text,sizeof(text))) return false; path=text; return true;
}
bool takeExportRequest() { return EM_ASM_INT({const result=Module.exportRequested;Module.exportRequested=false;return result?1:0;})!=0; }
void download(const std::filesystem::path& scene) {
    EM_ASM({Module.downloadScene(UTF8ToString($0));},scene.generic_string().c_str());
}
void setDirty(bool dirty) { EM_ASM({Module.sceneDirty=!!$0;},dirty); }
void runScene() { EM_ASM({location.search='?play=1';}); }
}
