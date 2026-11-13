#include <JuceHeader.h>

#if JUCE_IOS
 #import <AVFoundation/AVFoundation.h>
 #import <UIKit/UIKit.h>

namespace mobile
{
void requestMicrophonePermission(std::function<void(bool)> completion)
{
    auto handler = ^(BOOL granted)
    {
        juce::MessageManager::callAsync([completion, granted] { completion(granted == YES); });
    };
    if (@available(iOS 17.0, *))
        [AVAudioApplication requestRecordPermissionWithCompletionHandler:handler];
    else
    {
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE("-Wdeprecated-declarations")
        [[AVAudioSession sharedInstance] requestRecordPermission:handler];
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
    }
}

std::function<void()> observeAppDeactivation(std::function<void()> callback)
{
    auto* centre = [NSNotificationCenter defaultCenter];
    auto handler = ^(NSNotification*) { callback(); };
    auto* appObserver = [centre addObserverForName:UIApplicationWillResignActiveNotification
                                           object:nil queue:[NSOperationQueue mainQueue] usingBlock:handler];
    auto* sceneObserver = [centre addObserverForName:UISceneWillDeactivateNotification
                                             object:nil queue:[NSOperationQueue mainQueue] usingBlock:handler];
    return [centre, appObserver, sceneObserver]
    {
        [centre removeObserver:appObserver];
        [centre removeObserver:sceneObserver];
    };
}
}
#endif
