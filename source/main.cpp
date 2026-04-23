#define TESLA_INIT_IMPL // If you have more than one file using the tesla header, only define this in the main one
#include <exception_wrap.hpp>
#include <tesla.hpp>    // The Tesla Header

//This is a version for the SysDVR Config app protocol, it's not shown anywhere and not related to the major version
#define SYSDVR_VERSION_MIN 5
#define SYSDVR_VERSION_MAX 18
#define TYPE_MODE_USB 1
#define TYPE_MODE_TCP 2
#define TYPE_MODE_RTSP 4
#define TYPE_MODE_NULL 3
#define TYPE_MODE_SWITCHING 999998
#define TYPE_MODE_ERROR 999999

#define CMD_GET_VER 100
#define CMD_GET_MODE 101
#define CMD_RESET_DISPLAY 103

#define MODE_TO_CMD_SET(x) x
#define UPDATE_INTERVALL 30

class DvrOverlay : public tsl::Gui {
private:
    Service* dvrService;
    bool gotService = false;
    u32 version, mode = 0, ipAddress = 0;
    u32 targetMode = 0;
    int waitFrames = -1;
    tsl::elm::ListItem* lastModeItem = nullptr;
    std::string modeString;
    std::string versionString;
    char ipString[20];
    u32 statusColor = 0;
public:
    DvrOverlay(Service* dvrService, bool gotService) {
        this->gotService = gotService;
        this->dvrService = dvrService;
    }

    // Called when this Gui gets loaded to create the UI
    // Allocate all elements on the heap. libtesla will make sure to clean them up when not needed anymore
    virtual tsl::elm::Element* createUI() override {
        // A OverlayFrame is the base element every overlay consists of. This will draw the default Title and Subtitle.
        // If you need more information in the header or want to change it's look, use a HeaderOverlayFrame.
        auto frame = new tsl::elm::OverlayFrame(APP_TITLE, APP_VERSION);
        frame->m_showWidget=true;
        // A list that can contain sub elements and handles scrolling
        auto list = new tsl::elm::List();

        if(!gotService) {   
            list->addItem(getErrorDrawer("Failed to setup SysDVR Service!\nIs sysdvr running?"), getErrorDrawerSize());
            frame->setContent(list);
            return frame;
        }
        
        sysDvrGetVersion(&version);
        versionString = std::to_string(version);

        if(version>SYSDVR_VERSION_MAX ||version<SYSDVR_VERSION_MIN) {
            list->addItem(getErrorDrawer("Unkown SysDVR Config API: v"+ versionString
                +"\nOnly Config API v"+std::to_string(SYSDVR_VERSION_MIN)+" to "+std::to_string(SYSDVR_VERSION_MAX)+" is supported"), getErrorDrawerSize());
            frame->setContent(list);
            return frame;
        }

        u32 newMode, newIp;
        sysDvrGetMode(&newMode);
        nifmGetCurrentIpAddress(&newIp);
        updateMode(newMode);
        updateIP(newIp);

        list->addItem(new tsl::elm::CategoryHeader("Info"));

        // Match ultrahand table layout:
        //   startGap=20, lineHeight=16, newlineGap=4  → row spacing of 20px
        //   columnOffset=164                          → right-column x position
        //   baseX=12                                  → left-column x offset
        //   itemHeight = 16*3 + 4*2 + 9 = 65         → passed to addItem
        //   TableDrawer draws the rounded-rect tableBGColor background automatically
        auto *infodrawer = new tsl::elm::TableDrawer(
            [this](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
                constexpr s32 startGap     = 20;
                constexpr s32 lineHeight   = 16;
                constexpr s32 newlineGap   = 4;
                constexpr s32 columnOffset = 194+10;
                constexpr s32 baseX        = 12;

                const s32 row0 = y + startGap;
                const s32 row1 = row0 + lineHeight + newlineGap;
                const s32 row2 = row1 + lineHeight + newlineGap;

                // Left column — section labels
                renderer->drawString("Mode",        false, x + baseX, row0, lineHeight, tsl::sectionTextColor);
                renderer->drawString("IP-Address",  false, x + baseX, row1, lineHeight, tsl::sectionTextColor);
                renderer->drawString("IPC-Version", false, x + baseX, row2, lineHeight, tsl::sectionTextColor);

                // Right column — info values
                // Status dot at the column start; mode text 14px to the right (same gap as before)
                renderer->drawCircle(x + columnOffset + 6, row0 - 6, 5, true, renderer->a(statusColor));
                renderer->drawString(modeString,    false, x + columnOffset + 14 + 6, row0, lineHeight, tsl::infoTextColor);
                renderer->drawString(ipString,      false, x + columnOffset, row1, lineHeight, tsl::infoTextColor);
                renderer->drawString(versionString, false, x + columnOffset, row2, lineHeight, tsl::infoTextColor);
            },
            false,  // hideTableBackground — draw the rounded-rect background
            9       // endGap (matches drawTable default)
        );
        list->addItem(infodrawer, 65);  // 16*3 + 4*2 + 9

        // List Items
        list->addItem(new tsl::elm::CategoryHeader("Change Mode"));

        auto *offItem = new tsl::elm::ListItem("OFF");
        offItem->setClickListener(getModeLambda(TYPE_MODE_NULL, offItem));
        list->addItem(offItem);

        auto *usbModeItem = new tsl::elm::ListItem("USB");
        usbModeItem->setClickListener(getModeLambda(TYPE_MODE_USB, usbModeItem));
        list->addItem(usbModeItem);

        auto *tcpModeItem = new tsl::elm::ListItem("TCP");
        tcpModeItem->setClickListener(getModeLambda(TYPE_MODE_TCP, tcpModeItem));
        list->addItem(tcpModeItem);

        auto *rtspModeItem = new tsl::elm::ListItem("RTSP");
        rtspModeItem->setClickListener(getModeLambda(TYPE_MODE_RTSP, rtspModeItem));
        list->addItem(rtspModeItem);

        // Set initial checkmark on whichever mode is currently active
        tsl::elm::ListItem* modeItems[]  = { offItem, usbModeItem, tcpModeItem, rtspModeItem };
        u32                  modeCodes[] = { TYPE_MODE_NULL, TYPE_MODE_USB, TYPE_MODE_TCP, TYPE_MODE_RTSP };
        for (int i = 0; i < 4; i++) {
            if (mode == modeCodes[i]) {
                modeItems[i]->setValue(ult::CHECKMARK_SYMBOL);
                lastModeItem = modeItems[i];
                break;
            }
        }

        list->jumpToItem("", ult::CHECKMARK_SYMBOL, true);

        // Add the list to the frame for it to be drawn
        frame->setContent(list);

        // Return the frame to have it become the top level element of this Gui
        return frame;
    }

    tsl::elm::CustomDrawer* getErrorDrawer(std::string message1){
        return new tsl::elm::CustomDrawer([message1](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
            renderer->drawString(message1, false, x + 3, y + 15, 20, (0xF22F));
        });
    }

    int getErrorDrawerSize(){
        return 50;
    }

    std::function<bool(u64 keys)> getModeLambda(u32 mode, tsl::elm::ListItem* item){
        return [this, mode, item](u64 keys) {
            if (keys & HidNpadButton_A) {
                if (lastModeItem) lastModeItem->setValue("");
                item->setValue(ult::CHECKMARK_SYMBOL);
                lastModeItem = item;
                sysDVRRequestModeChange(mode);
                return true;
            }
            return false;
        };
    }


    // Called once every frame to update values
    int currentFrame = 0;
    virtual void update() override {
        currentFrame++;
        if(targetMode!=0 && waitFrames < 1){
            sysDvrSetMode(targetMode);
            refreshCurMode();
            waitFrames=-1;
        } else if(targetMode!=0){
            waitFrames--;
        }
        //only check for dvr mode and ip cahnges every 30 fps, so 0,5-1 sec
        if(currentFrame >= UPDATE_INTERVALL){
            currentFrame=0;
            refreshCurMode();
            refreshIp();
        }
    }

    void refreshCurMode(){
        if(targetMode!=0){
            return; // pending mode change
        }
        u32 newMode;
        Result result = sysDvrGetMode(&newMode);
        if(R_SUCCEEDED(result)){
            updateMode(newMode);
        }
    }

    void refreshIp(){
        u32 newIp;
        nifmGetCurrentIpAddress(&newIp);
        updateIP(newIp);
    }

    void updateMode(u32 newMode){
        if(newMode!=mode){
            mode = newMode;
            modeString = getModeString(mode);
            if(mode == TYPE_MODE_SWITCHING){
                statusColor = 0xF088;
            } else if(mode == TYPE_MODE_ERROR){
                statusColor = 0xF22F;
            } else if(mode == TYPE_MODE_NULL){
                statusColor = 0xF333;
            } else {
                statusColor = 0xF0F0;
            }
        }
    }

    void updateIP(u32 newIp){
        if(newIp!=ipAddress){
            ipAddress = newIp;
            snprintf(ipString, sizeof(ipString)-1, "%u.%u.%u.%u", ipAddress&0xFF, (ipAddress>>8)&0xFF, (ipAddress>>16)&0xFF, (ipAddress>>24)&0xFF);
        }
    }

    // Called once every frame to handle inputs not handled by other UI elements
    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
        return false;   // Return true here to signal the inputs have been consumed
    }

    std::string getModeString(u32 mode){
        switch(mode){
            case TYPE_MODE_USB:
                return"USB";
            case TYPE_MODE_TCP:
                return"TCP";
            case TYPE_MODE_RTSP:
                return"RTSP";
            case TYPE_MODE_NULL:
                return"OFF";
            case TYPE_MODE_SWITCHING:
                return"Switching";
            case TYPE_MODE_ERROR:
                return"Error";
            default:
                return"Unkown";
        }
    }


    Result sysDvrGetVersion(u32* out_ver)
    {
        u32 val;
        Result rc = serviceDispatchOut(dvrService, CMD_GET_VER, val);
        if (R_SUCCEEDED(rc))
            *out_ver = val;
        return rc;
    }

    Result sysDvrGetMode(u32* out_mode)
    {
        u32 val;
        Result rc = serviceDispatchOut(dvrService, CMD_GET_MODE, val);
        if (R_SUCCEEDED(rc))
            *out_mode = val;
        return rc;
    }

    void sysDVRRequestModeChange(u32 command){
        targetMode = command;
        waitFrames = 2;
    }

    Result sysDvrSetMode(u32 command)
    {
        targetMode = 0;
        Result rs = serviceDispatch(dvrService, MODE_TO_CMD_SET(command));

        //close and reinit sysdvr service, to directly apply the new mode.
        smInitialize();
        serviceClose(dvrService);
        smGetService(dvrService, "sysdvr");
        smExit();
        return rs;
    }
};

class OverlayTest : public tsl::Overlay {
private:
    Service dvr;
    bool gotService = false;
public:
    // libtesla already initialized fs, hid, pl, pmdmnt, hid:sys and set:sys
    virtual void initServices() override {
        if(isSysDVRServiceRunning()){
            gotService = R_SUCCEEDED(smGetService(&dvr, "sysdvr"));
        }
        nifmInitialize(NifmServiceType_User);
    }  // Called at the start to initialize all services necessary for this Overlay
    virtual void exitServices() override {
        if(gotService){
            serviceClose(&dvr);
        }
        nifmExit();
    }  // Callet at the end to clean up all services previously initialized

    bool isSysDVRServiceRunning() {
      u8 tmp=0;
      SmServiceName service_name = smEncodeName("sysdvr");
      Result rc;
      if(hosversionAtLeast(12,0,0)){
        rc = tipcDispatchInOut(smGetServiceSessionTipc(), 65100, service_name, tmp);
      } else {
        rc = serviceDispatchInOut(smGetServiceSession(), 65100, service_name, tmp);
      }
      if (R_SUCCEEDED(rc) && tmp & 1)
        return true;
      else
        return false;
    }

    virtual void onShow() override {}    // Called before overlay wants to change from invisible to visible state
    virtual void onHide() override {}    // Called before overlay wants to change from visible to invisible state

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<DvrOverlay>(&dvr, gotService);  // Initial Gui to load. It's possible to pass arguments to it's constructor like this
    }
};

int main(int argc, char **argv) {
    return tsl::loop<OverlayTest>(argc, argv);
}