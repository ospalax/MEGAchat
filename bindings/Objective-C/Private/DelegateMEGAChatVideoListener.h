
#import "megachatapi.h"
#import "MEGAChatSdk.h"

class DelegateMEGAChatVideoListener : public megachat::MegaChatVideoListener {
    
public:
    DelegateMEGAChatVideoListener(MEGAChatSdk *megaChatSdk, id<MEGAChatVideoDelegate>listener, bool singleListener = true);
    id<MEGAChatVideoDelegate>getUserListener();
    
    void onChatVideoData(megachat::MegaChatApi *api, uint64_t chatid, int width, int height, char *buffer, size_t size);
    
private:
    __weak MEGAChatSdk *megaChatSdk;
    id<MEGAChatVideoDelegate>listener;
    bool singleListener;
};
